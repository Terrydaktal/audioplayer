#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio/audio_player.h"
#include "audio/audio_process.h"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct AudioPlayer::InternalData {
    ma_engine engine;
    ma_sound* pSound = nullptr;
    bool soundInitialized = false;
};

AudioPlayer::AudioPlayer() : data(new InternalData()) {
    if (ma_engine_init(nullptr, &data->engine) != MA_SUCCESS)
        std::cerr << "Failed to initialize audio engine" << std::endl;
    workerThread = std::thread(&AudioPlayer::WorkerLoop, this);
}

AudioPlayer::~AudioPlayer() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        workerStop = true;
    }
    queueCV.notify_one();
    if (workerThread.joinable()) workerThread.join();

    std::lock_guard<std::mutex> lock(stateMutex);
    InternalUnload();
    ma_engine_uninit(&data->engine);
    delete data;
}

bool AudioPlayer::Load(const std::string& filePath) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pendingFilePath = filePath;
        hasPendingTask = true;
        isLoading = true;
        ++currentVersion;
    }
    queueCV.notify_one();
    return true;
}

void AudioPlayer::WorkerLoop() {
    while (true) {
        std::string filePath;
        int version = 0;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCV.wait(lock, [this] { return workerStop || hasPendingTask; });
            if (workerStop) break;
            filePath = pendingFilePath;
            hasPendingTask = false;
            version = currentVersion;
        }

        ma_sound* localSound = new ma_sound();
        bool initialized = false;
        std::string temporaryPath;
        if (audio::IsNativelySupported(filePath)) {
            initialized = ma_sound_init_from_file(&data->engine, filePath.c_str(), 0, nullptr, nullptr, localSound) == MA_SUCCESS;
        }
        if (!initialized && currentVersion == version) {
            const std::string base = fs::exists("/dev/shm") ? "/dev/shm" : "/tmp";
            temporaryPath = base + "/audioplayer_v" + std::to_string(version) + "_" +
                            std::to_string(std::rand() % 1000) + ".wav";
            const std::vector<std::string> arguments = {
                "ffmpeg", "-y", "-i", filePath, "-vn", "-acodec", "pcm_s16le",
                "-ar", "44100", "-ac", "2", "-threads", "0", temporaryPath
            };
            if (audio::RunCommandCancellable(arguments, currentVersion, version) && currentVersion == version)
                initialized = ma_sound_init_from_file(&data->engine, temporaryPath.c_str(), 0, nullptr, nullptr, localSound) == MA_SUCCESS;
        }

        if (currentVersion == version) {
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                InternalUnload();
                if (initialized) {
                    data->pSound = localSound;
                    data->soundInitialized = true;
                    loadedPath = filePath;
                    tempFilePath = temporaryPath;
                    mediaInfo = MediaInfo();
                    ma_uint64 length = 0;
                    cachedDuration = ma_sound_get_length_in_pcm_frames(localSound, &length) == MA_SUCCESS
                        ? static_cast<double>(length) / ma_engine_get_sample_rate(&data->engine)
                        : 0.0;
                    cachedPosition = 0.0;
                    lastPositionQueryTime = std::chrono::steady_clock::now();
                } else {
                    delete localSound;
                }
                isLoading = false;
            }
            if (initialized && currentVersion == version) {
                MediaInfo metadata;
                audio::ProbeToInfo(filePath, metadata);
                if (currentVersion == version) {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    mediaInfo = metadata;
                }
            }
        } else {
            if (initialized) ma_sound_uninit(localSound);
            delete localSound;
            if (!temporaryPath.empty()) fs::remove(temporaryPath);
            if (currentVersion == version) isLoading = false;
        }
    }
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
        loadedPath.clear();
    }
    if (!tempFilePath.empty()) {
        std::remove(tempFilePath.c_str());
        tempFilePath.clear();
    }
    cachedDuration = 0.0;
    cachedPosition = 0.0;
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
        ma_sound_seek_to_pcm_frame(data->pSound,
                                   static_cast<ma_uint64>(seconds * ma_engine_get_sample_rate(&data->engine)));
        cachedPosition = seconds;
        lastPositionQueryTime = std::chrono::steady_clock::now();
    }
}

double AudioPlayer::GetPosition() const {
    std::lock_guard<std::mutex> lock(const_cast<AudioPlayer*>(this)->stateMutex);
    if (!data->soundInitialized || !data->pSound) return 0.0;
    const auto now = std::chrono::steady_clock::now();
    const bool playing = ma_sound_is_playing(data->pSound);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPositionQueryTime).count();
    if (elapsed > 100) {
        ma_uint64 cursor = 0;
        ma_sound_get_cursor_in_pcm_frames(data->pSound, &cursor);
        cachedPosition = static_cast<double>(cursor) / ma_engine_get_sample_rate(&data->engine);
        lastPositionQueryTime = now;
    } else if (playing) {
        return cachedPosition + static_cast<double>(elapsed) / 1000.0;
    }
    return cachedPosition;
}

double AudioPlayer::GetDuration() const {
    std::lock_guard<std::mutex> lock(const_cast<AudioPlayer*>(this)->stateMutex);
    return cachedDuration;
}

bool AudioPlayer::IsPlaying() const {
    std::lock_guard<std::mutex> lock(const_cast<AudioPlayer*>(this)->stateMutex);
    return data->soundInitialized && data->pSound && ma_sound_is_playing(data->pSound);
}
