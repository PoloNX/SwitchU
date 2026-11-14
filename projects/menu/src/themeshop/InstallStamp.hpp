#pragma once

#include <cstdint>
#include <string>

namespace themeshop {

// Sidecar written into theme/icon pack folders on Theme Shop install.
// Existing SD content without this file falls back to file/folder mtime.
inline constexpr const char* kInstallStampFile = ".switchu-installed.json";

bool writeInstallStamp(const std::string& directory);

// Prefer sidecar installedAtUnix, else mtime of preferredRelative (theme.json /
// pack.json), else directory mtime. Returns 0 when nothing is readable.
std::int64_t resolveSortStamp(const std::string& directory,
                              const char* preferredRelative = nullptr);

} // namespace themeshop
