#pragma once

#include <nxui/core/Types.hpp>
#include <nxui/Theme.hpp>
#include <cstdint>
#include <string>

namespace nxui {
class Font;
class Renderer;
class Texture;
}

namespace switchu::folders {

struct FolderStyleDrawArgs {
    nxui::Renderer* renderer = nullptr;
    nxui::Rect bounds{};
    float radius = 18.f;
    float scale = 1.f;
    float opacity = 1.f;
    int styleIndex = 0;
    nxui::Color accent{};
    nxui::ThemeMode themeMode = nxui::ThemeMode::Dark;
    nxui::Font* font = nullptr;
    const std::string* title = nullptr;
    nxui::Texture* cover = nullptr;
    bool showCover = false;
    bool focused = false;
    bool drawName = true;
    bool schematicPlaceholder = false;
};

void drawFolderStyle(const FolderStyleDrawArgs& args);

} // namespace switchu::folders
