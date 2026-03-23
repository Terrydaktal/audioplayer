#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio_player.h"
#include <iostream>
#include <filesystem>
#include <cstdio>
#include <string>
#include <ctime>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <array>
#include <memory>
#include <sstream>
#include <map>

namespace fs = std::filesystem;

struct AudioPlayer::InternalData {
    ma_engine engine;
    ma_sound sound;
    bool soundInitialized = false;
};

AudioPlayer::AudioPlayer() : data(new InternalData()) {
    ma_result result = ma_engine_init(NULL, &data->engine);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
    }
}

AudioPlayer::~AudioPlayer() {
    Unload();
    if (loadThread.joinable()) loadThread.join();
    ma_engine_uninit(&data->engine);
    delete data;
}

std::string ExecCmd(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    result.erase(result.find_last_not_of(" \n\r\t") + 1);
    return result;
}

MediaInfo ProbeMedia(const std::string& path) {
    MediaInfo info;
    
    // Optimized: Get codec and format in a single call with limited probing
    // Output format: codec_name\nformat_name
    std::string cmd = "ffprobe -v error -probesize 32k -analyzeduration 0 -select_streams a:0 "
                      "-show_entries stream=codec_name:format=format_name "
                      "-of default=noprint_wrappers=1:nokey=1 \"" + path + "\"";
    
    std::string output = ExecCmd(cmd.c_str());
    std::stringstream ss(output);
    std::string rawCodec, rawContainer;
    
    if (std::getline(ss, rawCodec) && std::getline(ss, rawContainer)) {
        info.codec = rawCodec;
        
        size_t comma = rawContainer.find(',');
        std::string containerId = (comma != std::string::npos) ? rawContainer.substr(0, comma) : rawContainer;

        static std::map<std::string, std::string> containerMap = {
            {"mov", "MPEG-4 (M4A)"}, {"mp3", "MP3"}, {"wav", "WAV"}, 
            {"flac", "FLAC"}, {"ogg", "Ogg Vorbis"}, {"matroska", "MKV/WebM"}
        };

        if (containerMap.count(containerId)) info.container = containerMap[containerId];
        else info.container = containerId;

        // Selection extension based on real data
        if (info.codec == "aac" || containerId == "mov") info.recommendedExt = ".m4a";
        else if (info.codec == "mp3") info.recommendedExt = ".mp3";
        else if (info.codec == "flac") info.recommendedExt = ".flac";
        else if (info.codec == "wav" || info.codec.find("pcm") != std::string::npos) info.recommendedExt = ".wav";
        else info.recommendedExt = "." + containerId;
    } else {
        info.container = "Unknown";
        info.codec = "Unknown";
        info.recommendedExt = ".mp3";
    }

    return info;
}

bool AudioPlayer::Load(const std::string& filePath) {
    if (isLoading) return false;
    Unload();
    if (loadThread.joinable()) loadThread.join();

    isLoading = true;
    loadThread = std::thread([this, filePath]() {
        // 1. Fast Probe
        mediaInfo = ProbeMedia(filePath);

        // 2. Transcode (Fast WAV)
        std::string tmpFile = "/tmp/audioplayer_fix_" + std::to_string(std::time(0)) + "_" + std::to_string(rand() % 1000) + ".wav";
        
        // Added -threads 0 for multi-threaded encoding if applicable
        std::string cmd = "ffmpeg -y -i \"" + filePath + "\" -vn -acodec pcm_s16le -ar 44100 -ac 2 -threads 0 \"" + tmpFile + "\" 2>/dev/null";
        
        ma_result result = MA_ERROR;
        if (std::system(cmd.c_str()) == 0 && fs::exists(tmpFile)) {
            tempFilePath = tmpFile;
            result = ma_sound_init_from_file(&data->engine, tempFilePath.c_str(), 0, NULL, NULL, &data->sound);
        }

        if (result == MA_SUCCESS) {
            data->soundInitialized = true;
            loadedPath = filePath;
        } else {
            std::cerr << "Async failed to load: " << filePath << std::endl;
        }
        isLoading = false;
    });

    return true;
}

void AudioPlayer::Unload() {
    if (data->soundInitialized) {
        ma_sound_uninit(&data->sound);
        data->soundInitialized = false;
        loadedPath = "";
    }
    if (!tempFilePath.empty()) {
        std::remove(tempFilePath.c_str());
        tempFilePath = "";
    }
}

void AudioPlayer::Play() { if (data->soundInitialized) ma_sound_start(&data->sound); }
void AudioPlayer::Pause() { if (data->soundInitialized) ma_sound_stop(&data->sound); }
void AudioPlayer::Seek(double seconds) {
    if (data->soundInitialized) {
        ma_uint64 cursor = (ma_uint64)(seconds * ma_engine_get_sample_rate(&data->engine));
        ma_sound_seek_to_pcm_frame(&data->sound, cursor);
    }
}

double AudioPlayer::GetPosition() const {
    if (data->soundInitialized) {
        ma_uint64 cursor;
        ma_sound_get_cursor_in_pcm_frames(&data->sound, &cursor);
        return (double)cursor / ma_engine_get_sample_rate(&data->engine);
    }
    return 0.0;
}

double AudioPlayer::GetDuration() const {
    if (data->soundInitialized) {
        ma_uint64 length;
        ma_sound_get_length_in_pcm_frames(&data->sound, &length);
        return (double)length / ma_engine_get_sample_rate(&data->engine);
    }
    return 0.0;
}

bool AudioPlayer::IsPlaying() const { return data->soundInitialized && ma_sound_is_playing(&data->sound); }
