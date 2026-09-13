#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace switchu::folders {

inline constexpr int kFolderStyleClassic = 0;
inline constexpr int kFolderStyleSimple = 1;
inline constexpr int kFolderStyleMinimal = 2;
inline constexpr int kFolderStyleTab = 3;
inline constexpr int kFolderStyleRing = 4;
inline constexpr int kFolderStyleManila = 5;
inline constexpr int kFolderStyleLabel = 6;
inline constexpr int kFolderStyleCount = 7;
inline constexpr int kDefaultFolderStyle = kFolderStyleClassic;
inline constexpr int kMaxFolderPages = 8;

inline constexpr std::size_t folderSlotsPerPage(int sizeIndex) {
    switch (sizeIndex) {
        case 0: return 4u * 2u;
        case 2: return 6u * 4u;
        default: return 5u * 3u;
    }
}

inline constexpr std::size_t maxFolderSlots(int sizeIndex) {
    return folderSlotsPerPage(sizeIndex) * static_cast<std::size_t>(kMaxFolderPages);
}

inline bool folderStyleSupportsCover(int styleIndex) {
    return styleIndex != kFolderStyleClassic;
}

inline bool folderShouldShowCover(int styleIndex, bool showCover) {
    return showCover && folderStyleSupportsCover(styleIndex);
}

struct Folder {
    std::uint32_t id = 0;
    std::string name;
    std::vector<std::uint64_t> titleIds;
    int colorIndex = 0;
    int sizeIndex = 1;
    int pageCount = 1;

    std::size_t titleCount() const;
};

inline std::uint64_t firstCoverTitleId(const Folder& folder) {
    for (const std::uint64_t titleId : folder.titleIds) {
        if (titleId != 0)
            return titleId;
    }
    return 0;
}

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
