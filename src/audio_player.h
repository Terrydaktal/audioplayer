#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <string>
#include <atomic>
#include <thread>
#include <mutex>

struct MediaInfo {
    std::string container = "Probing...";
    std::string codec = "Probing...";
    std::string recommendedExt = "";
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
    void InternalUnload(); // Thread-safe internal cleanup

    struct InternalData;
    InternalData* data;
    std::string loadedPath;
    std::string tempFilePath;
    
    MediaInfo mediaInfo;
    std::mutex stateMutex; // Protects sound, path, and mediaInfo
    
    std::atomic<bool> isLoading{false};
    std::atomic<int> currentVersion{0};
    std::thread loadThread;
};

#endif
