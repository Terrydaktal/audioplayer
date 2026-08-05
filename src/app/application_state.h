#ifndef APPLICATION_STATE_H
#define APPLICATION_STATE_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/audio_player.h"
#include "platform/font_manager.h"
#include "transcripts/transcript.h"

struct ImFont;

struct ApplicationState {
    AudioPlayer player;
    TranscriptHandler transcript;
    std::mutex transcriptMutex;
    std::atomic<bool> transcriptLoading{false};
    std::thread transcriptLoadThread;

    std::vector<GlobalSearchResult> globalSearchResults;
    std::mutex globalSearchMutex;
    std::atomic<bool> globalSearchRunning{false};
    std::thread globalSearchThread;
    std::atomic<bool> databaseSyncing{false};
    std::thread databaseSyncThread;

    std::vector<FontInfo> availableFonts;
    std::string currentFontPath;
    float fontSize = 20.0f;
    bool fontNeedsUpdate = true;
    ImFont* mainFont = nullptr;

    char audioFolder[1024] = "/media/lewis/1b/Diary Recordings";
    char transcriptDir[1024] = "/media/lewis/1b/Transcripts";
    char fileFilter[256] = "";
    std::vector<std::string> folderFiles;
    std::string currentLoadedFile;
    bool currentAudioAvailable = false;

    char globalSearchQuery[256] = "";
    double pendingSeekTime = -1.0;
    int activeTab = 0;
    bool requestTabSwitch = false;

    char cropOutputPath[1024] = "cropped_output.mp3";
    bool cropPathNeedsUpdate = false;
    float cropStart = 0.0f;
    float cropEnd = 0.0f;
    char cropStartBuffer[64] = "00:00.000";
    char cropEndBuffer[64] = "00:00.000";

    char transcriptSearch[256] = "";
    std::vector<int> transcriptMatches;
    int currentMatch = -1;
    int lastMatch = -1;
    bool showSearch = false;
    bool searchFocusPending = false;
    int selectionStart = -1;
    int selectionEnd = -1;
    bool wasLoading = false;
};

#endif
