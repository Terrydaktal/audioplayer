#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include "transcript.h"

namespace fs = std::filesystem;

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
    fs::remove("mock_transcripts/test_mp3_whisper_large-v3.json");
    TranscriptHandler databaseOnlyHandler;
    assert(databaseOnlyHandler.Load("test.mp3", "mock_transcripts"));
    assert(databaseOnlyHandler.GetSegments().size() == 2);
    assert(databaseOnlyHandler.GetSegments()[1].text == "Segment 2");

    auto searchResults = SearchAllTranscripts("mock_transcripts", "Segment 1");
    assert(!searchResults.empty());
    assert(searchResults[0].audioFilename == "test.mp3");
    assert(searchResults[0].transcriptFilename == "test_mp3_whisper_large-v3.json");
    assert(searchResults[0].recordingTime == "2020-06-21T14:52:14+01:00");
    assert(!searchResults[0].recordingTimeUncertain);
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
