#include "platform/filesystem_utils.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <sys/stat.h>

namespace fs = std::filesystem;

std::vector<std::string> ScanAudioFolder(const std::string& folder) {
    std::vector<std::string> files;
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        return files;
    }

    const std::vector<std::string> extensions = {
        ".mp3", ".wav", ".m4a", ".mp4", ".aac", ".flac",
    };
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end()) {
            files.push_back(entry.path().filename().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool IsAvailableFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

std::string FormatFileMTime(const fs::path& path) {
    struct stat fileStat {};
    if (stat(path.c_str(), &fileStat) != 0) {
        return "mtime unavailable";
    }

    std::tm localTime {};
    if (localtime_r(&fileStat.st_mtime, &localTime) == nullptr) {
        return "mtime unavailable";
    }

    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime) == 0) {
        return "mtime unavailable";
    }
    return buffer;
}
