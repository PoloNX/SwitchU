#include "widgets/LockClockFonts.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <unordered_set>

namespace switchu::lockfonts {
namespace {

namespace fs = std::filesystem;

bool pathExists(const std::string& path) {
    if (path.empty())
        return false;
    std::error_code ec;
    return fs::exists(fs::path(path), ec) && !ec;
}

std::string toLower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWithInsensitive(const std::string& value, const std::string& suffix) {
    if (suffix.size() > value.size())
        return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(value[value.size() - suffix.size() + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

bool isFontFile(const fs::path& path) {
    const std::string ext = toLower(path.extension().string());
    return ext == ".ttf" || ext == ".otf";
}

bool looksLikeIconFont(const std::string& stem) {
    const std::string lower = toLower(stem);
    return lower.find("icon") != std::string::npos
        || lower.find("glyph") != std::string::npos
        || lower.find("symbol") != std::string::npos;
}

std::string stripWeightSuffix(std::string stem) {
    static const char* kSuffixes[] = {
        "-Thin", "-ExtraLight", "-Light", "-Regular", "-Medium",
        "-SemiBold", "-Bold", "-ExtraBold", "-Black",
        "-ThinItalic", "-LightItalic", "-Italic", "-MediumItalic",
        "-SemiBoldItalic", "-BoldItalic", "-BlackItalic",
        "Thin", "ExtraLight", "Light", "Regular", "Medium",
        "SemiBold", "Bold", "ExtraBold", "Black",
    };
    for (const char* suffix : kSuffixes) {
        if (endsWithInsensitive(stem, suffix) && stem.size() > std::strlen(suffix)) {
            stem.resize(stem.size() - std::strlen(suffix));
            while (!stem.empty() && (stem.back() == '-' || stem.back() == '_' || stem.back() == ' '))
                stem.pop_back();
            break;
        }
    }
    return stem.empty() ? std::string("Font") : stem;
}

std::string displayFontName(const fs::path& file) {
    return stripWeightSuffix(file.stem().string());
}

std::string themeLabelSuffix(const ThemePreset& preset) {
    std::string name = preset.name.empty() ? preset.id : preset.name;
    // Prefer a short folder label when the package id is package:<dir>.
    if (name.empty() && !preset.installPath.empty())
        name = fs::path(preset.installPath).filename().string();

    const std::string lower = toLower(name);
    if (lower.find("theme") == std::string::npos)
        name += " theme";
    return name;
}

bool isAbsolutePath(const std::string& path) {
    return (!path.empty() && path.front() == '/')
        || path.find(":/") != std::string::npos;
}

std::string joinPath(const std::string& base, const std::string& rel) {
    if (rel.empty())
        return {};
    if (isAbsolutePath(rel))
        return rel;
    if (base.empty())
        return rel;
    if (base.back() == '/' || base.back() == '\\')
        return base + rel;
    return base + "/" + rel;
}

std::string normalizeSlashes(std::string path) {
    for (char& c : path) {
        if (c == '\\')
            c = '/';
    }
    return path;
}

void addFontCandidate(std::vector<Entry>& out,
                      std::unordered_set<std::string>& seenPaths,
                      const ThemePreset& preset,
                      const std::string& absolutePath) {
    const std::string norm = normalizeSlashes(absolutePath);
    if (norm.empty() || !pathExists(norm))
        return;
    if (looksLikeIconFont(fs::path(norm).stem().string()))
        return;
    if (!seenPaths.insert(toLower(norm)).second)
        return;

    Entry e;
    e.id = "path:" + norm;
    e.label = displayFontName(fs::path(norm)) + " (" + themeLabelSuffix(preset) + ")";
    e.path = norm;
    e.builtin = false;
    out.push_back(std::move(e));
}

void collectDeclaredFonts(std::vector<Entry>& out,
                          std::unordered_set<std::string>& seenPaths,
                          const ThemePreset& preset) {
    if (preset.installPath.empty())
        return;
    for (const std::string& raw : {preset.fonts.regularPath, preset.fonts.smallPath}) {
        if (raw.empty())
            continue;
        addFontCandidate(out, seenPaths, preset, joinPath(preset.installPath, raw));
    }
}

void collectLooseFonts(std::vector<Entry>& out,
                       std::unordered_set<std::string>& seenPaths,
                       const ThemePreset& preset) {
    if (preset.installPath.empty())
        return;

    std::error_code ec;
    const fs::path root(preset.installPath);
    if (!fs::is_directory(root, ec) || ec)
        return;

    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        if (ec)
            break;
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        if (!isFontFile(it->path()))
            continue;
        addFontCandidate(out, seenPaths, preset, it->path().string());
    }
}

} // namespace

std::vector<Entry> builtinEntries() {
    return {
        {"theme", "Theme", "", true},
        {"dejavu", "DejaVu Sans", "DejaVuSans.ttf", true},
        {"outfit", "Outfit", "lock/Outfit-SemiBold.ttf", true},
        {"space_grotesk", "Space Grotesk", "lock/SpaceGrotesk-Medium.ttf", true},
        {"sora", "Sora", "lock/Sora-SemiBold.ttf", true},
        {"manrope", "Manrope", "lock/Manrope-SemiBold.ttf", true},
    };
}

std::vector<Entry> themePackageEntries(const std::vector<ThemePreset>& presets) {
    std::vector<Entry> out;
    std::unordered_set<std::string> seenPaths;

    for (const ThemePreset& preset : presets) {
        if (preset.source != ThemePresetSource::InstalledPackage
            && preset.source != ThemePresetSource::BuiltIn)
            continue;
        if (preset.installPath.empty())
            continue;
        collectDeclaredFonts(out, seenPaths, preset);
        collectLooseFonts(out, seenPaths, preset);
    }

    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return toLower(a.label) < toLower(b.label);
    });
    return out;
}

std::vector<Entry> buildEntries(const std::vector<ThemePreset>& presets) {
    std::vector<Entry> entries = builtinEntries();
    auto themeFonts = themePackageEntries(presets);
    entries.insert(entries.end(),
                   std::make_move_iterator(themeFonts.begin()),
                   std::make_move_iterator(themeFonts.end()));
    return entries;
}

int indexForId(const std::vector<Entry>& entries, const std::string& id) {
    if (entries.empty())
        return 0;
    const std::string key = id.empty() ? std::string("theme") : id;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[i].id == key)
            return i;
    }
    // Orphaned package font after uninstall → fall back to Theme.
    return 0;
}

std::string resolvePath(const std::string& id,
                        const std::vector<Entry>& entries,
                        const std::string& assetsRoot,
                        const std::string& themeFontPath) {
    const std::string key = id.empty() ? std::string("theme") : id;
    const std::string dejavu = assetsRoot + "/fonts/DejaVuSans.ttf";
    const auto fallbackTheme = [&]() {
        return !themeFontPath.empty() ? themeFontPath : dejavu;
    };

    if (key.rfind("path:", 0) == 0) {
        const std::string abs = key.substr(5);
        if (pathExists(abs))
            return abs;
        return fallbackTheme();
    }

    for (const Entry& e : entries) {
        if (e.id != key)
            continue;
        if (!e.builtin) {
            if (pathExists(e.path))
                return e.path;
            return fallbackTheme();
        }
        if (e.path.empty())
            return fallbackTheme();
        const std::string abs = assetsRoot + "/fonts/" + e.path;
        return pathExists(abs) ? abs : dejavu;
    }

    return fallbackTheme();
}

} // namespace switchu::lockfonts
