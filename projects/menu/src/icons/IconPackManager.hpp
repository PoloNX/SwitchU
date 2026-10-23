#pragma once

#include <string>
#include <vector>

namespace switchu::icons {

inline constexpr const char* kDefaultPackId = "default";

// Pack glyphs live under romfs:/icons/<pack>/. Shared chrome (page arrows,
// batteries, gamecard) ships in default/; music/ stays at icons root.
inline constexpr const char* kPackGlyphFiles[] = {
    "album.png",
    "mii_editor.png",
    "settings.png",
    "controller.png",
    "power.png",
    "themes.png",
};
inline constexpr int kPackGlyphCount = 6;
inline constexpr const char* kChangeViewFile = "change_view.png";

struct IconPackInfo {
    std::string id;
    std::string name;
    std::string author;
    std::string directory;   // absolute/romfs path to the pack folder
    std::string previewPath; // baked 3×2 mosaic (may be empty until generated)
    bool isDefault = false;
};

// `romfs:/icons` or `sdmc:/.../icons` root that contains pack subfolders.
std::string iconsRoot(const std::string& assetsBase);

std::string packDirectory(const std::string& assetsBase, const std::string& packId);

// Prefer pack file, else default pack, else empty.
std::string resolveGlyphPath(const std::string& assetsBase,
                             const std::string& packId,
                             const std::string& fileName);

std::vector<IconPackInfo> discoverPacks(const std::string& assetsBase);

std::string displayNameForPack(const std::string& packId);

// Ensure preview.png exists beside the pack (CPU bake). Returns path or empty.
std::string ensurePackPreview(const std::string& packDir);

} // namespace switchu::icons
