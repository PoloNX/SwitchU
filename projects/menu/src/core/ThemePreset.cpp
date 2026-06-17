#include "ThemePreset.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <system_error>

namespace {

template <typename T>
void readJsonOpt(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
        return;

    try {
        out = it->get<T>();
    } catch (...) {
    }
}

template <typename T>
bool readJsonAliases(const nlohmann::json& j, std::initializer_list<const char*> keys, T& out) {
    for (const char* key : keys) {
        auto it = j.find(key);
        if (it == j.end() || it->is_null())
            continue;

        try {
            out = it->get<T>();
            return true;
        } catch (...) {
        }
    }

    return false;
}

std::string makeThemeId(const char* prefix, const std::string& value) {
    return std::string(prefix) + value;
}

std::string trimString(std::string value) {
    auto notSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string lowerString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });
    return value;
}

bool hasBundledAudio(const std::string& installDir) {
    static const char* candidates[] = {
        "/sounds/sfx",
        "/sounds/music",
        "/sfx",
        "/music",
    };

    for (const char* suffix : candidates) {
        std::error_code ec;
        if (std::filesystem::is_directory(installDir + suffix, ec))
            return true;
    }

    return false;
}

std::string normalizePackageId(const std::string& value) {
    std::string trimmed = trimString(value);
    if (trimmed.empty())
        return trimmed;
    if (trimmed.find(':') != std::string::npos)
        return trimmed;
    return makeThemeId("package:", trimmed);
}

std::string normalizeThemeId(const char* prefix, const std::string& value) {
    std::string trimmed = trimString(value);
    if (trimmed.empty())
        return trimmed;
    if (trimmed.find(':') != std::string::npos)
        return trimmed;
    return makeThemeId(prefix, trimmed);
}

#ifdef SWITCHU_HOMEBREW
static constexpr const char* kBuiltInThemesDir = "romfs:/themes";
#else
static constexpr const char* kBuiltInThemesDir = "sdmc:/switch/SwitchU/themes";
#endif

static constexpr const char* kDefaultSharedSoundPreset = "wiiu";

bool parseHslTripletString(std::string value, float& h, float& s, float& l) {
    value = trimString(value);
    if (value.empty())
        return false;

    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        if (std::isdigit((unsigned char)ch) || ch == '.' || ch == '-' || ch == '+') {
            normalized.push_back(ch);
        } else {
            normalized.push_back(' ');
        }
    }

    std::stringstream ss(normalized);
    float vals[3] {};
    if (!(ss >> vals[0] >> vals[1] >> vals[2]))
        return false;

    h = vals[0];
    s = vals[1];
    l = vals[2];
    return true;
}

bool parseHslTripletValue(const nlohmann::json& value, float& h, float& s, float& l) {
    try {
        if (value.is_object()) {
            float localH = h;
            float localS = s;
            float localL = l;
            readJsonAliases(value, {"h", "hue"}, localH);
            readJsonAliases(value, {"s", "sat", "saturation"}, localS);
            readJsonAliases(value, {"l", "light", "lightness"}, localL);
            h = localH;
            s = localS;
            l = localL;
            return true;
        }

        if (value.is_array() && value.size() >= 3) {
            h = value[0].get<float>();
            s = value[1].get<float>();
            l = value[2].get<float>();
            return true;
        }

        if (value.is_string()) {
            return parseHslTripletString(value.get<std::string>(), h, s, l);
        }
    } catch (...) {
    }

    return false;
}

bool readHslTriplet(const nlohmann::json& j,
                    std::initializer_list<const char*> keys,
                    float& h, float& s, float& l) {
    for (const char* key : keys) {
        auto it = j.find(key);
        if (it == j.end() || it->is_null())
            continue;
        if (parseHslTripletValue(*it, h, s, l))
            return true;
    }

    return false;
}

const nlohmann::json* findObjectAlias(const nlohmann::json& j,
                                      std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        auto it = j.find(key);
        if (it == j.end() || it->is_null() || !it->is_object())
            continue;
        return &(*it);
    }

    return nullptr;
}

bool parseFloatPairString(std::string value, float& first, float& second) {
    value = trimString(value);
    if (value.empty())
        return false;

    std::string normalized;
    normalized.reserve(value.size());
    for (char ch : value) {
        if (std::isdigit((unsigned char)ch) || ch == '.' || ch == '-' || ch == '+')
            normalized.push_back(ch);
        else
            normalized.push_back(' ');
    }

    std::stringstream ss(normalized);
    float vals[2] {};
    if (!(ss >> vals[0] >> vals[1]))
        return false;

    first = vals[0];
    second = vals[1];
    return true;
}

bool parseFloatPairValue(const nlohmann::json& value, float& first, float& second) {
    try {
        if (value.is_array() && value.size() >= 2) {
            first = value[0].get<float>();
            second = value[1].get<float>();
            return true;
        }

        if (value.is_object()) {
            float localFirst = first;
            float localSecond = second;
            readJsonAliases(value, {"x", "min", "first", "width"}, localFirst);
            readJsonAliases(value, {"y", "max", "second", "height"}, localSecond);
            first = localFirst;
            second = localSecond;
            return true;
        }

        if (value.is_string())
            return parseFloatPairString(value.get<std::string>(), first, second);
    } catch (...) {
    }

    return false;
}

bool readFloatPair(const nlohmann::json& j,
                   std::initializer_list<const char*> keys,
                   float& first, float& second) {
    for (const char* key : keys) {
        auto it = j.find(key);
        if (it == j.end() || it->is_null())
            continue;
        if (parseFloatPairValue(*it, first, second))
            return true;
    }

    return false;
}

ThemeBackgroundLayout parseBackgroundLayout(std::string value) {
    value = lowerString(trimString(value));
    if (value == "grid" || value == "tiles" || value == "tiled")
        return ThemeBackgroundLayout::Grid;
    return ThemeBackgroundLayout::Floating;
}

ThemeBackgroundShapeSet parseBackgroundShapeSet(std::string value) {
    value = lowerString(trimString(value));
    if (value == "circle" || value == "circles")
        return ThemeBackgroundShapeSet::Circle;
    if (value == "triangle" || value == "triangles")
        return ThemeBackgroundShapeSet::Triangle;
    if (value == "square" || value == "squares")
        return ThemeBackgroundShapeSet::Square;
    if (value == "diamond" || value == "diamonds")
        return ThemeBackgroundShapeSet::Diamond;
    if (value == "hexagon" || value == "hexagons" || value == "hex")
        return ThemeBackgroundShapeSet::Hexagon;
    return ThemeBackgroundShapeSet::Mixed;
}

ThemeBackgroundSymmetry parseBackgroundSymmetry(std::string value) {
    value = lowerString(trimString(value));
    if (value == "mirrorx" || value == "mirror-x" || value == "horizontal" || value == "x")
        return ThemeBackgroundSymmetry::MirrorX;
    if (value == "mirrory" || value == "mirror-y" || value == "vertical" || value == "y")
        return ThemeBackgroundSymmetry::MirrorY;
    if (value == "quad" || value == "both" || value == "xy" || value == "mirror-xy" || value == "mirrorxy")
        return ThemeBackgroundSymmetry::Quad;
    return ThemeBackgroundSymmetry::None;
}

void readThemeBackgroundFromObject(const nlohmann::json& j, ThemeBackgroundConfig& background) {
    std::string layoutValue;
    if (readJsonAliases(j, {"layout", "pattern", "style"}, layoutValue))
        background.layout = parseBackgroundLayout(layoutValue);

    std::string shapeValue;
    if (readJsonAliases(j, {"shape", "shapeSet", "shape_set", "shapes"}, shapeValue))
        background.shapeSet = parseBackgroundShapeSet(shapeValue);

    std::string symmetryValue;
    if (readJsonAliases(j, {"symmetry", "mirror"}, symmetryValue))
        background.symmetry = parseBackgroundSymmetry(symmetryValue);

    readJsonAliases(j, {"count", "shapeCount", "shape_count", "shapesCount", "shapes_count"}, background.shapeCount);
    readJsonAliases(j, {"gridColumns", "grid_columns", "columns"}, background.gridColumns);
    readJsonAliases(j, {"gridRows", "grid_rows", "rows"}, background.gridRows);
    readJsonAliases(j, {"imageOpacity", "image_opacity"}, background.imageOpacity);
    readJsonAliases(j, {"opacity", "shapeOpacity", "shape_opacity"}, background.opacity);
    readJsonAliases(j, {"wobble", "drift"}, background.wobble);
    readJsonAliases(j, {"rotationSpeed", "rotation_speed", "spin", "spinSpeed", "spin_speed"}, background.rotationSpeed);
    readJsonAliases(j, {"fixedOrientation", "fixed_orientation", "lockOrientation", "lock_orientation"}, background.fixedOrientation);
    readJsonAliases(j, {"roundness", "cornerRoundness", "corner_roundness", "shapeRoundness", "shape_roundness"}, background.cornerRoundness);

    float orientationDegrees = background.orientationDegrees;
    if (readJsonAliases(j, {"orientation", "orientationDegrees", "orientation_degrees", "shapeOrientation", "shape_orientation", "angle"}, orientationDegrees)) {
        background.orientationDegrees = orientationDegrees;
        background.fixedOrientation = true;
    }

    readFloatPair(j, {"spacing", "gap"}, background.spacingX, background.spacingY);
    readFloatPair(j, {"size", "sizeRange", "size_range", "shapeSize", "shape_size"}, background.sizeMin, background.sizeMax);
    readFloatPair(j, {"speed", "speedRange", "speed_range", "motionSpeed", "motion_speed"}, background.speedMin, background.speedMax);

    auto imageIt = j.find("image");
    if (imageIt != j.end() && !imageIt->is_null()) {
        try {
            if (imageIt->is_string()) {
                background.imagePath = imageIt->get<std::string>();
            } else if (imageIt->is_object()) {
                readJsonAliases(*imageIt, {"path", "file", "src"}, background.imagePath);
                readJsonAliases(*imageIt, {"opacity", "alpha"}, background.imageOpacity);
                readJsonAliases(*imageIt, {"cover", "fill"}, background.imageCover);

                std::string fit;
                if (readJsonAliases(*imageIt, {"fit", "mode"}, fit))
                    background.imageCover = lowerString(trimString(fit)) != "contain";
            }
        } catch (...) {
        }
    }

    auto gridIt = findObjectAlias(j, {"grid"});
    if (gridIt) {
        readJsonAliases(*gridIt, {"columns", "x"}, background.gridColumns);
        readJsonAliases(*gridIt, {"rows", "y"}, background.gridRows);
        readFloatPair(*gridIt, {"spacing", "gap"}, background.spacingX, background.spacingY);
    }

    auto motionIt = findObjectAlias(j, {"motion", "animation"});
    if (motionIt) {
        readFloatPair(*motionIt, {"speed", "speedRange", "speed_range"}, background.speedMin, background.speedMax);
        readJsonAliases(*motionIt, {"wobble", "drift"}, background.wobble);
        readJsonAliases(*motionIt, {"rotation", "rotationSpeed", "rotation_speed", "spin"}, background.rotationSpeed);
    }

    background.shapeCount = std::max(1, background.shapeCount);
    background.gridColumns = std::max(1, background.gridColumns);
    background.gridRows = std::max(1, background.gridRows);
    background.spacingX = std::max(1.f, background.spacingX);
    background.spacingY = std::max(1.f, background.spacingY);
    background.sizeMin = std::max(1.f, background.sizeMin);
    background.sizeMax = std::max(background.sizeMin, background.sizeMax);
    background.speedMin = std::max(0.f, background.speedMin);
    background.speedMax = std::max(background.speedMin, background.speedMax);
    background.wobble = std::max(0.f, background.wobble);
    background.imageOpacity = std::clamp(background.imageOpacity, 0.f, 1.f);
    background.opacity = std::clamp(background.opacity, 0.f, 1.f);
    background.cornerRoundness = std::clamp(background.cornerRoundness, 0.f, 1.f);
}

void readThemeBackground(const nlohmann::json& j, ThemeBackgroundConfig& background) {
    readThemeBackgroundFromObject(j, background);

    auto backgroundIt = j.find("background");
    if (backgroundIt != j.end() && backgroundIt->is_object())
        readThemeBackgroundFromObject(*backgroundIt, background);

    auto themeIt = j.find("theme");
    if (themeIt != j.end() && themeIt->is_object()) {
        readThemeBackgroundFromObject(*themeIt, background);

        auto themeBackgroundIt = themeIt->find("background");
        if (themeBackgroundIt != themeIt->end() && themeBackgroundIt->is_object())
            readThemeBackgroundFromObject(*themeBackgroundIt, background);
    }
}

void readThemeFontConfigFromValue(const nlohmann::json& value, ThemeFontConfig& fonts) {
    try {
        if (value.is_string()) {
            std::string path = value.get<std::string>();
            fonts.regularPath = path;
            if (fonts.smallPath.empty())
                fonts.smallPath = path;
            return;
        }

        if (!value.is_object())
            return;

        std::string sharedPath;
        if (readJsonAliases(value, {"path", "font", "regular", "normal", "ui"}, sharedPath)) {
            fonts.regularPath = sharedPath;
            if (fonts.smallPath.empty())
                fonts.smallPath = sharedPath;
        }

        readJsonAliases(value, {"regularPath", "regular_path", "uiPath", "ui_path"}, fonts.regularPath);
        readJsonAliases(value, {"small", "smallPath", "small_path", "secondary", "caption"}, fonts.smallPath);

        if (fonts.smallPath.empty() && !fonts.regularPath.empty())
            fonts.smallPath = fonts.regularPath;
    } catch (...) {
    }
}

void readThemeFonts(const nlohmann::json& j, ThemeFontConfig& fonts) {
    auto fontIt = j.find("font");
    if (fontIt != j.end() && !fontIt->is_null())
        readThemeFontConfigFromValue(*fontIt, fonts);

    auto fontsIt = j.find("fonts");
    if (fontsIt != j.end() && !fontsIt->is_null())
        readThemeFontConfigFromValue(*fontsIt, fonts);

    auto themeIt = j.find("theme");
    if (themeIt != j.end() && themeIt->is_object()) {
        auto themeFontIt = themeIt->find("font");
        if (themeFontIt != themeIt->end() && !themeFontIt->is_null())
            readThemeFontConfigFromValue(*themeFontIt, fonts);

        auto themeFontsIt = themeIt->find("fonts");
        if (themeFontsIt != themeIt->end() && !themeFontsIt->is_null())
            readThemeFontConfigFromValue(*themeFontsIt, fonts);
    }
}

void readThemeIconsFromValue(const nlohmann::json& value, ThemeIconConfig& icons) {
    try {
        if (value.is_string()) {
            icons.basePath = value.get<std::string>();
            return;
        }

        if (!value.is_object())
            return;

        readJsonAliases(value, {"path", "base", "basePath", "base_path", "directory", "dir"}, icons.basePath);
    } catch (...) {
    }
}

void readThemeIcons(const nlohmann::json& j, ThemeIconConfig& icons) {
    auto iconsIt = j.find("icons");
    if (iconsIt != j.end() && !iconsIt->is_null())
        readThemeIconsFromValue(*iconsIt, icons);

    auto themeIt = j.find("theme");
    if (themeIt != j.end() && themeIt->is_object()) {
        auto themeIconsIt = themeIt->find("icons");
        if (themeIconsIt != themeIt->end() && !themeIconsIt->is_null())
            readThemeIconsFromValue(*themeIconsIt, icons);
    }
}

void readThemeColorsFromObject(const nlohmann::json& j, ThemeColorSet& colors) {
    readJsonAliases(j, {"cursorH", "cursor_h"}, colors.cursorH);
    readJsonAliases(j, {"cursorS", "cursor_s"}, colors.cursorS);
    readJsonAliases(j, {"cursorL", "cursor_l"}, colors.cursorL);
    readJsonAliases(j, {"accentH", "accent_h"}, colors.accentH);
    readJsonAliases(j, {"accentS", "accent_s"}, colors.accentS);
    readJsonAliases(j, {"accentL", "accent_l"}, colors.accentL);
    readJsonAliases(j, {"bgH", "bg_h", "backgroundH", "background_h"}, colors.bgH);
    readJsonAliases(j, {"bgS", "bg_s", "backgroundS", "background_s"}, colors.bgS);
    readJsonAliases(j, {"bgL", "bg_l", "backgroundL", "background_l"}, colors.bgL);
    readJsonAliases(j, {"bgAccH", "bg_acc_h", "backgroundAccentH", "background_accent_h"}, colors.bgAccH);
    readJsonAliases(j, {"bgAccS", "bg_acc_s", "backgroundAccentS", "background_accent_s"}, colors.bgAccS);
    readJsonAliases(j, {"bgAccL", "bg_acc_l", "backgroundAccentL", "background_accent_l"}, colors.bgAccL);
    readJsonAliases(j, {"shapeH", "shape_h", "shapesH", "shapes_h"}, colors.shapeH);
    readJsonAliases(j, {"shapeS", "shape_s", "shapesS", "shapes_s"}, colors.shapeS);
    readJsonAliases(j, {"shapeL", "shape_l", "shapesL", "shapes_l"}, colors.shapeL);

    readHslTriplet(j, {"cursor", "cursorAccent", "cursor_accent"},
                   colors.cursorH, colors.cursorS, colors.cursorL);
    readHslTriplet(j, {"accent", "primaryAccent"},
                   colors.accentH, colors.accentS, colors.accentL);
    readHslTriplet(j, {"background", "bg"},
                   colors.bgH, colors.bgS, colors.bgL);
    readHslTriplet(j, {"backgroundAccent", "bgAccent", "background_accent"},
                   colors.bgAccH, colors.bgAccS, colors.bgAccL);
    readHslTriplet(j, {"shapes", "shape", "shapeColor", "floatingShapes"},
                   colors.shapeH, colors.shapeS, colors.shapeL);
}

void readThemeColors(const nlohmann::json& j, ThemeColorSet& colors) {
    readThemeColorsFromObject(j, colors);

    auto colorsIt = j.find("colors");
    if (colorsIt != j.end() && colorsIt->is_object())
        readThemeColorsFromObject(*colorsIt, colors);

    auto themeIt = j.find("theme");
    if (themeIt != j.end() && themeIt->is_object()) {
        readThemeColorsFromObject(*themeIt, colors);

        auto themeColorsIt = themeIt->find("colors");
        if (themeColorsIt != themeIt->end() && themeColorsIt->is_object())
            readThemeColorsFromObject(*themeColorsIt, colors);
    }
}

bool loadJsonFile(const std::string& path, nlohmann::json& j) {
    std::ifstream f(path);
    if (!f.is_open())
        return false;

    try {
        f >> j;
        return true;
    } catch (...) {
        return false;
    }
}

bool loadThemePresetFromManifest(const std::string& manifestPath,
                                 ThemePresetSource source,
                                 const std::string& defaultName,
                                 nxui::ThemeMode defaultMode,
                                 const std::string& installPath,
                                 ThemePreset& preset) {
    nlohmann::json j;
    if (!loadJsonFile(manifestPath, j))
        return false;

    const char* idPrefix = (source == ThemePresetSource::InstalledPackage) ? "package:" : "builtin:";

    preset = ThemePreset{};
    preset.name = defaultName;
    preset.id = makeThemeId(idPrefix, defaultName);
    preset.builtIn = (source == ThemePresetSource::BuiltIn);
    preset.source = source;
    preset.installPath = installPath;
    preset.soundPreset = kDefaultSharedSoundPreset;

    std::string mode = (defaultMode == nxui::ThemeMode::Light) ? "light" : "dark";
    readJsonAliases(j, {"name", "title", "displayName", "display_name"}, preset.name);
    readJsonAliases(j, {"author", "creator", "by", "artist", "designer"}, preset.author);
    readJsonAliases(j, {"version", "themeVersion", "theme_version"}, preset.version);

    std::string rawId;
    if (readJsonAliases(j, {"id", "slug", "themeId", "theme_id"}, rawId))
        preset.id = normalizeThemeId(idPrefix, rawId);
    readJsonAliases(j, {"mode", "themeMode", "theme_mode", "variant"}, mode);
    readJsonAliases(j, {"soundPreset", "audioPreset", "audio_preset"}, preset.soundPreset);

    auto themeIt = j.find("theme");
    if (themeIt != j.end() && themeIt->is_object())
        readJsonAliases(*themeIt, {"mode", "themeMode", "theme_mode", "variant"}, mode);

    auto audioIt = j.find("audio");
    bool bundledAudio = false;
    if (audioIt != j.end() && audioIt->is_object()) {
        readJsonAliases(*audioIt, {"preset", "soundPreset", "sound_preset"}, preset.soundPreset);
        readJsonAliases(*audioIt, {"bundled", "useBundled", "use_bundled"}, bundledAudio);
    }

    if (preset.id.empty())
        preset.id = makeThemeId(idPrefix, defaultName);
    if (preset.name.empty())
        preset.name = defaultName;

    if (preset.soundPreset == "bundled" || preset.soundPreset == "theme" || preset.soundPreset == "package") {
        preset.soundPreset = (source == ThemePresetSource::InstalledPackage)
            ? preset.id
            : std::string(kDefaultSharedSoundPreset);
    }

    if (source == ThemePresetSource::InstalledPackage
        && ((preset.soundPreset.empty() && hasBundledAudio(installPath)) || bundledAudio)) {
        preset.soundPreset = preset.id;
    }

    if (preset.soundPreset.empty())
        preset.soundPreset = kDefaultSharedSoundPreset;

    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });

    preset.mode = (mode == "light") ? nxui::ThemeMode::Light : nxui::ThemeMode::Dark;
    preset.colors = ThemePreset::extractColors(
        preset.mode == nxui::ThemeMode::Light ? nxui::Theme::light() : nxui::Theme::dark());
    readThemeColors(j, preset.colors);
    readThemeBackground(j, preset.background);
    readThemeFonts(j, preset.fonts);
    readThemeIcons(j, preset.icons);
    return true;
}

ThemePreset makeLegacyBuiltInPreset(const char* name, nxui::ThemeMode mode) {
    ThemePreset preset;
    preset.id = makeThemeId("builtin:", name);
    preset.name = name;
    preset.author = "SwitchU";
    preset.mode = mode;
    preset.colors = ThemePreset::extractColors(
        mode == nxui::ThemeMode::Light ? nxui::Theme::light() : nxui::Theme::dark());
    preset.builtIn = true;
    preset.source = ThemePresetSource::BuiltIn;
    preset.soundPreset = kDefaultSharedSoundPreset;
    return preset;
}

} // namespace

nxui::Theme ThemePreset::toTheme() const {
    nxui::Theme t = (mode == nxui::ThemeMode::Dark)
                        ? nxui::Theme::dark()
                        : nxui::Theme::light();

    const bool hasExplicitCursor = colors.cursorH >= 0.f && colors.cursorS >= 0.f && colors.cursorL >= 0.f;
    const float cursorH = hasExplicitCursor ? colors.cursorH : colors.accentH;
    const float cursorS = hasExplicitCursor ? colors.cursorS : colors.accentS;
    const float cursorL = hasExplicitCursor ? colors.cursorL : colors.accentL;

    t.cursorNormal     = nxui::Color::fromHSL(cursorH, cursorS, cursorL, 1.f);
    float glowAlpha    = (mode == nxui::ThemeMode::Dark) ? 0.12f : 0.15f;
    t.cursorGlow       = t.cursorNormal.withAlpha(glowAlpha);

    t.background       = nxui::Color::fromHSL(colors.bgH, colors.bgS, colors.bgL, 1.f);
    t.backgroundAccent = nxui::Color::fromHSL(colors.bgAccH, colors.bgAccS, colors.bgAccL, 1.f);
    t.shapeColor       = nxui::Color::fromHSL(colors.shapeH, colors.shapeS, colors.shapeL, 0.10f);

    return t;
}

ThemeColorSet ThemePreset::extractColors(const nxui::Theme& theme) {
    ThemeColorSet c;
    theme.cursorNormal.toHSL(c.cursorH, c.cursorS, c.cursorL);
    theme.cursorNormal.toHSL(c.accentH, c.accentS, c.accentL);
    theme.background.toHSL(c.bgH, c.bgS, c.bgL);
    theme.backgroundAccent.toHSL(c.bgAccH, c.bgAccS, c.bgAccL);
    theme.shapeColor.toHSL(c.shapeH, c.shapeS, c.shapeL);
    return c;
}

static std::vector<ThemePreset> makeBuiltInPresets() {
    std::vector<ThemePreset> v;

    static const struct {
        const char* dirName;
        nxui::ThemeMode mode;
    } defs[] = {
        {"Default Dark", nxui::ThemeMode::Dark},
        {"Default Light", nxui::ThemeMode::Light},
    };

    for (const auto& def : defs) {
        ThemePreset preset;
        std::string themeDir = std::string(kBuiltInThemesDir) + "/" + def.dirName;
        std::string manifestPath = themeDir + "/theme.json";
        if (!loadThemePresetFromManifest(manifestPath,
                                         ThemePresetSource::BuiltIn,
                                         def.dirName,
                                         def.mode,
                                         themeDir,
                                         preset)) {
            preset = makeLegacyBuiltInPreset(def.dirName, def.mode);
        }
        v.push_back(std::move(preset));
    }

    return v;
}

const std::vector<ThemePreset>& ThemePreset::builtInPresets() {
    static std::vector<ThemePreset> presets = makeBuiltInPresets();
    return presets;
}

static constexpr const char* kUserPresetsPath = "sdmc:/config/SwitchU/theme_presets.ini";
static constexpr const char* kInstalledThemesDir = "sdmc:/config/SwitchU/themes";

std::vector<ThemePreset> ThemePreset::loadUserPresets() {
    std::vector<ThemePreset> result;
    std::ifstream f(kUserPresetsPath);
    if (!f.is_open()) return result;

    ThemePreset current;
    bool hasSection = false;

    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();

        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            if (hasSection)
                result.push_back(current);

            current = ThemePreset{};
            current.name = line.substr(1, line.size() - 2);
            current.id = makeThemeId("user:", current.name);
            current.builtIn = false;
            current.source = ThemePresetSource::UserPreset;
            current.soundPreset = kDefaultSharedSoundPreset;
            hasSection = true;
            continue;
        }

        if (!hasSection) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        try {
            if      (key == "mode")      current.mode = (val == "light") ? nxui::ThemeMode::Light : nxui::ThemeMode::Dark;
            else if (key == "cursor_h")  current.colors.cursorH  = std::stof(val);
            else if (key == "cursor_s")  current.colors.cursorS  = std::stof(val);
            else if (key == "cursor_l")  current.colors.cursorL  = std::stof(val);
            else if (key == "accent_h")  current.colors.accentH  = std::stof(val);
            else if (key == "accent_s")  current.colors.accentS  = std::stof(val);
            else if (key == "accent_l")  current.colors.accentL  = std::stof(val);
            else if (key == "bg_h")      current.colors.bgH      = std::stof(val);
            else if (key == "bg_s")      current.colors.bgS      = std::stof(val);
            else if (key == "bg_l")      current.colors.bgL      = std::stof(val);
            else if (key == "bg_acc_h")  current.colors.bgAccH   = std::stof(val);
            else if (key == "bg_acc_s")  current.colors.bgAccS   = std::stof(val);
            else if (key == "bg_acc_l")  current.colors.bgAccL   = std::stof(val);
            else if (key == "shape_h")   current.colors.shapeH   = std::stof(val);
            else if (key == "shape_s")   current.colors.shapeS   = std::stof(val);
            else if (key == "shape_l")   current.colors.shapeL   = std::stof(val);
        } catch (...) {}
    }

    if (hasSection)
        result.push_back(current);

    return result;
}

std::vector<ThemePreset> ThemePreset::loadInstalledPackages() {
    std::vector<ThemePreset> result;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kInstalledThemesDir, ec)) {
        if (ec)
            break;
        if (!entry.is_directory(ec)) {
            ec.clear();
            continue;
        }

        std::string dirName = entry.path().filename().string();
        std::string installDir = entry.path().string();

        std::string manifestPath = installDir + "/theme.json";
        ThemePreset preset;
        if (!loadThemePresetFromManifest(manifestPath,
                                         ThemePresetSource::InstalledPackage,
                                         dirName,
                                         nxui::ThemeMode::Dark,
                                         installDir,
                                         preset)) {
            continue;
        }
        result.push_back(std::move(preset));
    }

    std::sort(result.begin(), result.end(), [](const ThemePreset& lhs, const ThemePreset& rhs) {
        return lhs.name < rhs.name;
    });
    return result;
}

bool ThemePreset::saveUserPresets(const std::vector<ThemePreset>& presets) {
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory("sdmc:/config/SwitchU", ec);

    std::ofstream f(kUserPresetsPath, std::ios::trunc);
    if (!f.is_open()) return false;

    f << std::fixed << std::setprecision(4);
    for (auto& p : presets) {
        const float cursorH = p.colors.cursorH >= 0.f ? p.colors.cursorH : p.colors.accentH;
        const float cursorS = p.colors.cursorS >= 0.f ? p.colors.cursorS : p.colors.accentS;
        const float cursorL = p.colors.cursorL >= 0.f ? p.colors.cursorL : p.colors.accentL;
        f << '[' << p.name << "]\n";
        f << "mode=" << (p.mode == nxui::ThemeMode::Light ? "light" : "dark") << '\n';
        f << "cursor_h=" << cursorH << '\n';
        f << "cursor_s=" << cursorS << '\n';
        f << "cursor_l=" << cursorL << '\n';
        f << "accent_h=" << p.colors.accentH << '\n';
        f << "accent_s=" << p.colors.accentS << '\n';
        f << "accent_l=" << p.colors.accentL << '\n';
        f << "bg_h=" << p.colors.bgH << '\n';
        f << "bg_s=" << p.colors.bgS << '\n';
        f << "bg_l=" << p.colors.bgL << '\n';
        f << "bg_acc_h=" << p.colors.bgAccH << '\n';
        f << "bg_acc_s=" << p.colors.bgAccS << '\n';
        f << "bg_acc_l=" << p.colors.bgAccL << '\n';
        f << "shape_h=" << p.colors.shapeH << '\n';
        f << "shape_s=" << p.colors.shapeS << '\n';
        f << "shape_l=" << p.colors.shapeL << "\n\n";
    }

    return static_cast<bool>(f);
}
