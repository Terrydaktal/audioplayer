#include "transcript.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

#include <sqlite3.h>

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kFtsSchemaVersion = 1;
constexpr int kTranscriptSchemaVersion = 3;

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

void UpdateDatabase(sqlite3* database, const std::string& transcriptDir) {
    try {
        ExecSQL(database, "CREATE TABLE IF NOT EXISTS transcripts ("
                         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                         "filename TEXT UNIQUE,"
                         "mtime INTEGER,"
                         "audio_filename TEXT,"
                         "anchor_time TEXT,"
                         "anchor_time_src TEXT,"
                         "anchor_time_assumed TEXT,"
                         "anchor_time_uncertain INTEGER DEFAULT 0"
                         ");");
        if (!DatabaseHasColumn(database, "transcripts", "anchor_time"))
            ExecSQL(database, "ALTER TABLE transcripts ADD COLUMN anchor_time TEXT;");
        if (!DatabaseHasColumn(database, "transcripts", "anchor_time_src"))
            ExecSQL(database, "ALTER TABLE transcripts ADD COLUMN anchor_time_src TEXT;");
        if (!DatabaseHasColumn(database, "transcripts", "anchor_time_assumed"))
            ExecSQL(database, "ALTER TABLE transcripts ADD COLUMN anchor_time_assumed TEXT;");
        if (!DatabaseHasColumn(database, "transcripts", "anchor_time_uncertain"))
            ExecSQL(database, "ALTER TABLE transcripts ADD COLUMN anchor_time_uncertain INTEGER DEFAULT 0;");
        if (!DatabaseHasColumn(database, "transcripts", "audio_filename"))
            ExecSQL(database, "ALTER TABLE transcripts ADD COLUMN audio_filename TEXT;");
        if (!DatabaseHasColumn(database, "transcripts", "input"))
            ExecSQL(database, "ALTER TABLE transcripts ADD COLUMN input TEXT;");

        ExecSQL(database, "CREATE TABLE IF NOT EXISTS segments ("
                         "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                         "transcript_id INTEGER,"
                         "start_time REAL,"
                         "end_time REAL,"
                         "text TEXT,"
                         "FOREIGN KEY(transcript_id) REFERENCES transcripts(id) ON DELETE CASCADE"
                         ");");
        ExecSQL(database, "CREATE INDEX IF NOT EXISTS idx_segments_transcript_id ON segments(transcript_id);");
        ExecSQL(database, "CREATE INDEX IF NOT EXISTS idx_segments_transcript_time "
                         "ON segments(transcript_id, start_time, end_time);");
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

        const int databaseVersion = GetDatabaseUserVersion(database);
        const bool rebuildFts = ftsReady && databaseVersion < kFtsSchemaVersion;
        const bool backfillMetadata = databaseVersion < kTranscriptSchemaVersion;
        ExecSQL(database, "PRAGMA foreign_keys = ON;");
        ExecSQL(database, "PRAGMA journal_mode = WAL;");
        ExecSQL(database, "PRAGMA synchronous = NORMAL;");

        struct DbFile { int id; long long mtime; };
        std::map<std::string, DbFile> databaseFiles;
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database, "SELECT id, filename, mtime FROM transcripts;", -1, &statement, nullptr) == SQLITE_OK) {
            while (sqlite3_step(statement) == SQLITE_ROW) {
                const int id = sqlite3_column_int(statement, 0);
                const unsigned char* name = sqlite3_column_text(statement, 1);
                const std::string filename = name ? reinterpret_cast<const char*>(name) : "";
                if (!filename.empty()) databaseFiles[filename] = {id, sqlite3_column_int64(statement, 2)};
            }
        }
        sqlite3_finalize(statement);

        ExecSQL(database, "BEGIN TRANSACTION;");
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

            std::error_code timeError;
            const auto writeTime = entry.last_write_time(timeError);
            if (timeError) continue;
            const auto mtime = std::chrono::duration_cast<std::chrono::seconds>(
                writeTime.time_since_epoch()).count();
            const auto existing = databaseFiles.find(filename);
            if (existing != databaseFiles.end()) {
                if (existing->second.mtime == mtime) {
                    if (backfillMetadata) {
                        try {
                            std::ifstream metadataFile(entry.path());
                            const TranscriptMetadata metadata = ExtractTranscriptMetadata(json::parse(metadataFile));
                            sqlite3_stmt* update = nullptr;
                            if (sqlite3_prepare_v2(database,
                                    "UPDATE transcripts SET audio_filename = ?, anchor_time = ?, anchor_time_src = ?, "
                                    "anchor_time_assumed = ?, anchor_time_uncertain = ? WHERE id = ?;",
                                    -1, &update, nullptr) == SQLITE_OK) {
                                sqlite3_bind_text(update, 1, audioName.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(update, 2, metadata.anchorTime.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(update, 3, metadata.anchorTimeSource.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(update, 4, metadata.anchorTimeAssumed.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_int(update, 5, metadata.anchorTimeUncertain ? 1 : 0);
                                sqlite3_bind_int(update, 6, existing->second.id);
                                sqlite3_step(update);
                            }
                            sqlite3_finalize(update);
                        } catch (...) {
                            // Keep an unchanged transcript searchable when metadata is malformed.
                        }
                    }
                    continue;
                }
                sqlite3_stmt* remove = nullptr;
                if (sqlite3_prepare_v2(database, "DELETE FROM transcripts WHERE id = ?;", -1, &remove, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(remove, 1, existing->second.id);
                    sqlite3_step(remove);
                }
                sqlite3_finalize(remove);
            }

            try {
                std::ifstream file(entry.path());
                const json data = json::parse(file);
                if (!data.contains("segments") || !data["segments"].is_array()) continue;
                const TranscriptMetadata metadata = ExtractTranscriptMetadata(data);
                sqlite3_stmt* insertTranscript = nullptr;
                if (sqlite3_prepare_v2(database,
                        "INSERT INTO transcripts (filename, mtime, audio_filename, anchor_time, anchor_time_src, "
                        "anchor_time_assumed, anchor_time_uncertain) VALUES (?, ?, ?, ?, ?, ?, ?);",
                        -1, &insertTranscript, nullptr) != SQLITE_OK) {
                    sqlite3_finalize(insertTranscript);
                    continue;
                }
                sqlite3_bind_text(insertTranscript, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(insertTranscript, 2, mtime);
                sqlite3_bind_text(insertTranscript, 3, audioName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertTranscript, 4, metadata.anchorTime.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertTranscript, 5, metadata.anchorTimeSource.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(insertTranscript, 6, metadata.anchorTimeAssumed.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(insertTranscript, 7, metadata.anchorTimeUncertain ? 1 : 0);
                sqlite3_step(insertTranscript);
                sqlite3_finalize(insertTranscript);

                const sqlite3_int64 transcriptId = sqlite3_last_insert_rowid(database);
                sqlite3_stmt* insertSegment = nullptr;
                if (sqlite3_prepare_v2(database,
                        "INSERT INTO segments (transcript_id, start_time, end_time, text) VALUES (?, ?, ?, ?);",
                        -1, &insertSegment, nullptr) == SQLITE_OK) {
                    for (const auto& segment : data["segments"]) {
                        const double start = segment.value("start", 0.0);
                        const double end = segment.value("end", 0.0);
                        const std::string text = segment.value("text", "");
                        sqlite3_bind_int64(insertSegment, 1, transcriptId);
                        sqlite3_bind_double(insertSegment, 2, start);
                        sqlite3_bind_double(insertSegment, 3, end);
                        sqlite3_bind_text(insertSegment, 4, text.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(insertSegment);
                        sqlite3_reset(insertSegment);
                    }
                }
                sqlite3_finalize(insertSegment);
            } catch (...) {
                // Ignore one malformed transcript and continue indexing the directory.
            }
        }

        for (const auto& file : databaseFiles) {
            if (activeFiles.find(file.first) == activeFiles.end()) {
                sqlite3_stmt* remove = nullptr;
                if (sqlite3_prepare_v2(database,
                        "DELETE FROM transcripts WHERE id = ? AND (input IS NULL OR input = '');",
                        -1, &remove, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(remove, 1, file.second.id);
                    sqlite3_step(remove);
                }
                sqlite3_finalize(remove);
            }
        }

        ExecSQL(database, "COMMIT TRANSACTION;");
        if (rebuildFts && ExecSQL(database, "INSERT INTO segments_fts(segments_fts) VALUES ('rebuild');"))
            ExecSQL(database, "INSERT INTO segments_fts(segments_fts) VALUES ('optimize');");
        if (backfillMetadata) ExecSQL(database, "PRAGMA user_version = 3;");
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
        UpdateDatabase(database, transcriptDir);
        sqlite3_close(database);
    } catch (const std::exception& error) {
        std::cerr << "Database sync failed: " << error.what() << std::endl;
    } catch (...) {
        std::cerr << "Database sync failed with unknown error" << std::endl;
    }
}
