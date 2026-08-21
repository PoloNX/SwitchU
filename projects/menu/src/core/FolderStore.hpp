#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace switchu::folders {

struct Folder {
    std::uint32_t id = 0;
    std::string name;
    std::vector<std::uint64_t> titleIds;
    int colorIndex = 0;
    int sizeIndex = 1;
    int pageCount = 1;

    std::size_t titleCount() const;
};

inline constexpr int kMaxFolderPages = 8;

class FolderStore final {
public:
    static constexpr const char* kPath = "sdmc:/config/SwitchU/folders.json";
    static constexpr const char* kTempPath = "sdmc:/config/SwitchU/folders.tmp";
    static constexpr const char* kBackupPath = "sdmc:/config/SwitchU/folders.bak";

    bool load();
    bool save() const;

    const std::vector<Folder>& all() const { return m_folders; }
    Folder* find(std::uint32_t id);
    const Folder* find(std::uint32_t id) const;
    std::uint32_t create(std::string name);
    bool rename(std::uint32_t id, std::string name);
    bool remove(std::uint32_t id);
    bool addTitle(std::uint32_t folderId, std::uint64_t titleId);
    bool placeTitle(std::uint32_t folderId, std::uint64_t titleId, std::size_t index);
    bool removeTitle(std::uint32_t folderId, std::uint64_t titleId);
    bool setColorIndex(std::uint32_t folderId, int colorIndex);
    bool setSizeIndex(std::uint32_t folderId, int sizeIndex);
    bool setPageCount(std::uint32_t folderId, int pages);
    std::uint32_t folderForTitle(std::uint64_t titleId) const;

private:
    static std::string normalizedName(std::string name);
    std::vector<Folder> m_folders;
    std::uint32_t m_nextId = 1;
};

} // namespace switchu::folders
