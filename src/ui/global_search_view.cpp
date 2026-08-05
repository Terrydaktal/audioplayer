#include "ui/global_search_view.h"

#include <cstdio>
#include <filesystem>

#include "core/time_utils.h"
#include "imgui.h"
#include "platform/filesystem_utils.h"

namespace fs = std::filesystem;

namespace ui {
namespace {

std::string FormatRecordingTime(const GlobalSearchResult& result, const fs::path& audioPath) {
    if (!result.recordingTime.empty()) {
        std::string formatted = result.recordingTime;
        if (formatted.size() >= 19 && formatted[10] == 'T') {
            formatted[10] = ' ';
            formatted.resize(19);
        }
        if (result.recordingTimeUncertain) formatted = "~" + formatted;
        return formatted;
    }
    return FormatFileMTime(audioPath);
}

} // namespace

void RenderGlobalSearchTab(UiContext& context, const UiActions& actions) {
    auto& state = context.state;
    if (state.databaseSyncing) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Syncing database with folder...");
    } else if (ImGui::Button("Sync Transcripts DB")) {
        actions.syncDatabase();
    }

    ImGui::Text("Search all transcripts for:");
    ImGui::InputText("##globalsearchquery", state.globalSearchQuery, sizeof(state.globalSearchQuery));
    ImGui::SameLine();
    const bool submitted = ImGui::Button("Search") || ImGui::IsKeyPressed(ImGuiKey_Enter);
    if (submitted && state.globalSearchQuery[0] != '\0' &&
        !state.globalSearchRunning && !state.databaseSyncing) {
        actions.searchAllTranscripts(state.globalSearchQuery);
    }
    ImGui::Separator();

    if (state.globalSearchRunning) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Searching all transcripts...");
        return;
    }

    std::lock_guard<std::mutex> lock(state.globalSearchMutex);
    ImGui::Text("Top matches: %d", static_cast<int>(state.globalSearchResults.size()));
    ImGui::BeginChild("GlobalSearchResults", ImVec2(0, 0), true);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(state.globalSearchResults.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const auto& item = state.globalSearchResults[index];
            const fs::path audioPath = fs::path(state.audioFolder) / item.audioFilename;
            const std::string recordingTime = FormatRecordingTime(item, audioPath);
            char label[4096];
            std::snprintf(label, sizeof(label), "[%s] [%s] [%s] %s",
                          recordingTime.c_str(), item.audioFilename.c_str(),
                          FormatTime(item.start).c_str(), item.text.c_str());
            if (ImGui::Selectable(label, false)) actions.selectSearchResult(item);
        }
    }
    ImGui::EndChild();
}

} // namespace ui
