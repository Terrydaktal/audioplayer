#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include <sqlite3.h>

#include "transcript.h"

namespace fs = std::filesystem;

int DatabaseCount(const std::string& databasePath, const char* table) {
    sqlite3* database = nullptr;
    assert(sqlite3_open_v2(databasePath.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    const std::string query = std::string("SELECT COUNT(*) FROM ") + table + ";";
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(database, query.c_str(), -1, &statement, nullptr) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_ROW);
    const int count = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return count;
}

std::string DatabaseMetadata(const std::string& databasePath) {
    sqlite3* database = nullptr;
    assert(sqlite3_open_v2(databasePath.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(database,
                              "SELECT metadata_json FROM transcripts WHERE filename = ?;",
                              -1, &statement, nullptr) == SQLITE_OK);
    const std::string filename = "test_mp3_whisper_large-v3.json";
    sqlite3_bind_text(statement, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(statement) == SQLITE_ROW);
    const unsigned char* value = sqlite3_column_text(statement, 0);
    const std::string metadata = value ? reinterpret_cast<const char*>(value) : std::string();
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return metadata;
}

int main() {
    // Create mock directory and transcript
    fs::remove_all("mock_transcripts");
    fs::create_directories("mock_transcripts");
    std::ofstream f("mock_transcripts/test_mp3_whisper_large-v3.json");
    f << R"({
      "anchor_time": "2020-06-21T14:52:14+01:00",
      "anchor_time_src": "earliest:modify",
      "anchor_time_assumed": "end_of_recording",
      "anchor_time_uncertain": false,
      "language": "en",
      "segments": [
        {"start": 5.5, "end": 10.0, "text": "Segment 1"},
        {"start": 10.0, "end": 15.2, "text": "Segment 2"}
      ]
    })";
    f.close();

    std::ofstream rankFile("mock_transcripts/ranking_mp3_whisper_large-v3.json");
    rankFile << R"({
      "segments": [
        {"start": 1.0, "end": 2.0, "text": "word1 separated by several words before word2"}
      ]
    })";
    rankFile.close();

    std::ofstream exactFile("mock_transcripts/exact_mp3_whisper_large-v3.json");
    exactFile << R"({
      "segments": [
        {"start": 1.0, "end": 2.0, "text": "word1 word2 together"}
      ]
    })";
    exactFile.close();

    std::ofstream boundaryFile("mock_transcripts/boundary_mp3_whisper_large-v3.json");
    boundaryFile << R"({
      "segments": [
        {"start": 20.0, "end": 22.0, "text": "The film involved a cannibal"},
        {"start": 22.0, "end": 24.0, "text": "mukbang scene and strange romance"},
        {"start": 24.0, "end": 26.0, "text": "afterwards."}
      ]
    })";
    boundaryFile.close();

    std::ofstream proximityFile("mock_transcripts/proximity_mp3_whisper_large-v3.json");
    proximityFile << R"({
      "segments": [
        {"start": 10.0, "end": 11.0, "text": "alpha"},
        {"start": 12.0, "end": 13.0, "text": "beta"},
        {"start": 100.0, "end": 101.0, "text": "alpha"},
        {"start": 102.0, "end": 103.0, "text": "beta"}
      ]
    })";
    proximityFile.close();

    TranscriptHandler handler;
    // test.mp3 -> mock_transcripts/test_mp3_whisper_large-v3.json
    bool loaded = handler.Load("test.mp3", "mock_transcripts");
    assert(loaded);
    
    const auto& segments = handler.GetSegments();
    assert(segments.size() == 2);
    assert(segments[0].start == 5.5);
    assert(segments[1].text == "Segment 2");

    // The transcript remains searchable even though test.mp3 does not exist.
    SyncTranscriptDatabase("mock_transcripts");
    SyncTranscriptDatabase("mock_transcripts");
    assert(DatabaseCount("mock_transcripts/transcripts.db", "transcripts") == 5);
    assert(DatabaseCount("mock_transcripts/transcripts.db", "segments") == 11);

    // A metadata-only edit updates the metadata without duplicating or rebuilding segments.
    std::ofstream metadataOnlyFile("mock_transcripts/test_mp3_whisper_large-v3.json");
    metadataOnlyFile << R"({
      "anchor_time": "2021-07-22T15:53:15+01:00",
      "anchor_time_src": "metadata-only-test",
      "anchor_time_assumed": "end_of_recording",
      "anchor_time_uncertain": true,
      "language": "en",
      "metadata_only_marker": "preserved",
      "segments": [
        {"start": 5.5, "end": 10.0, "text": "Segment 1"},
        {"start": 10.0, "end": 15.2, "text": "Segment 2"}
      ]
    })";
    metadataOnlyFile.close();
    SyncTranscriptDatabase("mock_transcripts");
    assert(DatabaseCount("mock_transcripts/transcripts.db", "transcripts") == 5);
    assert(DatabaseCount("mock_transcripts/transcripts.db", "segments") == 11);
    assert(DatabaseMetadata("mock_transcripts/transcripts.db").find("metadata_only_marker") != std::string::npos);

    auto metadataResults = SearchAllTranscripts("mock_transcripts", "Segment 1");
    assert(!metadataResults.empty());
    assert(metadataResults[0].recordingTime == "2021-07-22T15:53:15+01:00");
    assert(metadataResults[0].recordingTimeUncertain);

    fs::remove("mock_transcripts/test_mp3_whisper_large-v3.json");
    SyncTranscriptDatabase("mock_transcripts");
    TranscriptHandler databaseOnlyHandler;
    assert(databaseOnlyHandler.Load("test.mp3", "mock_transcripts"));
    assert(databaseOnlyHandler.GetSegments().size() == 2);
    assert(databaseOnlyHandler.GetSegments()[1].text == "Segment 2");

    auto searchResults = SearchAllTranscripts("mock_transcripts", "Segment 1");
    assert(!searchResults.empty());
    assert(searchResults[0].audioFilename == "test.mp3");
    assert(searchResults[0].transcriptFilename == "test_mp3_whisper_large-v3.json");
    assert(searchResults[0].recordingTime == "2021-07-22T15:53:15+01:00");
    assert(searchResults[0].recordingTimeUncertain);
    assert(searchResults[0].text.find("Segment 1") != std::string::npos);
    assert(searchResults[0].text.find("Segment 2") != std::string::npos);

    auto rankedResults = SearchAllTranscripts("mock_transcripts", "word1 word2");
    assert(rankedResults.size() == 2);
    assert(rankedResults[0].transcriptFilename == "exact_mp3_whisper_large-v3.json");
    assert(rankedResults[0].text.find("word1 word2 together") != std::string::npos);
    assert(rankedResults[1].transcriptFilename == "ranking_mp3_whisper_large-v3.json");

    auto boundaryResults = SearchAllTranscripts("mock_transcripts", "cannibal mukbang");
    assert(boundaryResults.size() == 1);
    assert(boundaryResults[0].transcriptFilename == "boundary_mp3_whisper_large-v3.json");
    assert(boundaryResults[0].start == 20.0);
    assert(boundaryResults[0].text.find("cannibal\nmukbang") != std::string::npos);
    assert(boundaryResults[0].text.find("strange romance") != std::string::npos);

    auto proximityResults = SearchAllTranscripts("mock_transcripts", "alpha beta");
    assert(proximityResults.size() == 2);
    assert(proximityResults[0].start == 10.0);
    assert(proximityResults[1].start == 100.0);

    auto substringResults = SearchAllTranscripts("mock_transcripts", "gment");
    assert(substringResults.size() == 1);
    assert(substringResults[0].text.find("Segment 1") != std::string::npos);
    assert(substringResults[0].text.find("Segment 2") != std::string::npos);

    std::cout << "TranscriptHandler test passed!" << std::endl;

    // Cleanup
    fs::remove_all("mock_transcripts");
    return 0;
}
