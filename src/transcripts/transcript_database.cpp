#include "transcript.h"

#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "json.hpp"
#include "transcripts/transcript_fingerprint.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kFtsSchemaVersion = 1;
constexpr int kTranscriptSchemaVersion = 4;

struct TranscriptMetadata {
    std::string anchorTime;
    std::string anchorTimeSource;
    std::string anchorTimeAssumed;
    bool anchorTimeUncertain = false;
};

TranscriptMetadata ExtractTranscriptMetadata(const json& data) {
    TranscriptMetadata metadata;
    auto stringValue = [&data](const char* key) {
        return data.contains(key) && data[key].is_string()
            ? data[key].get<std::string>()
            : std::string();
    };
    metadata.anchorTime = stringValue("anchor_time");
    metadata.anchorTimeSource = stringValue("anchor_time_src");
    metadata.anchorTimeAssumed = stringValue("anchor_time_assumed");
    if (data.contains("anchor_time_uncertain") && data["anchor_time_uncertain"].is_boolean()) {
        metadata.anchorTimeUncertain = data["anchor_time_uncertain"].get<bool>();
    }
    return metadata;
}

struct ParsedTranscript {
    std::vector<Segment> segments;
    TranscriptMetadata metadata;
    std::string metadataJson;
    std::string metadataHash;
    std::string contentHash;
};

struct DbFile {
    sqlite3_int64 id = 0;
    std::string filename;
    long long mtime = 0;
    long long sourceMtimeNs = 0;
    long long sourceSizeBytes = 0;
    std::string contentHash;
    std::string audioFilename;
    std::string input;
};

bool ExecSQL(sqlite3* database, const std::string& sql);
bool DatabaseHasColumn(sqlite3* database, const char* table, const char* column);

std::string SqliteText(sqlite3_stmt* statement, int column) {
    const unsigned char* value = sqlite3_column_text(statement, column);
    return value ? reinterpret_cast<const char*>(value) : std::string();
}

bool EnsureColumn(sqlite3* database, const char* table, const char* column, const char* definition) {
    if (DatabaseHasColumn(database, table, column)) return true;
    return ExecSQL(database, "ALTER TABLE " + std::string(table) + " ADD COLUMN " +
        std::string(column) + " " + std::string(definition) + ";");
}

bool ReadFileStat(const fs::directory_entry& entry,
                  long long& mtimeSeconds,
                  long long& mtimeNanoseconds,
                  long long& sizeBytes) {
    std::error_code error;
    const auto writeTime = entry.last_write_time(error);
    if (error) return false;
    const auto duration = writeTime.time_since_epoch();
    mtimeSeconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    mtimeNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

    const auto size = entry.file_size(error);
    if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<long long>::max())) return false;
    sizeBytes = static_cast<long long>(size);
    return true;
}

bool ParseJsonTranscript(const fs::path& path,
                         const std::string& audioFilename,
                         ParsedTranscript& parsedTranscript) {
    std::ifstream file(path);
    if (!file) return false;

    const json data = json::parse(file);
    if (!data.is_object() || !data.contains("segments") || !data["segments"].is_array()) return false;

    ParsedTranscript parsed;
    parsed.metadata = ExtractTranscriptMetadata(data);
    for (const auto& value : data["segments"]) {
        if (!value.is_object()) return false;

        Segment segment;
        if (value.contains("start") && !value["start"].is_number()) return false;
        if (value.contains("end") && !value["end"].is_number()) return false;
        if (value.contains("text") && !value["text"].is_string()) return false;
        segment.start = value.contains("start") ? value["start"].get<double>() : 0.0;
        segment.end = value.contains("end") ? value["end"].get<double>() : 0.0;
        segment.text = value.contains("text") ? value["text"].get<std::string>() : std::string();
        if (!std::isfinite(segment.start) || !std::isfinite(segment.end)) return false;
        parsed.segments.push_back(std::move(segment));
    }

    json metadata = data;
    metadata.erase("segments");
    parsed.metadataJson = metadata.dump();
    parsed.metadataHash = Sha256Hex(parsed.metadataJson);
    parsed.contentHash = TranscriptContentFingerprint(audioFilename, parsed.segments);
    parsedTranscript = std::move(parsed);
    return true;
}

bool ExecSQL(sqlite3* database, const std::string& sql) {
    char* error = nullptr;
    const int result = sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        std::cerr << "SQL error: " << (error ? error : "unknown")
                  << " (Query: " << sql << ")" << std::endl;
        sqlite3_free(error);
        return false;
    }
    return true;
}

int GetDatabaseUserVersion(sqlite3* database) {
    sqlite3_stmt* statement = nullptr;
    int version = 0;
    if (sqlite3_prepare_v2(database, "PRAGMA user_version;", -1, &statement, nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW) {
        version = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    return version;
}

bool DatabaseHasColumn(sqlite3* database, const char* table, const char* column) {
    const std::string query = "PRAGMA table_info(" + std::string(table) + ");";
    sqlite3_stmt* statement = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(database, query.c_str(), -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(statement, 1);
            if (name && std::string(reinterpret_cast<const char*>(name)) == column) {
                found = true;
                break;
            }
        }
    }
    sqlite3_finalize(statement);
    return found;
}

bool UpdateTranscriptRecord(sqlite3* database,
                            sqlite3_int64 id,
                            const std::string& filename,
                            long long mtime,
                            const std::string& audioFilename,
                            const TranscriptMetadata& metadata,
                            long long sourceMtimeNs,
                            long long sourceSizeBytes,
                            const std::string& contentHash,
                            const std::string& metadataHash,
                            const std::string& metadataJson) {
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "UPDATE transcripts SET filename = ?, mtime = ?, audio_filename = ?, anchor_time = ?, "
        "anchor_time_src = ?, anchor_time_assumed = ?, anchor_time_uncertain = ?, source_mtime_ns = ?, "
        "source_size_bytes = ?, content_hash = ?, metadata_hash = ?, metadata_json = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(statement, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, mtime);
    sqlite3_bind_text(statement, 3, audioFilename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, metadata.anchorTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, metadata.anchorTimeSource.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, metadata.anchorTimeAssumed.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 7, metadata.anchorTimeUncertain ? 1 : 0);
    sqlite3_bind_int64(statement, 8, sourceMtimeNs);
    sqlite3_bind_int64(statement, 9, sourceSizeBytes);
    sqlite3_bind_text(statement, 10, contentHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 11, metadataHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 12, metadataJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 13, id);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool InsertTranscriptRecord(sqlite3* database,
                            const std::string& filename,
                            long long mtime,
                            const std::string& audioFilename,
                            const TranscriptMetadata& metadata,
                            long long sourceMtimeNs,
                            long long sourceSizeBytes,
                            const std::string& contentHash,
                            const std::string& metadataHash,
                            const std::string& metadataJson,
                            sqlite3_int64& id) {
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO transcripts (filename, mtime, audio_filename, anchor_time, anchor_time_src, "
        "anchor_time_assumed, anchor_time_uncertain, source_mtime_ns, source_size_bytes, content_hash, "
        "metadata_hash, metadata_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(statement, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, mtime);
    sqlite3_bind_text(statement, 3, audioFilename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, metadata.anchorTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, metadata.anchorTimeSource.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, metadata.anchorTimeAssumed.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 7, metadata.anchorTimeUncertain ? 1 : 0);
    sqlite3_bind_int64(statement, 8, sourceMtimeNs);
    sqlite3_bind_int64(statement, 9, sourceSizeBytes);
    sqlite3_bind_text(statement, 10, contentHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 11, metadataHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 12, metadataJson.c_str(), -1, SQLITE_TRANSIENT);

    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!succeeded) return false;
    id = sqlite3_last_insert_rowid(database);
    return id > 0;
}

bool InsertSegments(sqlite3* database, sqlite3_int64 transcriptId, const std::vector<Segment>& segments) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database,
                           "INSERT INTO segments (transcript_id, start_time, end_time, text) "
                           "VALUES (?, ?, ?, ?);",
                           -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }

    bool succeeded = true;
    for (const auto& segment : segments) {
        sqlite3_bind_int64(statement, 1, transcriptId);
        sqlite3_bind_double(statement, 2, segment.start);
        sqlite3_bind_double(statement, 3, segment.end);
        sqlite3_bind_text(statement, 4, segment.text.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(statement) != SQLITE_DONE) {
            succeeded = false;
            break;
        }
        sqlite3_reset(statement);
        sqlite3_clear_bindings(statement);
    }
    sqlite3_finalize(statement);
    return succeeded;
}

bool DeleteTranscriptRecord(sqlite3* database, sqlite3_int64 id) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database,
                           "DELETE FROM transcripts WHERE id = ? AND (input IS NULL OR input = '');",
                           -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(statement, 1, id);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool BackfillContentIdentity(sqlite3* database,
                             sqlite3_int64 id,
                             const std::string& audioFilename,
                             const std::string& contentHash) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database,
                           "UPDATE transcripts SET audio_filename = ?, content_hash = ? WHERE id = ?;",
                           -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(statement, 1, audioFilename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, contentHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 3, id);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool ComputeDatabaseContentFingerprint(sqlite3* database,
                                       sqlite3_int64 transcriptId,
                                       const std::string& audioFilename,
                                       std::string& contentHash) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database,
                           "SELECT start_time, end_time, text FROM segments WHERE transcript_id = ? "
                           "ORDER BY start_time, id;",
                           -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int64(statement, 1, transcriptId);

    std::vector<Segment> segments;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        segments.push_back({
            sqlite3_column_double(statement, 0),
            sqlite3_column_double(statement, 1),
            SqliteText(statement, 2)
        });
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) return false;

    contentHash = TranscriptContentFingerprint(audioFilename, segments);
    return true;
}

std::string ContentKey(const std::string& audioFilename, const std::string& contentHash) {
    return audioFilename + "\x1f" + contentHash;
}

std::string DirectAudioKey(const DbFile& file) {
    if (!file.audioFilename.empty()) return file.audioFilename;
    if (!file.input.empty()) return fs::path(file.input).filename().string();
    return {};
}

void RemoveContentIndex(std::map<std::string, DbFile>& contentFiles, sqlite3_int64 id) {
    for (auto iterator = contentFiles.begin(); iterator != contentFiles.end();) {
        if (iterator->second.id == id) {
            iterator = contentFiles.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void UpdateDatabase(sqlite3* database, const std::string& transcriptDir) {
    try {
        if (!ExecSQL(database, "CREATE TABLE IF NOT EXISTS transcripts ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "filename TEXT UNIQUE,"
                              "mtime INTEGER,"
                              "audio_filename TEXT,"
                              "anchor_time TEXT,"
                              "anchor_time_src TEXT,"
                              "anchor_time_assumed TEXT,"
                              "anchor_time_uncertain INTEGER DEFAULT 0,"
                              "input TEXT,"
                              "source_mtime_ns INTEGER,"
                              "source_size_bytes INTEGER,"
                              "content_hash TEXT,"
                              "metadata_hash TEXT,"
                              "metadata_json TEXT"
                              ");")) return;
        if (!EnsureColumn(database, "transcripts", "anchor_time", "TEXT") ||
            !EnsureColumn(database, "transcripts", "anchor_time_src", "TEXT") ||
            !EnsureColumn(database, "transcripts", "anchor_time_assumed", "TEXT") ||
            !EnsureColumn(database, "transcripts", "anchor_time_uncertain", "INTEGER DEFAULT 0") ||
            !EnsureColumn(database, "transcripts", "audio_filename", "TEXT") ||
            !EnsureColumn(database, "transcripts", "input", "TEXT") ||
            !EnsureColumn(database, "transcripts", "source_mtime_ns", "INTEGER") ||
            !EnsureColumn(database, "transcripts", "source_size_bytes", "INTEGER") ||
            !EnsureColumn(database, "transcripts", "content_hash", "TEXT") ||
            !EnsureColumn(database, "transcripts", "metadata_hash", "TEXT") ||
            !EnsureColumn(database, "transcripts", "metadata_json", "TEXT")) return;

        if (!ExecSQL(database, "CREATE TABLE IF NOT EXISTS segments ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "transcript_id INTEGER,"
                              "start_time REAL,"
                              "end_time REAL,"
                              "text TEXT,"
                              "FOREIGN KEY(transcript_id) REFERENCES transcripts(id) ON DELETE CASCADE"
                              ");") ||
            !ExecSQL(database, "CREATE INDEX IF NOT EXISTS idx_segments_transcript_id ON segments(transcript_id);") ||
            !ExecSQL(database, "CREATE INDEX IF NOT EXISTS idx_segments_transcript_time "
                              "ON segments(transcript_id, start_time, end_time);")) return;
        const bool ftsReady =
            ExecSQL(database, "CREATE VIRTUAL TABLE IF NOT EXISTS segments_fts USING fts5("
                             "text, content='segments', content_rowid='id', tokenize='trigram');") &&
            ExecSQL(database, "CREATE TRIGGER IF NOT EXISTS segments_ai AFTER INSERT ON segments BEGIN "
                             "INSERT INTO segments_fts(rowid, text) VALUES (new.id, new.text); END;") &&
            ExecSQL(database, "CREATE TRIGGER IF NOT EXISTS segments_ad AFTER DELETE ON segments BEGIN "
                             "INSERT INTO segments_fts(segments_fts, rowid, text) "
                             "VALUES ('delete', old.id, old.text); END;") &&
            ExecSQL(database, "CREATE TRIGGER IF NOT EXISTS segments_au AFTER UPDATE ON segments BEGIN "
                             "INSERT INTO segments_fts(segments_fts, rowid, text) VALUES ('delete', old.id, old.text); "
                             "INSERT INTO segments_fts(rowid, text) VALUES (new.id, new.text); END;");
        if (!ftsReady) return;

        const int databaseVersion = GetDatabaseUserVersion(database);
        const bool rebuildFts = ftsReady && databaseVersion < kFtsSchemaVersion;
        const bool fingerprintMigration = databaseVersion < kTranscriptSchemaVersion;
        if (!ExecSQL(database, "PRAGMA foreign_keys = ON;") ||
            !ExecSQL(database, "PRAGMA journal_mode = WAL;") ||
            !ExecSQL(database, "PRAGMA synchronous = NORMAL;")) return;

        std::map<std::string, DbFile> databaseFiles;
        std::map<std::string, DbFile> contentFiles;
        std::map<std::string, DbFile> directAudioFiles;
        sqlite3_stmt* statement = nullptr;
        const char* selectSql =
            "SELECT id, filename, mtime, COALESCE(source_mtime_ns, 0), "
            "COALESCE(source_size_bytes, 0), COALESCE(content_hash, ''), "
            "COALESCE(audio_filename, ''), COALESCE(input, '') FROM transcripts;";
        if (sqlite3_prepare_v2(database, selectSql, -1, &statement, nullptr) != SQLITE_OK) return;
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const std::string filename = SqliteText(statement, 1);
            if (filename.empty()) continue;

            DbFile file;
            file.id = sqlite3_column_int64(statement, 0);
            file.filename = filename;
            file.mtime = sqlite3_column_int64(statement, 2);
            file.sourceMtimeNs = sqlite3_column_int64(statement, 3);
            file.sourceSizeBytes = sqlite3_column_int64(statement, 4);
            file.contentHash = SqliteText(statement, 5);
            file.audioFilename = SqliteText(statement, 6);
            file.input = SqliteText(statement, 7);
            databaseFiles[filename] = file;
            if (!file.contentHash.empty() && !file.audioFilename.empty())
                contentFiles[ContentKey(file.audioFilename, file.contentHash)] = file;
            const std::string directAudio = DirectAudioKey(file);
            if (!file.input.empty() && !directAudio.empty()) directAudioFiles[directAudio] = file;
        }
        sqlite3_finalize(statement);

        if (!ExecSQL(database, "BEGIN TRANSACTION;")) return;

        // Populate fingerprints for rows created by older importer versions. This makes
        // duplicate detection work even when the original JSON source is no longer present.
        for (auto& item : databaseFiles) {
            DbFile& file = item.second;
            if (!file.input.empty() || !file.contentHash.empty()) continue;
            const std::string audioName = file.audioFilename.empty()
                ? TranscriptToAudioFilename(file.filename)
                : file.audioFilename;
            if (audioName.empty()) continue;

            std::string contentHash;
            if (!ComputeDatabaseContentFingerprint(database, file.id, audioName, contentHash) ||
                !BackfillContentIdentity(database, file.id, audioName, contentHash)) {
                throw std::runtime_error("Unable to backfill transcript fingerprint");
            }
            file.audioFilename = audioName;
            file.contentHash = contentHash;
            contentFiles[ContentKey(audioName, contentHash)] = file;
        }

        std::set<std::string> activeFiles;
        std::error_code directoryError;
        for (const auto& entry : fs::directory_iterator(transcriptDir, directoryError)) {
            if (directoryError) break;
            std::error_code entryError;
            if (!entry.is_regular_file(entryError)) continue;

            const std::string filename = entry.path().filename().string();
            const std::string audioName = TranscriptToAudioFilename(filename);
            if (audioName.empty()) continue;
            activeFiles.insert(filename);

            const auto existing = databaseFiles.find(filename);
            if (existing != databaseFiles.end() && !existing->second.input.empty()) continue;

            long long mtimeSeconds = 0;
            long long sourceMtimeNs = 0;
            long long sourceSizeBytes = 0;
            if (!ReadFileStat(entry, mtimeSeconds, sourceMtimeNs, sourceSizeBytes)) continue;

            if (existing != databaseFiles.end() && !fingerprintMigration &&
                existing->second.sourceMtimeNs == sourceMtimeNs &&
                existing->second.sourceSizeBytes == sourceSizeBytes &&
                !existing->second.contentHash.empty()) {
                continue;
            }

            ParsedTranscript parsed;
            try {
                if (!ParseJsonTranscript(entry.path(), audioName, parsed)) continue;
            } catch (const std::exception& error) {
                std::cerr << "Ignoring malformed transcript " << entry.path() << ": "
                          << error.what() << std::endl;
                continue;
            } catch (...) {
                std::cerr << "Ignoring malformed transcript " << entry.path() << std::endl;
                continue;
            }

            if (existing != databaseFiles.end()) {
                std::string existingContentHash = existing->second.contentHash;
                if (existingContentHash.empty() &&
                    !ComputeDatabaseContentFingerprint(database, existing->second.id, audioName, existingContentHash)) {
                    throw std::runtime_error("Unable to fingerprint existing transcript");
                }

                if (existingContentHash == parsed.contentHash) {
                    if (!UpdateTranscriptRecord(database, existing->second.id, filename, mtimeSeconds, audioName,
                                                parsed.metadata, sourceMtimeNs, sourceSizeBytes, parsed.contentHash,
                                                parsed.metadataHash, parsed.metadataJson)) {
                        throw std::runtime_error("Unable to update transcript metadata");
                    }
                    DbFile updated = existing->second;
                    updated.mtime = mtimeSeconds;
                    updated.sourceMtimeNs = sourceMtimeNs;
                    updated.sourceSizeBytes = sourceSizeBytes;
                    updated.contentHash = parsed.contentHash;
                    updated.audioFilename = audioName;
                    databaseFiles[filename] = updated;
                    RemoveContentIndex(contentFiles, updated.id);
                    contentFiles[ContentKey(audioName, updated.contentHash)] = updated;
                    continue;
                }

                const DbFile existingFile = existing->second;
                if (!DeleteTranscriptRecord(database, existingFile.id))
                    throw std::runtime_error("Unable to replace changed transcript");
                databaseFiles.erase(existing);
                RemoveContentIndex(contentFiles, existingFile.id);
            } else if (directAudioFiles.find(audioName) != directAudioFiles.end()) {
                continue;
            } else {
                const auto duplicate = contentFiles.find(ContentKey(audioName, parsed.contentHash));
                if (duplicate != contentFiles.end()) {
                    if (activeFiles.find(duplicate->second.filename) == activeFiles.end()) {
                        const std::string duplicateFilename = duplicate->second.filename;
                        if (!duplicateFilename.empty()) {
                            if (!UpdateTranscriptRecord(database, duplicate->second.id, filename, mtimeSeconds, audioName,
                                                        parsed.metadata, sourceMtimeNs, sourceSizeBytes, parsed.contentHash,
                                                        parsed.metadataHash, parsed.metadataJson)) {
                                throw std::runtime_error("Unable to rename duplicate transcript");
                            }
                            DbFile renamed = duplicate->second;
                            renamed.filename = filename;
                            renamed.mtime = mtimeSeconds;
                            renamed.sourceMtimeNs = sourceMtimeNs;
                            renamed.sourceSizeBytes = sourceSizeBytes;
                            renamed.contentHash = parsed.contentHash;
                            renamed.audioFilename = audioName;
                            databaseFiles.erase(duplicateFilename);
                            databaseFiles[filename] = renamed;
                            RemoveContentIndex(contentFiles, renamed.id);
                            contentFiles[ContentKey(audioName, renamed.contentHash)] = renamed;
                        }
                    }
                    continue;
                }
            }

            sqlite3_int64 transcriptId = 0;
            if (!InsertTranscriptRecord(database, filename, mtimeSeconds, audioName, parsed.metadata,
                                        sourceMtimeNs, sourceSizeBytes, parsed.contentHash, parsed.metadataHash,
                                        parsed.metadataJson, transcriptId) ||
                !InsertSegments(database, transcriptId, parsed.segments)) {
                if (transcriptId > 0) DeleteTranscriptRecord(database, transcriptId);
                throw std::runtime_error("Unable to import transcript");
            }

            DbFile imported;
            imported.id = transcriptId;
            imported.filename = filename;
            imported.mtime = mtimeSeconds;
            imported.sourceMtimeNs = sourceMtimeNs;
            imported.sourceSizeBytes = sourceSizeBytes;
            imported.contentHash = parsed.contentHash;
            imported.audioFilename = audioName;
            databaseFiles[filename] = imported;
            contentFiles[ContentKey(audioName, parsed.contentHash)] = imported;
        }
        if (directoryError) throw std::runtime_error("Unable to enumerate transcript directory: " + directoryError.message());

        if (!ExecSQL(database, "COMMIT TRANSACTION;")) throw std::runtime_error("Unable to commit transcript sync");
        if (rebuildFts && ExecSQL(database, "INSERT INTO segments_fts(segments_fts) VALUES ('rebuild');"))
            ExecSQL(database, "INSERT INTO segments_fts(segments_fts) VALUES ('optimize');");
        if (fingerprintMigration) ExecSQL(database, "PRAGMA user_version = 4;");
    } catch (const std::exception& error) {
        std::cerr << "Exception in UpdateDatabase: " << error.what() << std::endl;
        ExecSQL(database, "ROLLBACK;");
    } catch (...) {
        std::cerr << "Unknown exception in UpdateDatabase" << std::endl;
        ExecSQL(database, "ROLLBACK;");
    }
}

} // namespace

void SyncTranscriptDatabase(const std::string& transcriptDir) {
    try {
        if (!fs::exists(transcriptDir) || !fs::is_directory(transcriptDir)) return;
        const fs::path databasePath = fs::path(transcriptDir) / "transcripts.db";
        sqlite3* database = nullptr;
        if (sqlite3_open(databasePath.string().c_str(), &database) != SQLITE_OK) {
            sqlite3_close(database);
            return;
        }
        sqlite3_busy_timeout(database, 5000);
        UpdateDatabase(database, transcriptDir);
        sqlite3_close(database);
    } catch (const std::exception& error) {
        std::cerr << "Database sync failed: " << error.what() << std::endl;
    } catch (...) {
        std::cerr << "Database sync failed with unknown error" << std::endl;
    }
}
