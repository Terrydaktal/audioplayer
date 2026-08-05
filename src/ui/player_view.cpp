#include "ui/player_view.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "audio/crop_service.h"
#include "imgui.h"
#include "platform/filesystem_utils.h"
#include "core/time_utils.h"
#include "transcripts/transcript_search.h"

namespace fs = std::filesystem;

namespace ui {
namespace {

void ResetTranscriptUi(ApplicationState& state) {
    state.transcriptMatches.clear();
    state.currentMatch = -1;
    state.lastMatch = -1;
    state.selectionStart = -1;
    state.selectionEnd = -1;
}

void RenderSearchBar(UiContext& context, const std::vector<Segment>& segments) {
    auto& state = context.state;
    if (!state.showSearch) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::BeginChild("SearchBar", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 1.5f), true);
    if (state.searchFocusPending) {
        ImGui::SetKeyboardFocusHere();
        state.searchFocusPending = false;
    }
    ImGui::Text("Find:");
    ImGui::SameLine();
    if (ImGui::InputText("##search", state.transcriptSearch, sizeof(state.transcriptSearch))) {
        FindTranscriptMatches(segments, state.transcriptSearch, state.transcriptMatches, state.currentMatch);
        state.lastMatch = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Prev") && !state.transcriptMatches.empty()) {
        state.currentMatch = (state.currentMatch - 1 + static_cast<int>(state.transcriptMatches.size())) %
                             static_cast<int>(state.transcriptMatches.size());
        if (state.currentAudioAvailable) state.player.Seek(segments[state.transcriptMatches[state.currentMatch]].start);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next") && !state.transcriptMatches.empty()) {
        state.currentMatch = (state.currentMatch + 1) % static_cast<int>(state.transcriptMatches.size());
        if (state.currentAudioAvailable) state.player.Seek(segments[state.transcriptMatches[state.currentMatch]].start);
    }
    ImGui::SameLine();
    if (!state.transcriptMatches.empty()) {
        ImGui::Text("%d of %d", state.currentMatch + 1, static_cast<int>(state.transcriptMatches.size()));
    } else if (std::strlen(state.transcriptSearch) > 0) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "No matches");
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 30);
    if (ImGui::Button("X")) state.showSearch = false;
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void RenderTranscript(UiContext& context) {
    auto& state = context.state;
    std::lock_guard<std::mutex> lock(state.transcriptMutex);
    const auto& segments = state.transcript.GetSegments();
    if (segments.empty()) {
        if (state.transcriptLoading) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Loading transcript...");
        } else {
            ImGui::TextDisabled("No transcript is available for this recording.");
        }
        return;
    }

    ImGui::Separator();
    RenderSearchBar(context, segments);
    ImGui::BeginChild("TranscriptList", ImVec2(0, 0), true);
    if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
        state.selectionStart = -1;
        state.selectionEnd = -1;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(segments.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const auto& segment = segments[index];
            char label[2048];
            std::snprintf(label, sizeof(label), "[ %s -> %s] %s",
                          FormatTime(segment.start).c_str(), FormatTime(segment.end).c_str(), segment.text.c_str());
            const bool isMatch = std::find(state.transcriptMatches.begin(), state.transcriptMatches.end(), index) !=
                                 state.transcriptMatches.end();
            const bool isCurrentMatch = state.currentMatch >= 0 &&
                state.currentMatch < static_cast<int>(state.transcriptMatches.size()) &&
                state.transcriptMatches[state.currentMatch] == index;
            bool isSelected = false;
            if (state.selectionStart != -1 && state.selectionEnd != -1) {
                const int start = std::min(state.selectionStart, state.selectionEnd);
                const int end = std::max(state.selectionStart, state.selectionEnd);
                isSelected = index >= start && index <= end;
            }

            if (isCurrentMatch) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
            else if (isMatch) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 1, 1));
            if (isSelected) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.59f, 0.98f, 0.5f));
            ImGui::Selectable(label, isSelected || isCurrentMatch, ImGuiSelectableFlags_AllowOverlap);
            if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()) {
                state.selectionStart = index;
                state.selectionEnd = index;
            }
            if (ImGui::IsMouseDragging(0) && ImGui::IsItemHovered()) state.selectionEnd = index;
            if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) &&
                ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 4.0f) {
                if (state.currentAudioAvailable) state.player.Seek(segment.start);
                state.selectionStart = -1;
                state.selectionEnd = -1;
            }
            if (isSelected) ImGui::PopStyleColor();
            if (isCurrentMatch || isMatch) ImGui::PopStyleColor();
        }
    }
    if (state.currentMatch >= 0 && state.currentMatch < static_cast<int>(state.transcriptMatches.size()) &&
        state.currentMatch != state.lastMatch) {
        const float itemHeight = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetScrollY(state.transcriptMatches[state.currentMatch] * itemHeight - ImGui::GetWindowHeight() * 0.5f);
        state.lastMatch = state.currentMatch;
    }
    if (ImGui::IsMouseReleased(0) && state.selectionStart != -1 && state.selectionStart == state.selectionEnd) {
        state.selectionStart = -1;
        state.selectionEnd = -1;
    }
    ImGui::EndChild();
}

} // namespace

void RenderPlayerTab(UiContext& context) {
    auto& state = context.state;
    if (state.currentLoadedFile.empty()) {
        ImGui::Text("Select an audio file from the sidebar to begin.");
        return;
    }

    if (!state.currentAudioAvailable) {
        ImGui::TextColored(ImVec4(1, 0.75f, 0, 1),
                           "Audio file is unavailable; transcript is still available.");
        ImGui::Separator();
    }
    if (state.currentAudioAvailable) {
        if (state.player.IsLoading() || state.transcriptLoading) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Loading %s...", state.currentLoadedFile.c_str());
            ImGui::Separator();
        }

        const MediaInfo info = state.player.GetMediaInfo();
        if (state.cropPathNeedsUpdate && !info.recommendedExt.empty()) {
            const std::string stem = fs::path(state.currentLoadedFile).stem().string();
            std::snprintf(state.cropOutputPath, sizeof(state.cropOutputPath), "%s_cropped%s",
                          stem.c_str(), info.recommendedExt.c_str());
            state.cropPathNeedsUpdate = false;
            state.cropEnd = static_cast<float>(state.player.GetDuration());
            state.cropStart = 0.0f;
            std::snprintf(state.cropStartBuffer, sizeof(state.cropStartBuffer), "%s", FormatTime(state.cropStart).c_str());
            std::snprintf(state.cropEndBuffer, sizeof(state.cropEndBuffer), "%s", FormatTime(state.cropEnd).c_str());
        }

        ImGui::Text("Playing: %s [%s / %s]", state.currentLoadedFile.c_str(),
                    info.container.c_str(), info.codec.c_str());
        ImGui::Separator();
        const float buttonWidth = ImGui::CalcTextSize("Pause").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (ImGui::Button(state.player.IsPlaying() ? "Pause" : "Play", ImVec2(buttonWidth, 0))) {
            if (state.player.IsPlaying()) state.player.Pause();
            else state.player.Play();
        }
        ImGui::SameLine();
        float currentPosition = static_cast<float>(state.player.GetPosition());
        const float duration = static_cast<float>(state.player.GetDuration());
        if (ImGui::SliderFloat("Position", &currentPosition, 0.0f, duration, "")) state.player.Seek(currentPosition);
        ImGui::Text("Current Time: %s / %s", FormatTime(currentPosition).c_str(), FormatTime(duration).c_str());

        if (ImGui::CollapsingHeader("Cropping")) {
            ImGui::Text("Crop Start:");
            ImGui::SameLine();
            if (ImGui::InputText("##cropstart", state.cropStartBuffer, sizeof(state.cropStartBuffer)))
                state.cropStart = static_cast<float>(ParseTime(state.cropStartBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Set to Current##start")) {
                state.cropStart = currentPosition;
                std::snprintf(state.cropStartBuffer, sizeof(state.cropStartBuffer), "%s", FormatTime(state.cropStart).c_str());
            }
            ImGui::Text("Crop End:  ");
            ImGui::SameLine();
            if (ImGui::InputText("##cropend", state.cropEndBuffer, sizeof(state.cropEndBuffer)))
                state.cropEnd = static_cast<float>(ParseTime(state.cropEndBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Set to Current##end")) {
                state.cropEnd = currentPosition;
                std::snprintf(state.cropEndBuffer, sizeof(state.cropEndBuffer), "%s", FormatTime(state.cropEnd).c_str());
            }
            ImGui::InputText("Output Path", state.cropOutputPath, sizeof(state.cropOutputPath));
            if (ImGui::Button("Crop & Save")) {
                const fs::path input = fs::path(state.audioFolder) / state.currentLoadedFile;
                if (audio::CropAudio(input.string(), state.cropOutputPath, state.cropStart, state.cropEnd, info.recommendedExt))
                    ImGui::OpenPopup("Crop Success");
                else
                    ImGui::OpenPopup("Crop Failed");
            }
            if (ImGui::BeginPopupModal("Crop Success", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Successfully cropped audio to %s", state.cropOutputPath);
                if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("Crop Failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Failed to crop audio. Check console for details.");
                if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
    }

    RenderTranscript(context);
}

} // namespace ui
