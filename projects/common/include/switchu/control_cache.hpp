#pragma once
#include <switch.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace switchu::control_cache {

inline constexpr const char* kCacheDir = "sdmc:/config/SwitchU/control_cache";
inline constexpr uint32_t kMetaMagic = 0x53554343;
inline constexpr uint32_t kMetaVersion = 1;

struct Meta {
    uint32_t magic = kMetaMagic;
    uint32_t version = kMetaVersion;
    uint64_t title_id = 0;
    uint8_t startup_user_account = 1;
    uint8_t startup_user_account_option = 0;
    uint8_t reserved[6] = {};
    uint64_t save_data_owner_id = 0;
    uint64_t user_account_save_data_size = 0;
    uint64_t user_account_save_data_journal_size = 0;
    uint64_t device_save_data_size = 0;
    uint64_t device_save_data_journal_size = 0;
    uint64_t temporary_storage_size = 0;
    uint64_t cache_storage_size = 0;
    uint64_t cache_storage_journal_size = 0;
    uint64_t bcat_delivery_cache_storage_size = 0;
    char name[0x201] = {};
};

inline std::string formatTitleId(uint64_t titleId) {
    char buf[17] = {};
    std::snprintf(buf, sizeof(buf), "%016lX", static_cast<unsigned long>(titleId));
    return std::string(buf);
}

inline std::string metaPath(uint64_t titleId) {
    return std::string(kCacheDir) + "/" + formatTitleId(titleId) + ".meta";
}

inline std::string iconPath(uint64_t titleId) {
    return std::string(kCacheDir) + "/" + formatTitleId(titleId) + ".jpg";
}

inline void ensureDirectory() {
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory("sdmc:/config/SwitchU", ec);
    ec.clear();
    std::filesystem::create_directory(kCacheDir, ec);
}

inline bool readMeta(uint64_t titleId, Meta& out) {
    std::ifstream file(metaPath(titleId), std::ios::binary);
    if (!file.is_open())
        return false;

    Meta meta{};
    if (!file.read(reinterpret_cast<char*>(&meta), sizeof(meta)))
        return false;

    if (meta.magic != kMetaMagic || meta.version != kMetaVersion || meta.title_id != titleId)
        return false;

    out = meta;
    return true;
}

inline bool hasMeta(uint64_t titleId) {
    Meta meta{};
    return readMeta(titleId, meta);
}

inline std::vector<uint8_t> readIcon(uint64_t titleId) {
    std::vector<uint8_t> data;
    std::ifstream file(iconPath(titleId), std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return data;

    const std::streamoff size = file.tellg();
    if (size <= 0 || size > 0x40000)
        return data;

    file.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
        data.clear();
    return data;
}

inline bool writeIcon(uint64_t titleId, const uint8_t* data, size_t size) {
    if (!data || size == 0)
        return false;

    ensureDirectory();
    std::ofstream file(iconPath(titleId), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(file);
}

inline void copyString(char* dst, size_t dstSize, const char* src, size_t srcSize) {
    if (!dst || dstSize == 0)
        return;

    dst[0] = '\0';
    if (!src || srcSize == 0)
        return;

    size_t len = 0;
    while (len < srcSize && src[len] != '\0')
        ++len;
    len = std::min(dstSize - 1, len);
    std::memcpy(dst, src, len);
    dst[len] = '\0';
}

inline bool fillMetaFromControlData(uint64_t titleId, const NsApplicationControlData& controlData,
                                    Meta& out) {
    Meta meta{};
    meta.title_id = titleId;
    meta.startup_user_account = controlData.nacp.startup_user_account;
    meta.startup_user_account_option = controlData.nacp.startup_user_account_option;
    meta.save_data_owner_id = controlData.nacp.save_data_owner_id;
    meta.user_account_save_data_size = controlData.nacp.user_account_save_data_size;
    meta.user_account_save_data_journal_size = controlData.nacp.user_account_save_data_journal_size;
    meta.device_save_data_size = controlData.nacp.device_save_data_size;
    meta.device_save_data_journal_size = controlData.nacp.device_save_data_journal_size;
    meta.temporary_storage_size = controlData.nacp.temporary_storage_size;
    meta.cache_storage_size = controlData.nacp.cache_storage_size;
    meta.cache_storage_journal_size = controlData.nacp.cache_storage_journal_size;
    meta.bcat_delivery_cache_storage_size = controlData.nacp.bcat_delivery_cache_storage_size;

    NacpLanguageEntry* langEntry = nullptr;
    if (R_FAILED(nacpGetLanguageEntry(const_cast<NacpStruct*>(&controlData.nacp), &langEntry)) ||
        !langEntry || langEntry->name[0] == '\0') {
        langEntry = nullptr;
        for (int i = 0; i < 16; ++i) {
            auto* candidate = const_cast<NacpLanguageEntry*>(&controlData.nacp.lang[i]);
            if (candidate->name[0] != '\0') {
                langEntry = candidate;
                break;
            }
        }
    }

    if (langEntry)
        copyString(meta.name, sizeof(meta.name), langEntry->name, sizeof(langEntry->name));

    if (meta.name[0] == '\0') {
        const std::string fallback = formatTitleId(titleId);
        copyString(meta.name, sizeof(meta.name), fallback.c_str(), fallback.size());
    }

    out = meta;
    return true;
}

inline bool writeMeta(const Meta& meta) {
    ensureDirectory();
    std::ofstream file(metaPath(meta.title_id), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    file.write(reinterpret_cast<const char*>(&meta), sizeof(meta));
    return static_cast<bool>(file);
}

inline bool writeFromControlData(uint64_t titleId, const NsApplicationControlData& controlData,
                                 size_t controlSize) {
    Meta meta{};
    if (!fillMetaFromControlData(titleId, controlData, meta))
        return false;

    const bool metaOk = writeMeta(meta);
    bool iconOk = true;
    if (controlSize > sizeof(NacpStruct)) {
        const size_t iconSize = controlSize - sizeof(NacpStruct);
        iconOk = writeIcon(titleId, controlData.icon, iconSize);
    }
    return metaOk && iconOk;
}

}
