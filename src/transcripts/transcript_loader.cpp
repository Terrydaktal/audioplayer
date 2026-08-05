#include "transcript.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include <sqlite3.h>

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

TranscriptHandler::TranscriptHandler() = default;
TranscriptHandler::~TranscriptHandler() = default;

bool TranscriptHandler::Load(const std::string& audioPath, const std::string& transcriptDir) {
    const fs::path audio(audioPath);
    std::string suffix = audio.extension().string();
    if (!suffix.empty() && suffix.front() == '.') suffix.erase(0, 1);

    const std::string transcriptName =
        audio.stem().string() + "_" + suffix + "_whisper_large-v3.json";
    const fs::path transcriptPath = fs::path(transcriptDir) / transcriptName;
    if (LoadTranscriptFile(transcriptPath.string())) return true;

    const std::string databasePath = (fs::path(transcriptDir) / "transcripts.db").string();
    return LoadTranscriptDatabase(databasePath, transcriptName) ||
           LoadTranscriptDatabase(databasePath, audio.filename().string());
}

bool TranscriptHandler::LoadTranscriptFile(const std::string& transcriptPath) {
    segments.clear();
    if (!fs::exists(transcriptPath)) return false;

    try {
        std::ifstream file(transcriptPath);
        const json data = json::parse(file);
        if (data.contains("segments") && data["segments"].is_array()) {
            for (const auto& value : data["segments"]) {
                segments.push_back({
                    value.value("start", 0.0),
                    value.value("end", 0.0),
                    value.value("text", "")
                });
            }
        }
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Error parsing transcript: " << error.what() << std::endl;
        return false;
    }
}

bool TranscriptHandler::LoadTranscriptDatabase(const std::string& databasePath,
                                               const std::string& transcriptKey) {
    segments.clear();

    sqlite3* database = nullptr;
    if (sqlite3_open_v2(databasePath.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (database) sqlite3_close(database);
        return false;
    }
    sqlite3_busy_timeout(database, 5000);

    const char* sql =
        "SELECT s.start_time, s.end_time, s.text "
        "FROM segments s JOIN transcripts t ON t.id = s.transcript_id "
        "WHERE t.filename = ? OR t.audio_filename = ? OR t.input = ? "
        "ORDER BY s.start_time, s.id;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        sqlite3_close(database);
        return false;
    }
    sqlite3_bind_text(statement, 1, transcriptKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, transcriptKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, transcriptKey.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(statement, 2);
        segments.push_back({
            sqlite3_column_double(statement, 0),
            sqlite3_column_double(statement, 1),
            text ? reinterpret_cast<const char*>(text) : ""
        });
        found = true;
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return found;
}

std::string TranscriptToAudioFilename(const std::string& jsonFilename) {
    constexpr const char* suffix = "_whisper_large-v3.json";
    const std::string suffixString = suffix;
    if (jsonFilename.size() <= suffixString.size() ||
        jsonFilename.compare(jsonFilename.size() - suffixString.size(), suffixString.size(), suffixString) != 0) {
        return {};
    }

    const std::string base = jsonFilename.substr(0, jsonFilename.size() - suffixString.size());
    const std::size_t lastUnderscore = base.find_last_of('_');
    if (lastUnderscore == std::string::npos || lastUnderscore == 0) return {};
    return base.substr(0, lastUnderscore) + "." + base.substr(lastUnderscore + 1);
}
