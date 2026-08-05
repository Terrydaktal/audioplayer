#include "platform/font_manager.h"

#include "imgui.h"
#include "imgui_impl_sdlrenderer2.h"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

const std::string& GetFallbackFontPath() {
    static const std::vector<std::string> options = {
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
    for (const auto& path : options) {
        if (fs::exists(path)) {
            return path;
        }
    }
    static const std::string empty;
    return empty;
}

} // namespace

std::vector<FontInfo> ScanFonts() {
    std::vector<FontInfo> fonts = {
        {"Scalable Default", ""},
        {"Fixed Default (13px)", "FIXED_DEFAULT"},
    };
    std::vector<std::string> searchPaths = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
    };
    if (const char* home = std::getenv("HOME")) {
        std::string userFontPath = std::string(home) + "/.local/share/fonts";
        if (fs::exists(userFontPath)) {
            searchPaths.push_back(userFontPath);
        }
    }

    for (const auto& base : searchPaths) {
        if (!fs::exists(base)) {
            continue;
        }
        try {
            for (const auto& entry : fs::recursive_directory_iterator(base)) {
                if (entry.is_regular_file()) {
                    std::string extension = entry.path().extension().string();
                    if (extension == ".ttf" || extension == ".otf") {
                        fonts.push_back({entry.path().stem().string(), entry.path().string()});
                    }
                }
                if (fonts.size() > 500) {
                    break;
                }
            }
        } catch (...) {
            // Font discovery is best-effort.
        }
    }
    return fonts;
}

void ApplyFontChange(const std::string& path, float size, ImFont*& activeFont) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    std::string actualPath = path.empty() ? GetFallbackFontPath() : path;
    ImFont* font = nullptr;
    if (actualPath != "FIXED_DEFAULT" && !actualPath.empty()) {
        font = io.Fonts->AddFontFromFileTTF(actualPath.c_str(), size);
    }
    if (!font) {
        font = io.Fonts->AddFontDefault();
    }

    activeFont = font;
    io.FontDefault = activeFont;
    io.Fonts->Build();

    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    for (ImTextureData* texture : platform.Textures) {
        if (texture->Status != ImTextureStatus_OK) {
            ImGui_ImplSDLRenderer2_UpdateTexture(texture);
        }
    }
}
