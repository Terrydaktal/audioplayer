#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include <string>
#include <vector>

struct ImFont;

struct FontInfo {
    std::string name;
    std::string path;
};

std::vector<FontInfo> ScanFonts();
void ApplyFontChange(const std::string& path, float size, ImFont*& activeFont);

#endif
