#include "ui/ui_context.h"

#include <algorithm>

#include "core/time_utils.h"
#include "imgui.h"
#include "ui/global_search_view.h"
#include "ui/menu_bar.h"
#include "ui/player_view.h"
#include "ui/sidebar_view.h"

namespace ui {

void HandleEvent(UiContext& context, const SDL_Event& event) {
    auto& state = context.state;
    if (event.type == SDL_KEYDOWN) {
        if (context.io.KeyCtrl && event.key.keysym.sym == SDLK_f) {
            state.showSearch = true;
            state.searchFocusPending = true;
        }
        if (event.key.keysym.sym == SDLK_ESCAPE) state.showSearch = false;
        if (context.io.KeyCtrl && event.key.keysym.sym == SDLK_c &&
            state.selectionStart != -1 && state.selectionEnd != -1) {
            const int start = std::min(state.selectionStart, state.selectionEnd);
            const int end = std::max(state.selectionStart, state.selectionEnd);
            std::string copied;
            std::lock_guard<std::mutex> lock(state.transcriptMutex);
            const auto& segments = state.transcript.GetSegments();
            if (end < static_cast<int>(segments.size())) {
                for (int index = start; index <= end; ++index) {
                    copied += "[" + FormatTime(segments[index].start) + " -> " +
                              FormatTime(segments[index].end) + "] " + segments[index].text + "\n";
                }
                SDL_SetClipboardText(copied.c_str());
            }
        }
    }
}

void Render(UiContext& context, const UiActions& actions) {
    auto& state = context.state;
    RenderMenuBar(context);

    ImGui::SetNextWindowPos(ImVec2(0, 20));
    ImGui::SetNextWindowSize(ImVec2(context.io.DisplaySize.x, context.io.DisplaySize.y - 20));
    ImGui::Begin("Audio Player", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (state.mainFont) ImGui::PushFont(state.mainFont);

    RenderSidebar(context, actions);
    ImGui::SameLine();
    ImGui::BeginGroup();

    const bool loading = state.player.IsLoading();
    if (state.wasLoading && !loading && state.pendingSeekTime >= 0.0 && state.currentAudioAvailable) {
        state.player.Seek(state.pendingSeekTime);
        state.pendingSeekTime = -1.0;
    }
    state.wasLoading = loading;

    if (ImGui::BeginTabBar("MainTabBar")) {
        ImGuiTabItemFlags playerFlags = 0;
        if (state.requestTabSwitch && state.activeTab == 0) {
            playerFlags |= ImGuiTabItemFlags_SetSelected;
            state.requestTabSwitch = false;
        }
        if (ImGui::BeginTabItem("Current Player", nullptr, playerFlags)) {
            state.activeTab = 0;
            RenderPlayerTab(context);
            ImGui::EndTabItem();
        }

        ImGuiTabItemFlags searchFlags = 0;
        if (state.requestTabSwitch && state.activeTab == 1) {
            searchFlags |= ImGuiTabItemFlags_SetSelected;
            state.requestTabSwitch = false;
        }
        if (ImGui::BeginTabItem("Global Search", nullptr, searchFlags)) {
            state.activeTab = 1;
            RenderGlobalSearchTab(context, actions);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndGroup();
    if (state.mainFont) ImGui::PopFont();
    ImGui::End();
}

} // namespace ui
