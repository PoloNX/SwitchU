#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace switchu::homebrew {

std::string readHomebrewName(const std::string& path);
std::vector<std::uint8_t> readHomebrewIcon(const std::string& path);

inline constexpr std::uint64_t kHomebrewTitleIdPrefix = 0xFB00000000000000ULL;

inline constexpr std::uint64_t homebrewTitleId(std::uint32_t id) {
    return kHomebrewTitleIdPrefix | static_cast<std::uint64_t>(id);
}

inline constexpr bool isHomebrewTitleId(std::uint64_t value) {
    return (value & 0xFFFFFFFF00000000ULL) == kHomebrewTitleIdPrefix;
}

inline constexpr std::uint32_t homebrewIdFromTitleId(std::uint64_t value) {
    return isHomebrewTitleId(value) ? static_cast<std::uint32_t>(value) : 0;
}

struct HomebrewEntry {
    std::uint32_t id = 0;
    std::string name;
    std::string path;
};

class HomebrewStore final {
public:
    static constexpr const char* kPath = "sdmc:/config/SwitchU/homebrew.json";
    static constexpr const char* kTempPath = "sdmc:/config/SwitchU/homebrew.tmp";
    static constexpr const char* kBackupPath = "sdmc:/config/SwitchU/homebrew.bak";

    bool load();
    bool save() const;

    const std::vector<HomebrewEntry>& all() const { return m_entries; }
    std::vector<HomebrewEntry>& all() { return m_entries; }

    std::uint32_t create(std::string name, std::string path);
    bool remove(std::uint32_t id);
    HomebrewEntry* find(std::uint32_t id);
    const HomebrewEntry* find(std::uint32_t id) const;

private:
    std::vector<HomebrewEntry> m_entries;
    std::uint32_t m_nextId = 1;
};

} // namespace switchu::homebrew