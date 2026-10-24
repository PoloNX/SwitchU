#pragma once

#include <nxui/core/Types.hpp>
#include <nxui/Theme.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace nxui {
class Font;
class Renderer;
class Texture;
}

namespace switchu::folders {

/// 2x3 icon peek grid in single-row poster folders (6 small icons — cheap).
inline constexpr int kFolderPeekMax = 6;

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
    /// Up to 6 small title icons (2x3) when no custom folder poster is set.
    std::array<nxui::Texture*, kFolderPeekMax> peekCovers{};
    int peekCoverCount = 0;
    bool showCover = false;
    bool focused = false;
    bool drawName = true;
    bool schematicPlaceholder = false;
    /// Dynamic Line + posters: draw portrait-tuned chrome in a tall 2:3 tile.
    bool posterAspect = false;
    /// True when `cover` is the folder's own SteamGridDB poster (single inset).
    bool coverIsFolderPoster = false;
    /// True when `cover` is a baked 2×3 peek mosaic (draw into peek content rect).
    bool coverIsPeekComposite = false;
};

void drawFolderStyle(const FolderStyleDrawArgs& args);

} // namespace switchu::folders
