#pragma once

#include <nxui/core/Types.hpp>
#include <nxui/Theme.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace nxui {
class Font;
class Renderer;
class Texture;
}

namespace switchu::folders {

/// Live peek icons: up to 5×2 (10) for large grid folder tiles. Single-row
/// baked composites still use the first 6 in a fixed 2×3 mosaic.
inline constexpr int kFolderPeekMax = 10;
inline constexpr int kFolderPeekCompositeSlots = 6;
inline constexpr int kFolderPeekMaxColumns = 5;
inline constexpr int kFolderPeekMaxRows = 3;

/// Peek mosaic for a HOME folder tile span (not always 1:1 with the span).
inline constexpr void folderPeekGridForSpan(int spanColumns, int spanRows,
                                           int& outColumns, int& outRows) {
    // 2×1 is wide enough for three icons in a row.
    if (spanColumns == 2 && spanRows == 1) {
        outColumns = 3;
        outRows = 1;
        return;
    }
    // 4×2 has room for an extra column of peeks.
    if (spanColumns == 4 && spanRows == 2) {
        outColumns = 5;
        outRows = 2;
        return;
    }
    outColumns = std::clamp(spanColumns, 1, kFolderPeekMaxColumns);
    outRows = std::clamp(spanRows, 1, kFolderPeekMaxRows);
}

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
    /// Small member icons when no custom folder poster/hero is set.
    std::array<nxui::Texture*, kFolderPeekMax> peekCovers{};
    int peekCoverCount = 0;
    /// Peek mosaic for the folder tile (2×1→3×1, 4×2→5×2, else span).
    int peekColumns = 2;
    int peekRows = 3;
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
    /// HOME tile span (1×1, 2×1, 2×2, 4×2, 2×3, …) for layout/title scale.
    int tileColumns = 1;
    int tileRows = 1;
};

void drawFolderStyle(const FolderStyleDrawArgs& args);

} // namespace switchu::folders
