#include "FolderStore.hpp"

#include "DebugLog.hpp"
#include <switchu/sd_commit.hpp>
#include <switch.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace switchu::folders {
namespace {

std::string titleIdString(std::uint64_t titleId) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setw(16) << std::setfill('0')
           << titleId;
    return stream.str();
}

bool parseTitleId(const nlohmann::json& value, std::uint64_t& titleId) {
    try {
        if (value.is_string()) {
            const std::string text = value.get<std::string>();
            std::size_t consumed = 0;
            titleId = std::stoull(text, &consumed, 16);
            return consumed == text.size() && titleId != 0;
        }
        if (value.is_number_unsigned()) {
            titleId = value.get<std::uint64_t>();
            return titleId != 0;
        }
    } catch (...) {
    }
    return false;
}

} // namespace

std::string FolderStore::normalizedName(std::string name) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
        name.erase(name.begin());
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
        name.pop_back();
    if (name.size() > 48) {
        std::size_t cursor = 0;
        std::size_t safeEnd = 0;
        while (cursor < name.size() && cursor < 48) {
            const unsigned char lead = static_cast<unsigned char>(name[cursor]);
            std::size_t width = 1;
            if ((lead & 0xE0u) == 0xC0u) width = 2;
            else if ((lead & 0xF0u) == 0xE0u) width = 3;
            else if ((lead & 0xF8u) == 0xF0u) width = 4;
            if (cursor + width > name.size() || cursor + width > 48)
                break;
            safeEnd = cursor + width;
            cursor += width;
        }
        name.resize(safeEnd);
    }
    return name;
}

bool FolderStore::load() {
    m_folders.clear();
    m_nextId = 1;
    std::ifstream file(kPath);
    if (!file.is_open()) {
        DebugLog::log("[folders] no store yet");
        return true;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& error) {
        DebugLog::log("[folders] parse failed: %s", error.what());
        return false;
    }

    std::unordered_set<std::uint32_t> usedIds;
    std::unordered_set<std::uint64_t> assignedTitles;
    const auto entries = root.find("folders");
    if (entries == root.end() || !entries->is_array())
        return true;

    for (const auto& item : *entries) {
        if (!item.is_object())
            continue;
        Folder folder;
        try {
            folder.id = item.value("id", 0u);
            folder.name = normalizedName(item.value("name", std::string()));
        } catch (...) {
            continue;
        }
        if (folder.id == 0 || folder.name.empty() || usedIds.count(folder.id))
            continue;
        try {
            folder.colorIndex = std::clamp(
                item.value("color", static_cast<int>(folder.id % 8u)), 0, 7);
            folder.sizeIndex = std::clamp(item.value("size", 1), 0, 2);
        } catch (...) {
            folder.colorIndex = static_cast<int>(folder.id % 8u);
            folder.sizeIndex = 1;
        }
        usedIds.insert(folder.id);
        m_nextId = std::max(m_nextId, folder.id + 1);

        const auto titles = item.find("titles");
        if (titles != item.end() && titles->is_array()) {
            for (const auto& value : *titles) {
                std::uint64_t titleId = 0;
                if (parseTitleId(value, titleId) && !assignedTitles.count(titleId)) {
                    assignedTitles.insert(titleId);
                    folder.titleIds.push_back(titleId);
                }
            }
        }
        m_folders.push_back(std::move(folder));
    }
    DebugLog::log("[folders] loaded count=%d", static_cast<int>(m_folders.size()));
    return true;
}

bool FolderStore::save() const {
    std::error_code ec;
    std::filesystem::create_directories("sdmc:/config/SwitchU", ec);

    nlohmann::json root;
    root["version"] = 2;
    root["folders"] = nlohmann::json::array();
    for (const auto& folder : m_folders) {
        nlohmann::json item;
        item["id"] = folder.id;
        item["name"] = folder.name;
        item["color"] = folder.colorIndex;
        item["size"] = folder.sizeIndex;
        item["titles"] = nlohmann::json::array();
        for (std::uint64_t titleId : folder.titleIds)
            item["titles"].push_back(titleIdString(titleId));
        root["folders"].push_back(std::move(item));
    }

    {
        std::ofstream file(kTempPath, std::ios::trunc);
        if (!file.is_open()) {
            DebugLog::log("[folders] save open temp failed");
            return false;
        }
        file << root.dump(2) << '\n';
        file.flush();
        if (!file.good()) {
            DebugLog::log("[folders] save write temp failed");
            return false;
        }
    }
    if (!switchu::commitSdCard("folders temp")) {
        DebugLog::log("[folders] temp commit failed");
        return false;
    }

    const bool hadOriginal = std::filesystem::is_regular_file(kPath, ec) && !ec;
    ec.clear();
    if (hadOriginal) {
        std::filesystem::remove(kBackupPath, ec);
        ec.clear();
        std::filesystem::rename(kPath, kBackupPath, ec);
        if (ec) {
            DebugLog::log("[folders] backup rename failed ec=%d", ec.value());
            return false;
        }
    }

    ec.clear();
    std::filesystem::rename(kTempPath, kPath, ec);
    if (ec) {
        DebugLog::log("[folders] commit rename failed ec=%d", ec.value());
        if (hadOriginal) {
            std::error_code restoreError;
            std::filesystem::rename(kBackupPath, kPath, restoreError);
        }
        fsdevCommitDevice("sdmc");
        return false;
    }

    if (!switchu::commitSdCard("folders final")) {
        DebugLog::log("[folders] final commit failed");
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
    DebugLog::log("[folders] saved count=%d", static_cast<int>(m_folders.size()));
    return true;
}

Folder* FolderStore::find(std::uint32_t id) {
    auto it = std::find_if(m_folders.begin(), m_folders.end(),
                           [id](const Folder& folder) { return folder.id == id; });
    return it == m_folders.end() ? nullptr : &*it;
}

const Folder* FolderStore::find(std::uint32_t id) const {
    auto it = std::find_if(m_folders.begin(), m_folders.end(),
                           [id](const Folder& folder) { return folder.id == id; });
    return it == m_folders.end() ? nullptr : &*it;
}

std::uint32_t FolderStore::create(std::string name) {
    name = normalizedName(std::move(name));
    if (name.empty() || m_folders.size() >= 64)
        return 0;
    const std::uint32_t id = m_nextId++;
    Folder folder;
    folder.id = id;
    folder.name = std::move(name);
    folder.colorIndex = static_cast<int>(id % 8u);
    folder.sizeIndex = 1;
    m_folders.push_back(std::move(folder));
    return id;
}

bool FolderStore::rename(std::uint32_t id, std::string name) {
    Folder* folder = find(id);
    name = normalizedName(std::move(name));
    if (!folder || name.empty())
        return false;
    folder->name = std::move(name);
    return true;
}

bool FolderStore::remove(std::uint32_t id) {
    auto it = std::find_if(m_folders.begin(), m_folders.end(),
                           [id](const Folder& folder) { return folder.id == id; });
    if (it == m_folders.end())
        return false;
    m_folders.erase(it);
    return true;
}

bool FolderStore::addTitle(std::uint32_t folderId, std::uint64_t titleId) {
    if (titleId == 0)
        return false;
    Folder* folder = find(folderId);
    if (!folder)
        return false;
    const std::uint32_t previous = folderForTitle(titleId);
    if (previous == folderId)
        return true;
    if (previous != 0)
        removeTitle(previous, titleId);
    folder->titleIds.push_back(titleId);
    return true;
}

bool FolderStore::placeTitle(std::uint32_t folderId, std::uint64_t titleId,
                             std::size_t index) {
    if (titleId == 0)
        return false;
    Folder* target = find(folderId);
    if (!target)
        return false;

    const std::uint32_t previous = folderForTitle(titleId);
    if (previous != 0) {
        Folder* source = find(previous);
        if (source) {
            auto it = std::find(source->titleIds.begin(), source->titleIds.end(), titleId);
            if (it != source->titleIds.end())
                source->titleIds.erase(it);
        }
    }

    target = find(folderId);
    if (!target)
        return false;
    index = std::min(index, target->titleIds.size());
    target->titleIds.insert(target->titleIds.begin() + static_cast<std::ptrdiff_t>(index),
                            titleId);
    return true;
}

bool FolderStore::removeTitle(std::uint32_t folderId, std::uint64_t titleId) {
    Folder* folder = find(folderId);
    if (!folder)
        return false;
    auto it = std::find(folder->titleIds.begin(), folder->titleIds.end(), titleId);
    if (it == folder->titleIds.end())
        return false;
    folder->titleIds.erase(it);
    return true;
}

bool FolderStore::setColorIndex(std::uint32_t folderId, int colorIndex) {
    Folder* folder = find(folderId);
    if (!folder)
        return false;
    folder->colorIndex = std::clamp(colorIndex, 0, 7);
    return true;
}

bool FolderStore::setSizeIndex(std::uint32_t folderId, int sizeIndex) {
    Folder* folder = find(folderId);
    if (!folder)
        return false;
    folder->sizeIndex = std::clamp(sizeIndex, 0, 2);
    return true;
}

std::uint32_t FolderStore::folderForTitle(std::uint64_t titleId) const {
    for (const auto& folder : m_folders) {
        if (std::find(folder.titleIds.begin(), folder.titleIds.end(), titleId)
                != folder.titleIds.end())
            return folder.id;
    }
    return 0;
}

} // namespace switchu::folders
