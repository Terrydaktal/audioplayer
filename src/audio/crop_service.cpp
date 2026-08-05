#include "audio/crop_service.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace audio {
namespace fs = std::filesystem;

bool CropAudio(const std::string& input,
               const std::string& output,
               double start,
               double end,
               const std::string& realInputExtension) {
    if (start >= end) return false;

    std::string outputExtension = fs::path(output).extension().string();
    std::transform(outputExtension.begin(), outputExtension.end(), outputExtension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string inputExtension = realInputExtension.empty()
        ? fs::path(input).extension().string()
        : realInputExtension;
    std::transform(inputExtension.begin(), inputExtension.end(), inputExtension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const bool canCopy = inputExtension == outputExtension;
    std::stringstream command;
    command << "ffmpeg -y -ss " << std::fixed << std::setprecision(3) << start
            << " -to " << std::fixed << std::setprecision(3) << end
            << " -i \"" << input << "\" ";
    if (canCopy) {
        command << "-acodec copy ";
    } else if (outputExtension == ".mp3") {
        command << "-acodec libmp3lame -q:a 2 ";
    } else if (outputExtension == ".m4a" || outputExtension == ".mp4") {
        command << "-acodec aac -b:a 192k ";
    } else if (outputExtension == ".wav") {
        command << "-acodec pcm_s16le ";
    }
    command << "\"" << output << "\"";

    std::cout << "Executing Crop: " << command.str()
              << " (Copy: " << (canCopy ? "Yes" : "No") << ")" << std::endl;
    return std::system(command.str().c_str()) == 0;
}

} // namespace audio
