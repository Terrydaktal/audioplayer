#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <SDL.h>
#include <mutex>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "audio_player.h"
#include "transcript.h"
#include "ffmpeg_utils.h"

namespace fs = std::filesystem;

struct FontInfo {
    std::string name;
    std::string path;
};

ImFont* g_MainFont = nullptr;

std::string FormatTime(double seconds) {
    int total = (int)seconds;
    int ms = (int)((seconds - total) * 1000);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    char buffer[64];
    if (h > 0) snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", h, m, s, ms);
    else snprintf(buffer, sizeof(buffer), "%02d:%02d.%03d", m, s, ms);
    return std::string(buffer);
}

double ParseTime(const std::string& str) {
    std::vector<std::string> parts;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ':')) parts.push_back(item);
    try {
        if (parts.size() == 3) return std::stod(parts[0]) * 3600 + std::stod(parts[1]) * 60 + std::stod(parts[2]);
        if (parts.size() == 2) return std::stod(parts[0]) * 60 + std::stod(parts[1]);
        if (parts.size() == 1) return std::stod(parts[0]);
    } catch (...) { return 0.0; }
    return 0.0;
}

const std::string& GetFallbackFontPath() {
    static std::vector<std::string> options = {
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    };
    for (const auto& path : options) { if (fs::exists(path)) return path; }
    static std::string empty = "";
    return empty;
}

std::vector<FontInfo> ScanFonts() {
    std::vector<FontInfo> fonts;
    fonts.push_back({"Scalable Default", ""});
    fonts.push_back({"Fixed Default (13px)", "FIXED_DEFAULT"});
    std::vector<std::string> searchPaths = {"/usr/share/fonts", "/usr/local/share/fonts"};
    char* home = getenv("HOME");
    if (home) {
        std::string userFontPath = std::string(home) + "/.local/share/fonts";
        if (fs::exists(userFontPath)) searchPaths.push_back(userFontPath);
    }
    for (const auto& base : searchPaths) {
        if (!fs::exists(base)) continue;
        try {
            for (const auto& entry : fs::recursive_directory_iterator(base)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".ttf" || ext == ".otf") fonts.push_back({entry.path().stem().string(), entry.path().string()});
                }
                if (fonts.size() > 500) break; 
            }
        } catch (...) {}
    }
    return fonts;
}

void ApplyFontChange(const std::string& path, float size) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFont* font = nullptr;
    std::string actualPath = path;
    if (actualPath.empty()) actualPath = GetFallbackFontPath();
    if (actualPath != "FIXED_DEFAULT" && !actualPath.empty()) font = io.Fonts->AddFontFromFileTTF(actualPath.c_str(), size);
    if (!font) font = io.Fonts->AddFontDefault();
    g_MainFont = font;
    io.FontDefault = g_MainFont;
    io.Fonts->Build();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    for (ImTextureData* tex : platform_io.Textures) { if (tex->Status != ImTextureStatus_OK) ImGui_ImplSDLRenderer2_UpdateTexture(tex); }
}

void UpdateSearch(const std::vector<Segment>& segments, const char* query, std::vector<int>& matches, int& current) {
    matches.clear();
    std::string q = query;
    if (q.empty()) { current = -1; return; }
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    for (int i = 0; i < (int)segments.size(); ++i) {
        std::string text = segments[i].text;
        std::transform(text.begin(), text.end(), text.begin(), ::tolower);
        if (text.find(q) != std::string::npos) matches.push_back(i);
    }
    if (!matches.empty()) { if (current < 0 || current >= (int)matches.size()) current = 0; } else current = -1;
}

std::vector<std::string> ScanAudioFolder(const std::string& folder) {
    std::vector<std::string> files;
    if (!fs::exists(folder) || !fs::is_directory(folder)) return files;
    const std::vector<std::string> exts = {".mp3", ".wav", ".m4a", ".mp4", ".aac", ".flac"};
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (std::find(exts.begin(), exts.end(), ext) != exts.end()) files.push_back(entry.path().filename().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) return -1;
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Interactive Audio Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    AudioPlayer player;
    TranscriptHandler transcript;
    std::mutex transcriptMutex;
    std::atomic<bool> transcriptLoading{false};

    std::vector<FontInfo> availableFonts = ScanFonts();
    std::string currentFontPath = ""; 
    float fontSize = 20.0f;
    bool fontNeedsUpdate = true;

    char audioFolder[1024] = "/media/lewis/1b/Diary Recordings";
    char transcriptDir[1024] = "/media/lewis/1b/Clean Transcripts";
    char fileFilter[256] = "";
    std::vector<std::string> folderFiles = ScanAudioFolder(audioFolder);
    std::string currentLoadedFile = "";

    char cropOutputPath[1024] = "cropped_output.mp3";
    bool cropPathNeedsUpdate = false;
    float cropStart = 0.0f;
    float cropEnd = 0.0f;
    char cropStartBuf[64] = "00:00.000";
    char cropEndBuf[64] = "00:00.000";

    char searchBuffer[256] = "";
    std::vector<int> matchIndices;
    int currentMatchIdx = -1;
    int lastMatchIdx = -1;
    bool showSearch = false;
    bool searchFocusPending = false;

    int selectStart = -1;
    int selectEnd = -1;

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            if (event.type == SDL_KEYDOWN) {
                bool ctrl = (io.KeyCtrl);
                if (ctrl && event.key.keysym.sym == SDLK_f) { showSearch = true; searchFocusPending = true; }
                if (event.key.keysym.sym == SDLK_ESCAPE) showSearch = false;
                if (ctrl && event.key.keysym.sym == SDLK_c && selectStart != -1 && selectEnd != -1) {
                    std::string copyText = "";
                    int start = std::min(selectStart, selectEnd);
                    int end = std::max(selectStart, selectEnd);
                    std::lock_guard<std::mutex> lock(transcriptMutex);
                    const auto& segs = transcript.GetSegments();
                    if (end < (int)segs.size()) {
                        for (int i = start; i <= end; ++i) {
                            copyText += "[" + FormatTime(segs[i].start) + " -> " + FormatTime(segs[i].end) + "] " + segs[i].text + "\n";
                        }
                        SDL_SetClipboardText(copyText.c_str());
                    }
                }
            }
        }

        if (fontNeedsUpdate) { ApplyFontChange(currentFontPath, fontSize); fontNeedsUpdate = false; }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                if (ImGui::BeginMenu("Change Font")) {
                    for (const auto& font : availableFonts) { if (ImGui::MenuItem(font.name.c_str())) { currentFontPath = font.path; fontNeedsUpdate = true; } }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::InputFloat("Font Size", &fontSize, 1.0f, 5.0f, "%.1f")) { if (fontSize < 8.0f) fontSize = 8.0f; if (fontSize > 72.0f) fontSize = 72.0f; fontNeedsUpdate = true; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) { if (ImGui::MenuItem("Find", "Ctrl+F")) { showSearch = true; searchFocusPending = true; } ImGui::EndMenu(); }
            ImGui::EndMainMenuBar();
        }

        ImGui::SetNextWindowPos(ImVec2(0, 20));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - 20));
        ImGui::Begin("Audio Player", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        if (g_MainFont) ImGui::PushFont(g_MainFont);

        ImGui::BeginChild("Sidebar", ImVec2(300, 0), true);
        ImGui::Text("Audio Folder:");
        if (ImGui::InputText("##folder", audioFolder, sizeof(audioFolder))) folderFiles = ScanAudioFolder(audioFolder);
        ImGui::Text("Transcripts:");
        ImGui::InputText("##transcripts", transcriptDir, sizeof(transcriptDir));
        ImGui::Separator();
        ImGui::Text("Filter Files:");
        ImGui::InputText("##filter", fileFilter, sizeof(fileFilter));
        ImGui::BeginChild("FileList", ImVec2(0, 0), true);
        std::string filterStr = fileFilter;
        std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);
        for (const auto& filename : folderFiles) {
            std::string lowerFile = filename;
            std::transform(lowerFile.begin(), lowerFile.end(), lowerFile.begin(), ::tolower);
            if (filterStr.empty() || lowerFile.find(filterStr) != std::string::npos) {
                if (ImGui::Selectable(filename.c_str(), currentLoadedFile == filename)) {
                    fs::path fullPath = fs::path(audioFolder) / filename;
                    if (player.Load(fullPath.string())) {
                        currentLoadedFile = filename;
                        matchIndices.clear(); currentMatchIdx = -1; lastMatchIdx = -1; cropPathNeedsUpdate = true;
                        selectStart = -1; selectEnd = -1;
                        
                        transcriptLoading = true;
                        std::string tDir = transcriptDir;
                        std::string aPath = fullPath.string();
                        std::thread([&transcript, &transcriptMutex, &transcriptLoading, aPath, tDir]() {
                            std::lock_guard<std::mutex> lock(transcriptMutex);
                            transcript.Load(aPath, tDir);
                            transcriptLoading = false;
                        }).detach();
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginGroup();
        
        if (!currentLoadedFile.empty()) {
            if (player.IsLoading() || transcriptLoading) {
                ImGui::TextColored(ImVec4(1,1,0,1), "Loading %s...", currentLoadedFile.c_str());
                ImGui::Separator();
            }

            const auto& info = player.GetMediaInfo();
            if (cropPathNeedsUpdate && !info.recommendedExt.empty()) {
                std::string stem = fs::path(currentLoadedFile).stem().string();
                snprintf(cropOutputPath, sizeof(cropOutputPath), "%s_cropped%s", stem.c_str(), info.recommendedExt.c_str());
                cropPathNeedsUpdate = false;
                cropEnd = (float)player.GetDuration();
                cropStart = 0.0f;
                snprintf(cropStartBuf, sizeof(cropStartBuf), "%s", FormatTime(cropStart).c_str());
                snprintf(cropEndBuf, sizeof(cropEndBuf), "%s", FormatTime(cropEnd).c_str());
            }

            ImGui::Text("Playing: %s [%s / %s]", currentLoadedFile.c_str(), info.container.c_str(), info.codec.c_str());
            ImGui::Separator();
            
            float bWidth = ImGui::CalcTextSize("Pause").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::Button(player.IsPlaying() ? "Pause" : "Play", ImVec2(bWidth, 0))) { if (player.IsPlaying()) player.Pause(); else player.Play(); }
            ImGui::SameLine();
            float currentPos = (float)player.GetPosition();
            float duration = (float)player.GetDuration();
            if (ImGui::SliderFloat("Position", &currentPos, 0.0f, duration, "")) player.Seek(currentPos);
            ImGui::Text("Current Time: %s / %s", FormatTime(currentPos).c_str(), FormatTime(duration).c_str());

            if (ImGui::CollapsingHeader("Cropping")) {
                ImGui::Text("Crop Start:"); ImGui::SameLine();
                if (ImGui::InputText("##cropstart", cropStartBuf, sizeof(cropStartBuf))) cropStart = (float)ParseTime(cropStartBuf);
                ImGui::SameLine();
                if (ImGui::Button("Set to Current##start")) { cropStart = currentPos; snprintf(cropStartBuf, sizeof(cropStartBuf), "%s", FormatTime(cropStart).c_str()); }
                ImGui::Text("Crop End:  "); ImGui::SameLine();
                if (ImGui::InputText("##cropend", cropEndBuf, sizeof(cropEndBuf))) cropEnd = (float)ParseTime(cropEndBuf);
                ImGui::SameLine();
                if (ImGui::Button("Set to Current##end")) { cropEnd = currentPos; snprintf(cropEndBuf, sizeof(cropEndBuf), "%s", FormatTime(cropEnd).c_str()); }
                ImGui::InputText("Output Path", cropOutputPath, sizeof(cropOutputPath));
                if (ImGui::Button("Crop & Save")) {
                    fs::path fullPath = fs::path(audioFolder) / currentLoadedFile;
                    if (CropAudio(fullPath.string(), cropOutputPath, cropStart, cropEnd, info.recommendedExt)) ImGui::OpenPopup("Crop Success");
                    else ImGui::OpenPopup("Crop Failed");
                }
                if (ImGui::BeginPopupModal("Crop Success", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Successfully cropped audio to %s", cropOutputPath);
                    if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                if (ImGui::BeginPopupModal("Crop Failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Failed to crop audio. Check console for details.");
                    if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }

            {
                std::lock_guard<std::mutex> lock(transcriptMutex);
                const auto& segments = transcript.GetSegments();
                if (!segments.empty()) {
                    ImGui::Separator();
                    if (showSearch) {
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                        ImGui::BeginChild("SearchBar", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 1.5f), true);
                        if (searchFocusPending) { ImGui::SetKeyboardFocusHere(); searchFocusPending = false; }
                        ImGui::Text("Find:"); ImGui::SameLine();
                        if (ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer))) { UpdateSearch(segments, searchBuffer, matchIndices, currentMatchIdx); lastMatchIdx = -1; }
                        ImGui::SameLine();
                        if (ImGui::Button("Prev") && !matchIndices.empty()) { currentMatchIdx = (currentMatchIdx - 1 + matchIndices.size()) % matchIndices.size(); player.Seek(segments[matchIndices[currentMatchIdx]].start); }
                        ImGui::SameLine();
                        if (ImGui::Button("Next") && !matchIndices.empty()) { currentMatchIdx = (currentMatchIdx + 1) % matchIndices.size(); player.Seek(segments[matchIndices[currentMatchIdx]].start); }
                        ImGui::SameLine();
                        if (!matchIndices.empty()) ImGui::Text("%d of %d", currentMatchIdx + 1, (int)matchIndices.size());
                        else if (strlen(searchBuffer) > 0) ImGui::TextColored(ImVec4(1,0,0,1), "No matches");
                        ImGui::SameLine(ImGui::GetWindowWidth() - 30);
                        if (ImGui::Button("X")) showSearch = false;
                        ImGui::EndChild();
                        ImGui::PopStyleColor();
                    }

                    ImGui::BeginChild("TranscriptList", ImVec2(0, 0), true);
                    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) { selectStart = -1; selectEnd = -1; }

                    for (int i = 0; i < (int)segments.size(); ++i) {
                        const auto& seg = segments[i];
                        char label[2048];
                        snprintf(label, sizeof(label), "[ %s -> %s] %s", FormatTime(seg.start).c_str(), FormatTime(seg.end).c_str(), seg.text.c_str());
                        bool isMatch = std::find(matchIndices.begin(), matchIndices.end(), i) != matchIndices.end();
                        bool isCurrMatch = (currentMatchIdx != -1 && matchIndices[currentMatchIdx] == i);
                        bool isSelected = false;
                        if (selectStart != -1 && selectEnd != -1) {
                            int s = std::min(selectStart, selectEnd);
                            int e = std::max(selectStart, selectEnd);
                            if (i >= s && i <= e) isSelected = true;
                        }
                        if (isCurrMatch) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); if (currentMatchIdx != lastMatchIdx) { ImGui::SetScrollHereY(); lastMatchIdx = currentMatchIdx; } }
                        else if (isMatch) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
                        if (isSelected) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.59f, 0.98f, 0.5f));
                        ImGui::Selectable(label, isSelected || isCurrMatch, ImGuiSelectableFlags_AllowOverlap);
                        if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()) { selectStart = i; selectEnd = i; }
                        if (ImGui::IsMouseDragging(0) && ImGui::IsItemHovered()) { selectEnd = i; }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
                            if (ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 4.0f) { player.Seek(seg.start); selectStart = -1; selectEnd = -1; }
                        }
                        if (isSelected) ImGui::PopStyleColor();
                        if (isCurrMatch || isMatch) ImGui::PopStyleColor();
                    }
                    if (ImGui::IsMouseReleased(0) && selectStart != -1 && selectStart == selectEnd) { selectStart = -1; selectEnd = -1; }
                    ImGui::EndChild();
                }
            }
        } else ImGui::Text("Select an audio file from the sidebar to begin.");
        ImGui::EndGroup();
        if (g_MainFont) ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    ImGui_ImplSDLRenderer2_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
