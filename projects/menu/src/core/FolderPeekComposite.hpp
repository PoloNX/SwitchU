#pragma once

#include "FolderStore.hpp"
#include "widgets/FolderPalette.hpp"
#include "widgets/FolderStyleDraw.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace switchu::folders::peek_composite {

inline constexpr int kWidth = 256;
inline constexpr int kHeight = 384;
// v2: rounded cells + shadow/outline matching drawPeekGrid.
inline constexpr int kLayoutVersion = 2;

struct BakedImage {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
};

/// Ordered first 6 non-zero title ids (same as applyFolderCovers peek walk).
std::array<std::uint64_t, kFolderPeekMax> memberTitleIds(const Folder& folder);

/// CPU bake of the 2×3 peek mosaic into a 2:3 RGBA canvas.
BakedImage bake(const Folder& folder);

/// Load disk cache if membership + icon stamps + color still match.
bool load(const Folder& folder, BakedImage& out);

/// Write disk cache for a successful bake.
bool save(const Folder& folder, const BakedImage& image);

void remove(std::uint32_t folderId);

std::string cachePath(std::uint32_t folderId);

} // namespace switchu::folders::peek_composite
