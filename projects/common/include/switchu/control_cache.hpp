#pragma once
#include <switch.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>
#include <zlib.h>

namespace switchu::control_cache {

inline constexpr const char* kCacheDir = "sdmc:/config/SwitchU/control_cache";
inline constexpr uint32_t kMetaMagic = 0x53554343;
// Version 6: names from compressed (TitlesDataFormat 1) NACPs were garbage
// before; bumping forces those metas to be rebuilt.
inline constexpr uint32_t kMetaVersion = 6;

// HOS 21.0.0+ titles may store their names DEFLATE-compressed
// (TitlesDataFormat 1): a u16 size, then raw DEFLATE data that expands to
// NacpLanguageEntry[32]. libnx's nacpGetLanguageEntry does not decompress it
// and hands back entries that point into the compressed bytes. The title block
// is addressed by raw offset because newer libnx renamed NacpStruct::lang to
// lang_data, and the released libnx that CI builds against still has lang.
inline constexpr size_t kNacpTitleEntryCount = 16;
inline constexpr size_t kNacpDecompressedTitleEntryCount = 32;
inline constexpr size_t kNacpTitlesDataFormatOffset = 0x3215;
inline constexpr size_t kNacpCompressedTitlesCapacity = 0x2FFE;
static_assert(sizeof(NacpStruct) == 0x4000);
static_assert(sizeof(NacpLanguageEntry) == 0x300);

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
    char display_version[0x10] = {};
    char name[0x201] = {};
    char english_name[0x201] = {};
    char publisher[0x101] = {};
};

inline std::string formatTitleId(uint64_t titleId) {
    char buf[17] = {};
    std::snprintf(buf, sizeof(buf), "%016lX", static_cast<unsigned long>(titleId));
    return std::string(buf);
}

inline bool isValidUtf8(const char* value, size_t capacity) {
    if (!value || capacity == 0)
        return false;

    const auto* bytes = reinterpret_cast<const unsigned char*>(value);
    size_t i = 0;
    bool terminated = false;
    while (i < capacity) {
        const unsigned char lead = bytes[i];
        if (lead == 0) {
            terminated = true;
            break;
        }
        if (lead < 0x80) {
            // Reject control bytes other than the whitespace used by titles.
            if (lead < 0x20 && lead != '\t' && lead != '\n' && lead != '\r')
                return false;
            ++i;
            continue;
        }

        size_t continuationCount = 0;
        uint32_t codepoint = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            continuationCount = 1;
            codepoint = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            continuationCount = 2;
            codepoint = lead & 0x0F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            continuationCount = 3;
            codepoint = lead & 0x07;
        } else {
            return false;
        }

        if (i + continuationCount >= capacity)
            return false;
        for (size_t n = 1; n <= continuationCount; ++n) {
            const unsigned char next = bytes[i + n];
            if ((next & 0xC0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }

        const uint32_t minimum = continuationCount == 1 ? 0x80
                               : continuationCount == 2 ? 0x800 : 0x10000;
        if (codepoint < minimum || codepoint > 0x10FFFF
            || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return false;
        }
        i += continuationCount + 1;
    }
    return terminated;
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

    meta.display_version[sizeof(meta.display_version) - 1] = '\0';
    meta.name[sizeof(meta.name) - 1] = '\0';
    meta.english_name[sizeof(meta.english_name) - 1] = '\0';
    meta.publisher[sizeof(meta.publisher) - 1] = '\0';
    if (!isValidUtf8(meta.name, sizeof(meta.name)))
        return false;
    if (meta.english_name[0] != '\0'
        && !isValidUtf8(meta.english_name, sizeof(meta.english_name)))
        meta.english_name[0] = '\0';
    if (meta.publisher[0] != '\0' && !isValidUtf8(meta.publisher, sizeof(meta.publisher)))
        meta.publisher[0] = '\0';
    if (meta.display_version[0] != '\0'
        && !isValidUtf8(meta.display_version, sizeof(meta.display_version))) {
        meta.display_version[0] = '\0';
    }

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

inline NacpLanguageEntry* titleEntries(NacpStruct& nacp) {
    return reinterpret_cast<NacpLanguageEntry*>(&nacp);
}

// Rewrites a compressed title block into the legacy 16-entry form so the
// libnx helpers and the slot scans below read real names. A block that fails
// to decompress is cleared rather than left for those readers to misparse.
inline void normalizeNacpTitles(NacpStruct& nacp) {
    auto* bytes = reinterpret_cast<uint8_t*>(&nacp);
    if (bytes[kNacpTitlesDataFormatOffset] != 1)
        return;

    std::vector<NacpLanguageEntry> decompressed(kNacpDecompressedTitleEntryCount);
    uint16_t compressedSize = 0;
    std::memcpy(&compressedSize, bytes, sizeof(compressedSize));
    bool ok = false;
    if (compressedSize != 0 && compressedSize <= kNacpCompressedTitlesCapacity) {
        z_stream zs{};
        if (inflateInit2(&zs, -15) == Z_OK) {
            zs.next_in = bytes + sizeof(compressedSize);
            zs.avail_in = compressedSize;
            zs.next_out = reinterpret_cast<Bytef*>(decompressed.data());
            zs.avail_out = sizeof(NacpLanguageEntry) * kNacpDecompressedTitleEntryCount;
            const int zr = inflate(&zs, Z_FINISH);
            inflateEnd(&zs);
            ok = zr == Z_STREAM_END || zr == Z_OK;
        }
    }

    std::memset(bytes, 0, sizeof(NacpLanguageEntry) * kNacpTitleEntryCount);
    bytes[kNacpTitlesDataFormatOffset] = 0;
    if (!ok)
        return;

    auto* entries = titleEntries(nacp);
    std::memcpy(entries, decompressed.data(), sizeof(NacpLanguageEntry) * kNacpTitleEntryCount);

    // Slots 16+ (e.g. Polish, Thai) have no legacy slot; surface one only when
    // it is the sole name the title carries.
    for (size_t i = 0; i < kNacpTitleEntryCount; ++i) {
        if (entries[i].name[0] != '\0')
            return;
    }
    for (size_t i = kNacpTitleEntryCount; i < kNacpDecompressedTitleEntryCount; ++i) {
        if (decompressed[i].name[0] != '\0') {
            entries[0] = decompressed[i];
            return;
        }
    }
}

inline bool hasTitleName(const NsApplicationControlData& controlData) {
    auto nacp = std::make_unique<NacpStruct>(controlData.nacp);
    normalizeNacpTitles(*nacp);
    const auto* entries = titleEntries(*nacp);
    for (size_t i = 0; i < kNacpTitleEntryCount; ++i) {
        if (entries[i].name[0] != '\0' && isValidUtf8(entries[i].name, sizeof(entries[i].name)))
            return true;
    }
    return false;
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
    copyString(meta.display_version, sizeof(meta.display_version),
               controlData.nacp.display_version,
               sizeof(controlData.nacp.display_version));

    auto nacp = std::make_unique<NacpStruct>(controlData.nacp);
    normalizeNacpTitles(*nacp);
    const auto* entries = titleEntries(*nacp);

    NacpLanguageEntry* langEntry = nullptr;
    NacpLanguageEntry* preferred = nullptr;
    if (R_SUCCEEDED(nacpGetLanguageEntry(nacp.get(), &preferred))
        && preferred && preferred->name[0] != '\0'
        && isValidUtf8(preferred->name, sizeof(preferred->name))) {
        langEntry = preferred;
    }
    if (!langEntry) {
        for (size_t i = 0; i < kNacpTitleEntryCount; ++i) {
            auto* candidate = const_cast<NacpLanguageEntry*>(&entries[i]);
            if (candidate->name[0] != '\0'
                && isValidUtf8(candidate->name, sizeof(candidate->name))) {
                langEntry = candidate;
                break;
            }
        }
    }

    if (langEntry) {
        copyString(meta.name, sizeof(meta.name), langEntry->name, sizeof(langEntry->name));
        if (isValidUtf8(langEntry->author, sizeof(langEntry->author))) {
            copyString(meta.publisher, sizeof(meta.publisher),
                       langEntry->author, sizeof(langEntry->author));
        }
    }

    // NACP slots 0 and 1 are American and British English. Keep this stable
    // search title independent from the console's display language.
    const NacpLanguageEntry* englishEntry = nullptr;
    for (int languageIndex : {0, 1}) {
        const auto* candidate = &entries[languageIndex];
        if (candidate->name[0] != '\0'
            && isValidUtf8(candidate->name, sizeof(candidate->name))) {
            englishEntry = candidate;
            break;
        }
    }
    if (englishEntry) {
        copyString(meta.english_name, sizeof(meta.english_name),
                   englishEntry->name, sizeof(englishEntry->name));
    }

    if (meta.name[0] == '\0') {
        const std::string fallback = formatTitleId(titleId);
        copyString(meta.name, sizeof(meta.name), fallback.c_str(), fallback.size());
    }
    if (meta.english_name[0] == '\0')
        copyString(meta.english_name, sizeof(meta.english_name),
                   meta.name, sizeof(meta.name));

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
