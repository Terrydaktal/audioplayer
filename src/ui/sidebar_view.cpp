#include "ui/sidebar_view.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "imgui.h"
#include "platform/filesystem_utils.h"

namespace fs = std::filesystem;

namespace ui {

void RenderSidebar(UiContext& context, const UiActions& actions) {
    auto& state = context.state;
    ImGui::BeginChild("Sidebar", ImVec2(300, 0), true);
    ImGui::Text("Audio Folder:");
    if (ImGui::InputText("##folder", state.audioFolder, sizeof(state.audioFolder))) {
        state.folderFiles = ScanAudioFolder(state.audioFolder);
    }
    ImGui::Text("Transcripts:");
    ImGui::InputText("##transcripts", state.transcriptDir, sizeof(state.transcriptDir));
    ImGui::Separator();
    ImGui::Text("Filter Files:");
    ImGui::InputText("##filter", state.fileFilter, sizeof(state.fileFilter));

    ImGui::BeginChild("FileList", ImVec2(0, 0), true);
    std::string filter = state.fileFilter;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    for (const auto& filename : state.folderFiles) {
        std::string lowerFilename = filename;
        std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (!filter.empty() && lowerFilename.find(filter) == std::string::npos) continue;
        if (ImGui::Selectable(filename.c_str(), state.currentLoadedFile == filename)) {
            actions.selectAudioFile(filename);
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace ui
