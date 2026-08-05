#ifndef CROP_SERVICE_H
#define CROP_SERVICE_H

#include <string>

namespace audio {

bool CropAudio(const std::string& input,
               const std::string& output,
               double start,
               double end,
               const std::string& realInputExtension = {});

} // namespace audio

#endif
