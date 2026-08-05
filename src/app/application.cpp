#include "app/application.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <thread>

#include <SDL.h>
#include <unistd.h>

#include "app/application_state.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "platform/filesystem_utils.h"
#include "platform/font_manager.h"
#include "transcript.h"
#include "ui/ui_context.h"

namespace fs = std::filesystem;

Application::Application() = default;

Application::~Application() {
    JoinBackgroundTasks();
}

bool Application::DetachFromTerminal() {
    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Failed to fork: " << std::strerror(errno) << std::endl;
        return false;
    }
    if (pid > 0) return false;
    if (setsid() < 0) {
        std::cerr << "Failed to setsid: " << std::strerror(errno) << std::endl;
        return false;
    }

    const int descriptor = open("/dev/null", O_RDWR);
    if (descriptor != -1) {
        dup2(descriptor, STDIN_FILENO);
        dup2(descriptor, STDOUT_FILENO);
        dup2(descriptor, STDERR_FILENO);
        if (descriptor > STDERR_FILENO) close(descriptor);
    }
    return true;
}

void Application::InitializeState() {
    state_ = std::make_unique<ApplicationState>();
    state_->availableFonts = ScanFonts();
    state_->folderFiles = ScanAudioFolder(state_->audioFolder);
}

void Application::JoinBackgroundTasks() {
    if (!state_) return;
    if (state_->transcriptLoadThread.joinable()) state_->transcriptLoadThread.join();
    if (state_->globalSearchThread.joinable()) state_->globalSearchThread.join();
    if (state_->databaseSyncThread.joinable()) state_->databaseSyncThread.join();
}

void Application::StartTranscriptLoad(const std::string& audioPath,
                                      const std::string& transcriptDirectory,
                                      const std::string& transcriptPath,
    const std::string& databasePath,
                                      const std::string& databaseKey) {
    auto& state = *state_;
    if (state.transcriptLoadThread.joinable()) state.transcriptLoadThread.join();
    state.transcriptLoading = true;
    {
        std::lock_guard<std::mutex> lock(state.transcriptMutex);
        state.transcript.Clear();
    }
    state.transcriptLoadThread = std::thread([this, audioPath, transcriptDirectory, transcriptPath,
                                               databasePath, databaseKey]() {
        auto& current = *state_;
        std::lock_guard<std::mutex> lock(current.transcriptMutex);
        if (transcriptPath.empty()) {
            if (!databasePath.empty()) current.transcript.LoadTranscriptDatabase(databasePath, databaseKey);
            else current.transcript.Load(audioPath, transcriptDirectory);
        } else if (!current.transcript.LoadTranscriptFile(transcriptPath) && !databasePath.empty()) {
            current.transcript.LoadTranscriptDatabase(databasePath, databaseKey);
        }
        current.transcriptLoading = false;
    });
}

void Application::StartDatabaseSync() {
    auto& state = *state_;
    if (state.databaseSyncThread.joinable()) state.databaseSyncThread.join();
    state.databaseSyncing = true;
    const std::string directory = state.transcriptDir;
    state.databaseSyncThread = std::thread([this, directory]() {
        SyncTranscriptDatabase(directory);
        state_->databaseSyncing = false;
    });
}

void Application::StartGlobalSearch(const std::string& query) {
    auto& state = *state_;
    if (state.globalSearchThread.joinable()) state.globalSearchThread.join();
    state.globalSearchRunning = true;
    const std::string directory = state.transcriptDir;
    state.globalSearchThread = std::thread([this, directory, query]() {
        auto results = SearchAllTranscripts(directory, query);
        {
            std::lock_guard<std::mutex> lock(state_->globalSearchMutex);
            state_->globalSearchResults = std::move(results);
        }
        state_->globalSearchRunning = false;
    });
}

void Application::SelectAudioFile(const std::string& filename) {
    auto& state = *state_;
    const fs::path fullPath = fs::path(state.audioFolder) / filename;
    state.currentLoadedFile = filename;
    state.currentAudioAvailable = IsAvailableFile(fullPath);
    state.player.Load(fullPath.string());
    state.cropPathNeedsUpdate = true;
    state.pendingSeekTime = -1.0;
    state.transcriptMatches.clear();
    state.currentMatch = -1;
    state.lastMatch = -1;
    state.selectionStart = -1;
    state.selectionEnd = -1;
    StartTranscriptLoad(fullPath.string(), state.transcriptDir);
}

void Application::SelectSearchResult(const GlobalSearchResult& result) {
    auto& state = *state_;
    const fs::path fullPath = fs::path(state.audioFolder) / result.audioFilename;
    state.currentLoadedFile = result.audioFilename;
    state.currentAudioAvailable = IsAvailableFile(fullPath);
    state.player.Load(fullPath.string());
    state.cropPathNeedsUpdate = true;
    state.transcriptMatches.clear();
    state.currentMatch = -1;
    state.lastMatch = -1;
    state.selectionStart = -1;
    state.selectionEnd = -1;
    const std::string directory = state.transcriptDir;
    StartTranscriptLoad(fullPath.string(), directory,
                        (fs::path(directory) / result.transcriptFilename).string(),
                        (fs::path(directory) / "transcripts.db").string(),
                        result.transcriptFilename);
    state.pendingSeekTime = state.currentAudioAvailable ? result.start : -1.0;
    state.activeTab = 0;
    state.requestTabSwitch = true;
}

UiActions Application::BuildActions() {
    return {
        [this](const std::string& filename) { SelectAudioFile(filename); },
        [this](const GlobalSearchResult& result) { SelectSearchResult(result); },
        [this]() { StartDatabaseSync(); },
        [this](const std::string& query) { StartGlobalSearch(query); },
    };
}

int Application::Run(bool daemonize) {
    if (daemonize && !DetachFromTerminal()) return 0;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    const SDL_WindowFlags windowFlags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Interactive Audio Player", SDL_WINDOWPOS_CENTERED,
                                         SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
    if (!window) {
        SDL_Quit();
        return -1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    InitializeState();
    // The existing database is immediately usable. Synchronization is explicit
    // from the Global Search view so startup never waits on a large transcript
    // directory or SQLite WAL checkpoint.
    const UiActions actions = BuildActions();
    UiContext context{*state_, io};
    bool done = false;
    while (!done) {
        const Uint32 frameStart = SDL_GetTicks();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            ui::HandleEvent(context, event);
        }

        if (state_->fontNeedsUpdate) {
            ApplyFontChange(state_->currentFontPath, state_->fontSize, state_->mainFont);
            state_->fontNeedsUpdate = false;
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ui::Render(context, actions);
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        const Uint32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < 16) SDL_Delay(16 - frameTime);
    }

    JoinBackgroundTasks();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    state_.reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
