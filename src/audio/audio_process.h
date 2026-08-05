#ifndef AUDIO_PROCESS_H
#define AUDIO_PROCESS_H

#include "audio/audio_player.h"

#include <atomic>
#include <string>
#include <vector>

namespace audio {

bool RunCommandCancellable(const std::vector<std::string>& args,
                           std::atomic<int>& currentVersion,
                           int targetVersion);
void ProbeToInfo(const std::string& path, MediaInfo& out);
bool IsNativelySupported(const std::string& path);

} // namespace audio

#endif
