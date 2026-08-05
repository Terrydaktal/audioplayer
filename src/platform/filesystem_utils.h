#ifndef FILESYSTEM_UTILS_H
#define FILESYSTEM_UTILS_H

#include <filesystem>
#include <string>
#include <vector>

std::vector<std::string> ScanAudioFolder(const std::string& folder);
bool IsAvailableFile(const std::filesystem::path& path);
std::string FormatFileMTime(const std::filesystem::path& path);

#endif
