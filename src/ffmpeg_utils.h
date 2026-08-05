#ifndef FFMPEG_UTILS_H
#define FFMPEG_UTILS_H

#include "audio/crop_service.h"

// Compatibility wrapper for older callers. New code should use audio::CropAudio.
inline bool CropAudio(const std::string& input,
                      const std::string& output,
                      double start,
                      double end,
                      const std::string& realInputExt = {}) {
    return audio::CropAudio(input, output, start, end, realInputExt);
}

#endif
