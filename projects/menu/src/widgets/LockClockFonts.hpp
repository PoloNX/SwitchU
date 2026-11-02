#pragma once

#include "core/ThemePreset.hpp"

#include <string>
#include <vector>

namespace switchu::lockfonts {

struct Entry {
    std::string id;
    std::string label;
    // Absolute path for package fonts. Empty for "theme" (follow active UI face)
    // or relative under assetsRoot/fonts/ for built-ins.
    std::string path;
    bool builtin = false;
};

/// Built-in faces shipped with SwitchU (Theme + DejaVu + lock/…).
std::vector<Entry> builtinEntries();

/// Scan installed theme packages for .ttf/.otf (declared fonts + loose files).
std::vector<Entry> themePackageEntries(const std::vector<ThemePreset>& presets);

/// Built-ins first, then theme fonts sorted by label.
std::vector<Entry> buildEntries(const std::vector<ThemePreset>& presets);

int indexForId(const std::vector<Entry>& entries, const std::string& id);

/// Resolve a picker id to a loadable font path. Missing package fonts fall back
/// to the active theme face, then DejaVu.
std::string resolvePath(const std::string& id,
                        const std::vector<Entry>& entries,
                        const std::string& assetsRoot,
                        const std::string& themeFontPath);

} // namespace switchu::lockfonts
