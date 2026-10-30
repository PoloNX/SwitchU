#pragma once

#include "ThemeTransferState.hpp"

#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace nxui {
class ThreadPool;
}

class ThemeCatalogClient {
public:
    struct Entry {
        std::string id;
        std::string name;
        std::string author;
        std::string version;
        std::string path;
        std::string manifest;
        std::string cover;
        std::vector<std::string> screenshots;
        bool hasIcons = false;
        bool hasMusic = false;
        bool hasSounds = false;
    };

    // Icon pack published in the same catalog (index.json -> "icons").
    struct IconEntry {
        std::string id;
        std::string name;
        std::string author;
        std::string version;
        std::string description;
        std::string path;      // repo folder, e.g. icons/default
        std::string manifest;  // pack.json inside that folder
        std::string cover;     // pack preview image
        bool builtin = false;  // also ships inside the console
        bool colour = false;   // colour artwork (spectrum badge + tint UI)
        std::string theme;     // theme folder this pack was extracted from
    };

    struct Snapshot {
        ThemeTransferState loading;
        std::vector<Entry> entries;
        std::vector<IconEntry> icons;
        std::uint64_t revision = 0;
    };

    static constexpr const char* kDefaultCatalogUrl = "https://raw.githubusercontent.com/PoloNX/SwitchU-Themes/main/index.json";

    explicit ThemeCatalogClient(std::string catalogUrl = kDefaultCatalogUrl);

    void setCatalogUrl(std::string catalogUrl);
    const std::string& catalogUrl() const {
        return m_catalogUrl;
    }

    void refresh(nxui::ThreadPool& pool);
    bool poll();
    Snapshot snapshot() const;

private:
    static Snapshot loadCatalog(const std::string& url);

    std::string m_catalogUrl;
    mutable std::mutex m_mutex;
    ThemeTransferState m_loading;
    std::vector<Entry> m_entries;
    std::vector<IconEntry> m_icons;
    std::future<void> m_future;
    std::uint64_t m_revision = 0;
};