#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include "transcript.h"

namespace fs = std::filesystem;

int main() {
    // Create mock directory and transcript
    fs::create_directories("mock_transcripts");
    std::ofstream f("mock_transcripts/test_mp3_whisper_large-v3.json");
    f << R"({
      "segments": [
        {"start": 5.5, "end": 10.0, "text": "Segment 1"},
        {"start": 10.0, "end": 15.2, "text": "Segment 2"}
      ]
    })";
    f.close();

    TranscriptHandler handler;
    // test.mp3 -> mock_transcripts/test_mp3_whisper_large-v3.json
    bool loaded = handler.Load("test.mp3", "mock_transcripts");
    assert(loaded);
    
    const auto& segments = handler.GetSegments();
    assert(segments.size() == 2);
    assert(segments[0].start == 5.5);
    assert(segments[1].text == "Segment 2");

    std::cout << "TranscriptHandler test passed!" << std::endl;

    // Cleanup
    fs::remove_all("mock_transcripts");
    return 0;
}
