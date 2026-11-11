#include "HomebrewStore.hpp"

#include "DebugLog.hpp"
#include <switchu/sd_commit.hpp>
#include <switch.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace switchu::homebrew {
namespace {

std::string normalizedName(std::string name) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
        name.erase(name.begin());
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
        name.pop_back();
    if (name.size() > 64) name.resize(64);
    return name;
}

} // namespace

bool HomebrewStore::load() {
    m_entries.clear();
    m_nextId = 1;
    std::ifstream file(kPath);
    if (!file.is_open()) {
        DebugLog::log("[homebrew] no store yet");
        return true;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& error) {
        DebugLog::log("[homebrew] parse failed: %s", error.what());
        return false;
    }

    const auto entries = root.find("homebrew");
    if (entries == root.end() || !entries->is_array())
        return true;

    for (const auto& item : *entries) {
        if (!item.is_object()) continue;
        HomebrewEntry entry;
        try {
            entry.id = item.value("id", 0u);
            entry.name = normalizedName(item.value("name", std::string()));
            entry.path = item.value("path", std::string());
        } catch (...) {
            continue;
        }
        if (entry.id == 0 || entry.name.empty() || entry.path.empty())
            continue;
        m_entries.push_back(std::move(entry));
        for (const auto& e : m_entries)
            m_nextId = std::max(m_nextId, e.id + 1);
    }
    DebugLog::log("[homebrew] loaded count=%d", static_cast<int>(m_entries.size()));
    return true;
}

bool HomebrewStore::save() const {
    std::error_code ec;
    std::filesystem::create_directories("sdmc:/config/SwitchU", ec);

    nlohmann::json root;
    root["version"] = 1;
    root["homebrew"] = nlohmann::json::array();
    for (const auto& entry : m_entries) {
        nlohmann::json item;
        item["id"] = entry.id;
        item["name"] = entry.name;
        item["path"] = entry.path;
        root["homebrew"].push_back(std::move(item));
    }

    {
        std::ofstream file(kTempPath, std::ios::trunc);
        if (!file.is_open()) {
            DebugLog::log("[homebrew] save open temp failed");
            return false;
        }
        file << root.dump(2) << '\n';
        file.flush();
        if (!file.good()) {
            DebugLog::log("[homebrew] save write temp failed");
            return false;
        }
    }
    if (!switchu::commitSdCard("homebrew temp")) {
        DebugLog::log("[homebrew] temp commit failed");
        return false;
    }

    const bool hadOriginal = std::filesystem::is_regular_file(kPath, ec) && !ec;
    ec.clear();
    if (hadOriginal) {
        std::filesystem::remove(kBackupPath, ec);
        ec.clear();
        std::filesystem::rename(kPath, kBackupPath, ec);
        if (ec) {
            DebugLog::log("[homebrew] backup rename failed ec=%d", ec.value());
            return false;
        }
    }

    ec.clear();
    std::filesystem::rename(kTempPath, kPath, ec);
    if (ec) {
        DebugLog::log("[homebrew] commit rename failed ec=%d", ec.value());
        if (hadOriginal) {
            std::error_code restoreError;
            std::filesystem::rename(kBackupPath, kPath, restoreError);
        }
        fsdevCommitDevice("sdmc");
        return false;
    }

    if (!switchu::commitSdCard("homebrew final")) {
        DebugLog::log("[homebrew] final commit failed");
        std::filesystem::remove(kPath, ec);
        if (hadOriginal) {
            ec.clear();
            std::filesystem::rename(kBackupPath, kPath, ec);
        }
        fsdevCommitDevice("sdmc");
        return false;
    }

    if (hadOriginal) {
        std::filesystem::remove(kBackupPath, ec);
        fsdevCommitDevice("sdmc");
    }
    DebugLog::log("[homebrew] saved count=%d", static_cast<int>(m_entries.size()));
    return true;
}

std::uint32_t HomebrewStore::create(std::string name, std::string path) {
    name = normalizedName(std::move(name));
    if (name.empty() || path.empty() || m_entries.size() >= 256)
        return 0;
    const std::uint32_t id = m_nextId++;
    HomebrewEntry entry;
    entry.id = id;
    entry.name = std::move(name);
    entry.path = std::move(path);
    m_entries.push_back(std::move(entry));
    return id;
}

bool HomebrewStore::remove(std::uint32_t id) {
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const HomebrewEntry& e) { return e.id == id; });
    if (it == m_entries.end()) return false;
    m_entries.erase(it);
    return true;
}

HomebrewEntry* HomebrewStore::find(std::uint32_t id) {
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const HomebrewEntry& e) { return e.id == id; });
    return it == m_entries.end() ? nullptr : &*it;
}

const HomebrewEntry* HomebrewStore::find(std::uint32_t id) const {
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const HomebrewEntry& e) { return e.id == id; });
    return it == m_entries.end() ? nullptr : &*it;
}

namespace {
struct NroAssetInfo {
    std::size_t assetOffset = 0;
    std::uint64_t iconOffset = 0, iconSize = 0;
    std::uint64_t nacpOffset = 0, nacpSize = 0;
};

bool findNroAsset(const std::vector<std::uint8_t>& data, NroAssetInfo& info) {
    for (std::size_t i = 0; i + 48 <= data.size(); ++i) {
        if (data[i] != 0x41 || data[i + 1] != 0x53 ||
            data[i + 2] != 0x45 || data[i + 3] != 0x54)
            continue;
        std::uint32_t magic = 0, version = 0;
        std::memcpy(&magic, data.data() + i, 4);
        std::memcpy(&version, data.data() + i + 4, 4);
        if (magic != 0x54455341u || version != 0)
            continue;
        std::uint64_t iconOff, iconSz, nacpOff, nacpSz;
        std::memcpy(&iconOff, data.data() + i + 8, 8);
        std::memcpy(&iconSz, data.data() + i + 16, 8);
        std::memcpy(&nacpOff, data.data() + i + 24, 8);
        std::memcpy(&nacpSz, data.data() + i + 32, 8);
        if (iconOff + iconSz > data.size() - i) continue;
        if (nacpOff + nacpSz > data.size() - i) continue;
        if (nacpSz < 0x200) continue;
        info.assetOffset = i;
        info.iconOffset = iconOff;
        info.iconSize = iconSz;
        info.nacpOffset = nacpOff;
        info.nacpSize = nacpSz;
        return true;
    }
    return false;
}

std::vector<std::uint8_t> readWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return {};
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0 || size > 128 * 1024 * 1024) return {};
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file.good() && !file.eof()) return {};
    return data;
}
} // namespace

std::string readHomebrewName(const std::string& path) {
    const auto data = readWholeFile(path);
    NroAssetInfo info;
    if (!findNroAsset(data, info)) return {};
    const std::size_t start = info.assetOffset + static_cast<std::size_t>(info.nacpOffset);
    if (start + 0x200 > data.size()) return {};
    std::string name(reinterpret_cast<const char*>(data.data() + start), 0x200);
    const auto nul = name.find('\0');
    if (nul != std::string::npos) name.resize(nul);
    // trim
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
        name.erase(name.begin());
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
        name.pop_back();
    return name;
}

std::vector<std::uint8_t> readHomebrewIcon(const std::string& path) {
    const auto data = readWholeFile(path);
    NroAssetInfo info;
    if (!findNroAsset(data, info)) return {};
    if (info.iconSize == 0) return {};
    const std::size_t start = info.assetOffset + static_cast<std::size_t>(info.iconOffset);
    if (start + info.iconSize > data.size()) return {};
    return std::vector<std::uint8_t>(data.begin() + start,
                                     data.begin() + start + static_cast<std::size_t>(info.iconSize));
}

} // namespace switchu::homebrew