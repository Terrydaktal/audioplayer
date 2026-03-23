#ifndef FFMPEG_UTILS_H
#define FFMPEG_UTILS_H

#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

inline bool CropAudio(const std::string& input, const std::string& output, double start, double end, const std::string& realInputExt = "") {
    if (start >= end) return false;
    
    // Get the extension of the output file
    std::string outExt = fs::path(output).extension().string();
    std::transform(outExt.begin(), outExt.end(), outExt.begin(), ::tolower);

    // Use the real format hint if provided, otherwise fall back to disk extension
    std::string effectiveInExt = realInputExt;
    if (effectiveInExt.empty()) {
        effectiveInExt = fs::path(input).extension().string();
    }
    std::transform(effectiveInExt.begin(), effectiveInExt.end(), effectiveInExt.begin(), ::tolower);

    // If real formats match, we can do a lossless copy
    bool canCopy = (effectiveInExt == outExt);

    std::stringstream cmd;
    cmd << "ffmpeg -y -ss " << std::fixed << std::setprecision(3) << start 
        << " -to " << std::fixed << std::setprecision(3) << end 
        << " -i \"" << input << "\" ";
    
    if (canCopy) {
        cmd << "-acodec copy ";
    } else {
        if (outExt == ".mp3") cmd << "-acodec libmp3lame -q:a 2 ";
        else if (outExt == ".m4a" || outExt == ".mp4") cmd << "-acodec aac -b:a 192k ";
        else if (outExt == ".wav") cmd << "-acodec pcm_s16le ";
    }

    cmd << "\"" << output << "\"";
    
    std::string command = cmd.str();
    std::cout << "Executing Crop: " << command << " (Copy: " << (canCopy ? "Yes" : "No") << ")" << std::endl;
    int result = std::system(command.c_str());
    return result == 0;
}

#endif
