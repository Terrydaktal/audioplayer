#include "transcripts/transcript_search.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "fuzzy_rank_bridge.h"

namespace fs = std::filesystem;

namespace {

constexpr int kSearchCandidateLimit = 5000;
constexpr double kSearchContextSeconds = 20.0;
constexpr double kSearchWindowMaxSpanSeconds = 60.0;

std::string BuildFtsQuery(const std::string& query) {
    std::vector<std::string> terms;
    std::string term;
    auto finishTerm = [&]() {
        if (term.size() >= 3) terms.push_back(term);
        term.clear();
    };
    for (unsigned char character : query) {
        if (std::isalnum(character) || character >= 0x80) term.push_back(static_cast<char>(character));
        else finishTerm();
    }
    finishTerm();

    std::string result;
    for (const auto& value : terms) {
        if (!result.empty()) result += " OR ";
        result += "\"" + value + "\"";
    }
    return result;
}

} // namespace

void FindTranscriptMatches(const std::vector<Segment>& segments,
                           const std::string& query,
                           std::vector<int>& matches,
                           int& currentMatch) {
    matches.clear();
    if (query.empty()) {
        currentMatch = -1;
        return;
    }

    std::string loweredQuery = query;
    std::transform(loweredQuery.begin(), loweredQuery.end(), loweredQuery.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    for (int index = 0; index < static_cast<int>(segments.size()); ++index) {
        std::string loweredText = segments[index].text;
        std::transform(loweredText.begin(), loweredText.end(), loweredText.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (loweredText.find(loweredQuery) != std::string::npos) matches.push_back(index);
    }
    if (matches.empty()) {
        currentMatch = -1;
    } else if (currentMatch < 0 || currentMatch >= static_cast<int>(matches.size())) {
        currentMatch = 0;
    }
}

std::vector<GlobalSearchResult> SearchAllTranscripts(const std::string& transcriptDir,
                                                     const std::string& query) {
    std::vector<GlobalSearchResult> results;
    if (query.empty() || !fs::exists(transcriptDir) || !fs::is_directory(transcriptDir)) return results;

    const fs::path databasePath = fs::path(transcriptDir) / "transcripts.db";
    sqlite3* database = nullptr;
    int result = sqlite3_open_v2(databasePath.string().c_str(), &database, SQLITE_OPEN_READONLY, nullptr);
    if (result != SQLITE_OK) {
        sqlite3_close(database);
        result = sqlite3_open(databasePath.string().c_str(), &database);
        if (result != SQLITE_OK) {
            sqlite3_close(database);
            return results;
        }
    }
    sqlite3_busy_timeout(database, 5000);

    struct SearchAnchor {
        long long transcriptId;
        long long segmentId;
        double start;
        double end;
        double score;
    };
    struct TranscriptAnchors {
        std::string filename;
        std::string audioFilename;
        std::string recordingTime;
        bool recordingTimeUncertain = false;
        std::vector<SearchAnchor> anchors;
    };
    struct SearchCandidate {
        GlobalSearchResult result;
        double score;
    };

    std::map<long long, TranscriptAnchors> transcriptAnchors;
    const std::string ftsQuery = BuildFtsQuery(query);
    bool useFts = !ftsQuery.empty();
    const char* ftsSql =
        "SELECT s.id, s.transcript_id, t.filename, t.audio_filename, t.anchor_time, t.anchor_time_uncertain, "
        "s.start_time, s.end_time, -bm25(segments_fts) "
        "FROM segments_fts JOIN segments s ON s.id = segments_fts.rowid "
        "JOIN transcripts t ON t.id = s.transcript_id WHERE segments_fts MATCH ? "
        "ORDER BY bm25(segments_fts) LIMIT ?;";
    const char* likeSql =
        "SELECT s.id, s.transcript_id, t.filename, t.audio_filename, t.anchor_time, t.anchor_time_uncertain, "
        "s.start_time, s.end_time, 0.0 FROM segments s JOIN transcripts t ON t.id = s.transcript_id "
        "WHERE s.text LIKE ? LIMIT ?;";

    sqlite3_stmt* statement = nullptr;
    if (useFts && sqlite3_prepare_v2(database, ftsSql, -1, &statement, nullptr) != SQLITE_OK) {
        sqlite3_finalize(statement);
        statement = nullptr;
        useFts = false;
    }
    if (!useFts && sqlite3_prepare_v2(database, likeSql, -1, &statement, nullptr) != SQLITE_OK) {
        sqlite3_close(database);
        return results;
    }
    const std::string searchValue = useFts ? ftsQuery : "%" + query + "%";
    sqlite3_bind_text(statement, 1, searchValue.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, kSearchCandidateLimit);

    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* filenameColumn = sqlite3_column_text(statement, 2);
        const std::string filename = filenameColumn ? reinterpret_cast<const char*>(filenameColumn) : "";
        if (filename.empty()) continue;
        const long long transcriptId = sqlite3_column_int64(statement, 1);
        auto& transcript = transcriptAnchors[transcriptId];
        const unsigned char* audioColumn = sqlite3_column_text(statement, 3);
        const unsigned char* timeColumn = sqlite3_column_text(statement, 4);
        transcript.filename = filename;
        transcript.audioFilename = audioColumn ? reinterpret_cast<const char*>(audioColumn) : "";
        transcript.recordingTime = timeColumn ? reinterpret_cast<const char*>(timeColumn) : "";
        transcript.recordingTimeUncertain = sqlite3_column_int(statement, 5) != 0;
        transcript.anchors.push_back({
            transcriptId,
            sqlite3_column_int64(statement, 0),
            sqlite3_column_double(statement, 6),
            sqlite3_column_double(statement, 7),
            sqlite3_column_double(statement, 8)
        });
    }
    sqlite3_finalize(statement);
    if (transcriptAnchors.empty()) {
        sqlite3_close(database);
        return results;
    }

    struct ContextSegment { long long id; double start; double end; std::string text; };
    struct AnchorWindow {
        double rangeStart;
        double rangeEnd;
        std::vector<const SearchAnchor*> anchors;
    };
    std::vector<SearchCandidate> candidates;
    const char* contextSql =
        "SELECT id, start_time, end_time, text FROM segments "
        "WHERE transcript_id = ? AND end_time >= ? AND start_time <= ? ORDER BY start_time, id;";
    sqlite3_stmt* contextStatement = nullptr;
    if (sqlite3_prepare_v2(database, contextSql, -1, &contextStatement, nullptr) != SQLITE_OK) {
        sqlite3_finalize(contextStatement);
        sqlite3_close(database);
        return results;
    }

    for (auto& pair : transcriptAnchors) {
        auto& transcript = pair.second;
        auto& anchors = transcript.anchors;
        std::sort(anchors.begin(), anchors.end(), [](const SearchAnchor& left, const SearchAnchor& right) {
            if (left.start != right.start) return left.start < right.start;
            return left.segmentId < right.segmentId;
        });

        std::vector<AnchorWindow> windows;
        for (const auto& anchor : anchors) {
            const double rangeStart = std::max(0.0, anchor.start - kSearchContextSeconds);
            const double rangeEnd = anchor.end + kSearchContextSeconds;
            if (windows.empty() ||
                anchor.start > windows.back().anchors.back()->end + kSearchContextSeconds ||
                rangeEnd - windows.back().rangeStart > kSearchWindowMaxSpanSeconds) {
                windows.push_back({rangeStart, rangeEnd, {&anchor}});
            } else {
                windows.back().rangeEnd = std::max(windows.back().rangeEnd, rangeEnd);
                windows.back().anchors.push_back(&anchor);
            }
        }

        for (const auto& window : windows) {
            sqlite3_reset(contextStatement);
            sqlite3_clear_bindings(contextStatement);
            sqlite3_bind_int64(contextStatement, 1, pair.first);
            sqlite3_bind_double(contextStatement, 2, window.rangeStart);
            sqlite3_bind_double(contextStatement, 3, window.rangeEnd);
            std::vector<ContextSegment> context;
            while (sqlite3_step(contextStatement) == SQLITE_ROW) {
                const unsigned char* text = sqlite3_column_text(contextStatement, 3);
                context.push_back({
                    sqlite3_column_int64(contextStatement, 0),
                    sqlite3_column_double(contextStatement, 1),
                    sqlite3_column_double(contextStatement, 2),
                    text ? reinterpret_cast<const char*>(text) : ""
                });
            }
            if (context.empty()) continue;
            std::string combined;
            for (const auto& segment : context) {
                if (segment.text.empty()) continue;
                if (!combined.empty()) combined += "\n";
                combined += segment.text;
            }
            if (combined.empty()) continue;

            double start = window.anchors.front()->start;
            double end = window.anchors.front()->end;
            double score = window.anchors.front()->score;
            for (const SearchAnchor* anchor : window.anchors) {
                start = std::min(start, anchor->start);
                end = std::max(end, anchor->end);
                score = std::max(score, anchor->score);
            }
            GlobalSearchResult item;
            item.audioFilename = transcript.audioFilename.empty()
                ? TranscriptToAudioFilename(transcript.filename)
                : transcript.audioFilename;
            item.transcriptFilename = transcript.filename;
            item.recordingTime = transcript.recordingTime;
            item.recordingTimeUncertain = transcript.recordingTimeUncertain;
            item.start = start;
            item.end = end;
            item.text = std::move(combined);
            candidates.push_back({std::move(item), score});
        }
    }
    sqlite3_finalize(contextStatement);
    sqlite3_close(database);
    if (candidates.empty()) return results;

    std::vector<FuzzyRankCandidate> rankCandidates;
    rankCandidates.reserve(candidates.size());
    for (const auto& candidate : candidates) rankCandidates.push_back({candidate.result.text.c_str(), candidate.score});
    std::vector<std::size_t> rankedIndices(candidates.size());
    const std::size_t rankedCount = fuzzy_rank_transcript_windows(
        query.c_str(), rankCandidates.data(), rankCandidates.size(), rankedIndices.data(), rankedIndices.size());
    results.reserve(rankedCount > 0 ? rankedCount : candidates.size());
    if (rankedCount > 0) {
        for (std::size_t index = 0; index < rankedCount; ++index) {
            if (rankedIndices[index] < candidates.size()) results.push_back(std::move(candidates[rankedIndices[index]].result));
        }
    } else {
        for (auto& candidate : candidates) results.push_back(std::move(candidate.result));
    }
    return results;
}
