#pragma once
#include <nxui/Theme.hpp>
#include <string>
#include <vector>

enum class ThemePresetSource {
    BuiltIn,
    UserPreset,
    InstalledPackage,
};

// Theme Shop + managed installs.
inline constexpr const char* kShopThemesDir = "sdmc:/config/SwitchU/themes";
// Manual / user-facing drop folder (same tree as icons sibling under /switch/SwitchU).
inline constexpr const char* kManualThemesDir = "sdmc:/switch/SwitchU/themes";


struct ThemeColorSet {
    float cursorH = -1.f, cursorS = -1.f, cursorL = -1.f;
    float accentH = 0.f, accentS = 0.f, accentL = 0.f;
    float bgH = 0.f, bgS = 0.f, bgL = 0.f;
    float bgAccH = 0.f, bgAccS = 0.f, bgAccL = 0.f;
    float shapeH = 0.f, shapeS = 0.f, shapeL = 0.f;
};

enum class ThemeBackgroundLayout {
    Floating,
    Grid,
};

enum class ThemeBackgroundShapeSet {
    Mixed,
    Circle,
    Triangle,
    Square,
    Diamond,
    Hexagon,
};

enum class ThemeBackgroundSymmetry {
    None,
    MirrorX,
    MirrorY,
    Quad,
};

struct ThemeBackgroundConfig {
    std::string imagePath;
    float imageOpacity = 0.f;
    bool imageCover = true;
    ThemeBackgroundLayout layout = ThemeBackgroundLayout::Floating;
    ThemeBackgroundShapeSet shapeSet = ThemeBackgroundShapeSet::Mixed;
    ThemeBackgroundSymmetry symmetry = ThemeBackgroundSymmetry::None;
    int shapeCount = 30;
    int gridColumns = 14;
    int gridRows = 8;
    float spacingX = 88.f;
    float spacingY = 88.f;
    float sizeMin = 14.f;
    float sizeMax = 54.f;
    float speedMin = 6.f;
    float speedMax = 28.f;
    float wobble = 16.f;
    float opacity = 1.f;
    float rotationSpeed = 0.5f;
    bool fixedOrientation = false;
    float orientationDegrees = 0.f;
    float cornerRoundness = 0.f;
};

struct ThemeFontConfig {
    std::string regularPath;
    std::string smallPath;
};

struct ThemeIconConfig {
    std::string basePath;
    // When set, Apply uses this Icons-library pack id (e.g. "atmos") instead of
    // extracting assets/icons under the theme folder name.
    std::string packId;
};

struct ThemePreset {
    std::string     id;
    std::string     name;
    std::string     author;
    std::string     version;
    nxui::ThemeMode mode   = nxui::ThemeMode::Dark;
    ThemeColorSet   colors;
    ThemeBackgroundConfig background;
    ThemeFontConfig fonts;
    ThemeIconConfig icons;
    bool            builtIn = true;
    ThemePresetSource source = ThemePresetSource::BuiltIn;
    std::string     soundPreset;
    std::string     installPath;

    nxui::Theme toTheme() const;

    static ThemeColorSet extractColors(const nxui::Theme& theme);

    static const std::vector<ThemePreset>& builtInPresets();

    static std::vector<ThemePreset> loadUserPresets();

    static std::vector<ThemePreset> loadInstalledPackages();

    // Resolve package:<slug> to an on-disk folder (config first, then switch).
    static std::string resolveInstalledThemePath(const std::string& packageOrSlug);

    static bool saveUserPresets(const std::vector<ThemePreset>& presets);
};
