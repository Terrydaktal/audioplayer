#include "audio_process.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace audio {
namespace {

std::string ExecCommand(const char* command) {
    std::array<char, 128> buffer{};
    std::string result;
    struct PipeCloser {
        void operator()(FILE* file) const {
            if (file) pclose(file);
        }
    };

    std::unique_ptr<FILE, PipeCloser> pipe(popen(command, "r"));
    if (!pipe) return {};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    const auto last = result.find_last_not_of(" \n\r\t");
    if (last == std::string::npos) return {};
    result.erase(last + 1);
    return result;
}

} // namespace

bool RunCommandCancellable(const std::vector<std::string>& args,
                           std::atomic<int>& currentVersion,
                           int targetVersion) {
    if (args.empty()) return false;

    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        const int fd = open("/dev/null", O_RDWR);
        if (fd != -1) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }

        std::vector<char*> cArgs;
        cArgs.reserve(args.size() + 1);
        for (const auto& arg : args) cArgs.push_back(const_cast<char*>(arg.c_str()));
        cArgs.push_back(nullptr);
        execvp(cArgs[0], cArgs.data());
        _exit(1);
    }

    while (true) {
        int status = 0;
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result < 0) return false;
        if (result > 0) return WIFEXITED(status) && WEXITSTATUS(status) == 0;

        if (currentVersion.load() != targetVersion) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void ProbeToInfo(const std::string& path, MediaInfo& out) {
    const std::string command =
        "ffprobe -v error -probesize 32k -analyzeduration 0 "
        "-show_entries stream=codec_name:format=format_name "
        "-of default=noprint_wrappers=1:nokey=1 \"" + path + "\"";

    const std::string output = ExecCommand(command.c_str());
    std::stringstream stream(output);
    std::string codec;
    std::string container;
    if (!std::getline(stream, codec) || !std::getline(stream, container)) return;

    out.codec = codec;
    const auto comma = container.find(',');
    const std::string containerId = comma == std::string::npos
        ? container
        : container.substr(0, comma);
    static const std::map<std::string, std::string> containerNames = {
        {"mov", "MPEG-4 (M4A)"}, {"mp3", "MP3"}, {"wav", "WAV"},
        {"flac", "FLAC"}, {"ogg", "Ogg Vorbis"}, {"matroska", "MKV/WebM"}
    };
    const auto name = containerNames.find(containerId);
    out.container = name == containerNames.end() ? containerId : name->second;

    if (codec == "aac" || containerId == "mov") out.recommendedExt = ".m4a";
    else if (codec == "mp3") out.recommendedExt = ".mp3";
    else if (codec == "flac") out.recommendedExt = ".flac";
    else if (codec == "wav" || codec.find("pcm") != std::string::npos) out.recommendedExt = ".wav";
    else out.recommendedExt = "." + containerId;
    out.isReady = true;
}

bool IsNativelySupported(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    unsigned char header[4] = {0};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    return std::memcmp(header, "ID3", 3) == 0 ||
           (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) ||
           std::memcmp(header, "RIFF", 4) == 0 ||
           std::memcmp(header, "fLaC", 4) == 0;
}

} // namespace audio
