#ifndef UI_CONTEXT_H
#define UI_CONTEXT_H

#include <functional>
#include <string>

#include <SDL.h>

#include "app/application_state.h"

struct ImGuiIO;

struct UiActions {
    std::function<void(const std::string&)> selectAudioFile;
    std::function<void(const GlobalSearchResult&)> selectSearchResult;
    std::function<void()> syncDatabase;
    std::function<void(const std::string&)> searchAllTranscripts;
};

struct UiContext {
    ApplicationState& state;
    ImGuiIO& io;
};

namespace ui {

void HandleEvent(UiContext& context, const SDL_Event& event);
void Render(UiContext& context, const UiActions& actions);

} // namespace ui

#endif
