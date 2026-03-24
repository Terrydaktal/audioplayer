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
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

struct AudioPlayer::InternalData {
    ma_engine engine;
    ma_sound* pSound = nullptr;
    bool soundInitialized = false;
};

AudioPlayer::AudioPlayer() : data(new InternalData()) {
    ma_result result = ma_engine_init(NULL, &data->engine);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to initialize audio engine" << std::endl;
    }
}

AudioPlayer::~AudioPlayer() {
    currentVersion++; 
    if (loadThread.joinable()) loadThread.join();
    
    std::lock_guard<std::mutex> lock(stateMutex);
    InternalUnload();
    ma_engine_uninit(&data->engine);
    delete data;
}

std::string ExecCmd(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    struct PipeCloser { void operator()(FILE* f) { if (f) pclose(f); } };
    std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd, "r"));
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    result.erase(result.find_last_not_of(" \n\r\t") + 1);
    return result;
}

void ProbeToInfo(const std::string& path, MediaInfo& out) {
    std::string cmd = "ffprobe -v error -probesize 32k -analyzeduration 0 "
                      "-show_entries stream=codec_name:format=format_name "
                      "-of default=noprint_wrappers=1:nokey=1 \"" + path + "\"";
    
    std::string output = ExecCmd(cmd.c_str());
    std::stringstream ss(output);
    std::string rawCodec, rawContainer;
    
    if (std::getline(ss, rawCodec) && std::getline(ss, rawContainer)) {
        out.codec = rawCodec;
        size_t comma = rawContainer.find(',');
        std::string containerId = (comma != std::string::npos) ? rawContainer.substr(0, comma) : rawContainer;

        static std::map<std::string, std::string> containerMap = {
            {"mov", "MPEG-4 (M4A)"}, {"mp3", "MP3"}, {"wav", "WAV"}, 
            {"flac", "FLAC"}, {"ogg", "Ogg Vorbis"}, {"matroska", "MKV/WebM"}
        };

        if (containerMap.count(containerId)) out.container = containerMap[containerId];
        else out.container = containerId;

        if (out.codec == "aac" || containerId == "mov") out.recommendedExt = ".m4a";
        else if (out.codec == "mp3") out.recommendedExt = ".mp3";
        else if (out.codec == "flac") out.recommendedExt = ".flac";
        else if (out.codec == "wav" || out.codec.find("pcm") != std::string::npos) out.recommendedExt = ".wav";
        else out.recommendedExt = "." + containerId;
        out.isReady = true;
    }
}

bool IsNativelySupported(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    unsigned char b[4] = {0};
    f.read((char*)b, 4);
    if (memcmp(b, "ID3", 3) == 0) return true;
    if (b[0] == 0xFF && (b[1] & 0xE0) == 0xE0) return true;
    if (memcmp(b, "RIFF", 4) == 0) return true;
    if (memcmp(b, "fLaC", 4) == 0) return true;
    return false;
}

bool AudioPlayer::Load(const std::string& filePath) {
    int version = ++currentVersion;
    isLoading = true;

    if (loadThread.joinable()) loadThread.join();

    loadThread = std::thread([this, filePath, version]() {
        ma_sound* pLocalSound = new ma_sound();
        bool localInitialized = false;
        std::string localTempPath = "";

        if (IsNativelySupported(filePath)) {
            if (ma_sound_init_from_file(&data->engine, filePath.c_str(), 0, NULL, NULL, pLocalSound) == MA_SUCCESS) {
                localInitialized = true;
            }
        }

        if (!localInitialized && this->currentVersion == version) {
            std::string baseTemp = fs::exists("/dev/shm") ? "/dev/shm" : "/tmp";
            localTempPath = baseTemp + "/audioplayer_v" + std::to_string(version) + "_" + std::to_string(rand() % 1000) + ".wav";
            std::string cmd = "ffmpeg -y -i \"" + filePath + "\" -vn -acodec pcm_s16le -ar 44100 -ac 2 -threads 0 \"" + localTempPath + "\" 2>/dev/null";
            
            if (std::system(cmd.c_str()) == 0 && this->currentVersion == version) {
                if (ma_sound_init_from_file(&data->engine, localTempPath.c_str(), 0, NULL, NULL, pLocalSound) == MA_SUCCESS) {
                    localInitialized = true;
                }
            }
        }

        if (this->currentVersion == version) {
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                this->InternalUnload();
                if (localInitialized) {
                    this->data->pSound = pLocalSound;
                    this->data->soundInitialized = true;
                    this->loadedPath = filePath;
                    this->tempFilePath = localTempPath;
                    this->mediaInfo = MediaInfo(); 
                } else {
                    delete pLocalSound;
                }
                this->isLoading = false; 
            }

            if (localInitialized && this->currentVersion == version) {
                MediaInfo meta;
                ProbeToInfo(filePath, meta);
                if (this->currentVersion == version) {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    this->mediaInfo = meta;
                }
            }
        } else {
            if (localInitialized) { ma_sound_uninit(pLocalSound); delete pLocalSound; }
            else { delete pLocalSound; }
            if (!localTempPath.empty()) fs::remove(localTempPath);
        }
    });

    return true;
}

MediaInfo AudioPlayer::GetMediaInfo() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return mediaInfo;
}

void AudioPlayer::InternalUnload() {
    if (data->soundInitialized && data->pSound) {
        ma_sound_uninit(data->pSound);
        delete data->pSound;
        data->pSound = nullptr;
        data->soundInitialized = false;
        loadedPath = "";
    }
    if (!tempFilePath.empty()) {
        std::remove(tempFilePath.c_str());
        tempFilePath = "";
    }
}

void AudioPlayer::Play() {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (data->soundInitialized && data->pSound) ma_sound_start(data->pSound);
}

void AudioPlayer::Pause() {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (data->soundInitialized && data->pSound) ma_sound_stop(data->pSound);
}

void AudioPlayer::Seek(double seconds) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (data->soundInitialized && data->pSound) {
        ma_uint64 cursor = (ma_uint64)(seconds * ma_engine_get_sample_rate(&data->engine));
        ma_sound_seek_to_pcm_frame(data->pSound, cursor);
    }
}

double AudioPlayer::GetPosition() const {
    std::lock_guard<std::mutex> lock(const_cast<AudioPlayer*>(this)->stateMutex);
    if (data->soundInitialized && data->pSound) {
        ma_uint64 cursor;
        ma_sound_get_cursor_in_pcm_frames(data->pSound, &cursor);
        return (double)cursor / ma_engine_get_sample_rate(&data->engine);
    }
    return 0.0;
}

double AudioPlayer::GetDuration() const {
    std::lock_guard<std::mutex> lock(const_cast<AudioPlayer*>(this)->stateMutex);
    if (data->soundInitialized && data->pSound) {
        ma_uint64 length;
        ma_sound_get_length_in_pcm_frames(data->pSound, &length);
        return (double)length / ma_engine_get_sample_rate(&data->engine);
    }
    return 0.0;
}

bool AudioPlayer::IsPlaying() const {
    std::lock_guard<std::mutex> lock(const_cast<AudioPlayer*>(this)->stateMutex);
    return data->soundInitialized && data->pSound && ma_sound_is_playing(data->pSound);
}
