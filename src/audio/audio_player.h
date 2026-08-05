#ifndef AUDIO_AUDIO_PLAYER_H
#define AUDIO_AUDIO_PLAYER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

struct MediaInfo {
    std::string container = "Probing...";
    std::string codec = "Probing...";
    std::string recommendedExt;
    bool isReady = false;
};

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool Load(const std::string& filePath);
    void Play();
    void Pause();
    void Seek(double seconds);

    double GetPosition() const;
    double GetDuration() const;
    bool IsPlaying() const;
    bool IsLoading() const { return isLoading; }

    const std::string& GetLoadedPath() const { return loadedPath; }
    MediaInfo GetMediaInfo();

private:
    void InternalUnload();
    void WorkerLoop();

    struct InternalData;
    InternalData* data;
    std::string loadedPath;
    std::string tempFilePath;
    MediaInfo mediaInfo;
    std::mutex stateMutex;
    std::atomic<bool> isLoading{false};
    std::atomic<int> currentVersion{0};
    std::thread workerThread;
    std::mutex queueMutex;
    std::condition_variable queueCV;
    std::string pendingFilePath;
    bool workerStop = false;
    bool hasPendingTask = false;
    double cachedDuration = 0.0;
    mutable double cachedPosition = 0.0;
    mutable std::chrono::steady_clock::time_point lastPositionQueryTime;
};

#endif
