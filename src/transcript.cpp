#include "transcript.h"
#include "json.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>

using json = nlohmann::json;
namespace fs = std::filesystem;

TranscriptHandler::TranscriptHandler() {}
TranscriptHandler::~TranscriptHandler() {}

bool TranscriptHandler::Load(const std::string& audioPath, const std::string& transcriptDir) {
    segments.clear();
    fs::path p(audioPath);
    std::string stem = p.stem().string(); // e.g., 2025_12_06_23_46_31_1
    std::string extension = p.extension().string(); // e.g., .mp3
    
    // Pattern: {stem}_mp3_whisper_large-v3.json
    // But the extension might not be .mp3. Let's handle it by replacing dot with nothing or similar.
    // The prompt says: 2025_12_06_23_46_31_1.mp3 is called 2025_12_06_23_46_31_1_mp3_whisper_large-v3.json
    std::string suffix = extension;
    if (!suffix.empty() && suffix[0] == '.') {
        suffix = suffix.substr(1); // remove the dot
    }
    
    std::string transcriptName = stem + "_" + suffix + "_whisper_large-v3.json";
    fs::path transcriptPath = fs::path(transcriptDir) / transcriptName;
    
    if (!fs::exists(transcriptPath)) {
        std::cerr << "Transcript not found: " << transcriptPath << std::endl;
        return false;
    }
    
    try {
        std::ifstream f(transcriptPath);
        json data = json::parse(f);
        
        if (data.contains("segments") && data["segments"].is_array()) {
            for (auto& seg : data["segments"]) {
                Segment s;
                s.start = seg.value("start", 0.0);
                s.end = seg.value("end", 0.0);
                s.text = seg.value("text", "");
                segments.push_back(s);
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing transcript: " << e.what() << std::endl;
        return false;
    }
}
