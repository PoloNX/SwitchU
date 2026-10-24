#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace switchu::icons {

inline constexpr const char* kDefaultPackId = "default";

// Theme-bundled icon sets appear as theme:<themeFolder> (e.g. theme:iisu).
// Glyphs stay under sdmc:/config/SwitchU/themes/<folder>/assets/icons/.
// Use a string literal array (not const char*) so sizeof gives the text length.
inline constexpr char kThemePackPrefix[] = "theme:";
inline constexpr std::size_t kThemePackPrefixLen = sizeof(kThemePackPrefix) - 1; // 6
inline constexpr const char* kInstalledThemesDir = "sdmc:/config/SwitchU/themes";

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
    std::string previewPath; // generated 3x2 mosaic (may be empty until baked)
    bool isDefault = false;
    // True when album.png (or first pack glyph) is full-colour artwork.
    bool colourPack = false;
    // True when this entry is sourced from an installed theme's assets/icons.
    bool fromTheme = false;
};

// `romfs:/icons` or `sdmc:/.../icons` root that contains pack subfolders.
std::string iconsRoot(const std::string& assetsBase);

std::string packDirectory(const std::string& assetsBase, const std::string& packId);

// Prefer pack file, else default pack, else empty.
std::string resolveGlyphPath(const std::string& assetsBase,
                             const std::string& packId,
                             const std::string& fileName);

std::vector<IconPackInfo> discoverPacks(const std::string& assetsBase);

// Same shape as discoverPacks, but scans installed theme folders for icons.
std::vector<IconPackInfo> discoverThemePacks();

// Absolute icons directory for any pack id (SD pack or theme:*). Empty if missing.
std::string resolvePackIconsDirectory(const std::string& assetsBase, const std::string& packId);

std::string displayNameForPack(const std::string& packId);

// Prefer pack preview.png; else cached transparent mosaic; else bake once
// under sdmc:/config/SwitchU/icon_pack_previews/ (fills missing glyphs from default).
std::string ensurePackPreview(const std::string& packDir, const std::string& packId = {});

bool isThemePackId(const std::string& packId);
std::string makeThemePackId(const std::string& themeFolderOrPresetId);
std::string themeFolderFromPackId(const std::string& packId);

// Theme packs may ship a partial glyph set (e.g. iisu omits album.png).
bool iconDirLooksUsable(const std::string& dir);
std::string firstGlyphPathInDir(const std::string& dir);

} // namespace switchu::icons
