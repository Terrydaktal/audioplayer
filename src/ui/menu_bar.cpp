#include "ui/menu_bar.h"

#include "imgui.h"

namespace ui {

void RenderMenuBar(UiContext& context) {
    auto& state = context.state;
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("View")) {
        if (ImGui::BeginMenu("Change Font")) {
            for (const auto& font : state.availableFonts) {
                if (ImGui::MenuItem(font.name.c_str())) {
                    state.currentFontPath = font.path;
                    state.fontNeedsUpdate = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::InputFloat("Font Size", &state.fontSize, 1.0f, 5.0f, "%.1f")) {
            if (state.fontSize < 8.0f) state.fontSize = 8.0f;
            if (state.fontSize > 72.0f) state.fontSize = 72.0f;
            state.fontNeedsUpdate = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Find", "Ctrl+F")) {
            state.showSearch = true;
            state.searchFocusPending = true;
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

} // namespace ui
