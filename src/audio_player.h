#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <string>
#include <atomic>
#include <thread>

struct MediaInfo {
    std::string container;
    std::string codec;
    std::string recommendedExt;
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
    const MediaInfo& GetMediaInfo() const { return mediaInfo; }

private:
    void Unload();

    struct InternalData;
    InternalData* data;
    std::string loadedPath;
    std::string tempFilePath;
    MediaInfo mediaInfo;
    std::atomic<bool> isLoading{false};
    std::thread loadThread;
};

#endif
