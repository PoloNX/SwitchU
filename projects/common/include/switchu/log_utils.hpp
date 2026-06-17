#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <sys/time.h>
#include <system_error>
#include <vector>

namespace switchu::log_detail {

inline void ensure_log_dir(const char* log_dir) {
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory(log_dir, ec);
}

inline bool read_local_time(std::tm& local_time, long& milliseconds, long& tenths) {
    timeval now{};
    if (::gettimeofday(&now, nullptr) != 0) {
        std::time_t fallback = std::time(nullptr);
        if (fallback == static_cast<std::time_t>(-1) || !::localtime_r(&fallback, &local_time))
            return false;

        milliseconds = 0;
        tenths = 0;
        return true;
    }

    std::time_t seconds = static_cast<std::time_t>(now.tv_sec);
    if (!::localtime_r(&seconds, &local_time))
        return false;

    milliseconds = now.tv_usec / 1000L;
    tenths = now.tv_usec / 100000L;
    return true;
}

inline void format_archive_timestamp(char* buffer, size_t size) {
    std::tm local_time{};
    long milliseconds = 0;
    long tenths = 0;
    if (!read_local_time(local_time, milliseconds, tenths)) {
        std::snprintf(buffer, size, "0000-00-00_00-00-00-000");
        return;
    }

    std::snprintf(buffer, size,
                  "%04d-%02d-%02d_%02d-%02d-%02d-%03ld",
                  local_time.tm_year + 1900,
                  local_time.tm_mon + 1,
                  local_time.tm_mday,
                  local_time.tm_hour,
                  local_time.tm_min,
                  local_time.tm_sec,
                  milliseconds);
}

inline void format_line_timestamp(char* buffer, size_t size) {
    std::tm local_time{};
    long milliseconds = 0;
    long tenths = 0;
    if (!read_local_time(local_time, milliseconds, tenths)) {
        std::snprintf(buffer, size, "0000-00-00 00:00:00.0");
        return;
    }

    std::snprintf(buffer, size,
                  "%04d-%02d-%02d %02d:%02d:%02d.%01ld",
                  local_time.tm_year + 1900,
                  local_time.tm_mon + 1,
                  local_time.tm_mday,
                  local_time.tm_hour,
                  local_time.tm_min,
                  local_time.tm_sec,
                  tenths);
}

inline void build_current_log_path(char* path, size_t size, const char* log_dir, const char* base_name, const char* extension) {
    std::snprintf(path, size, "%s/%s%s", log_dir, base_name, extension);
}

inline void build_archived_log_path(char* path, size_t size, const char* log_dir, const char* base_name, const char* extension) {
    char timestamp[32];
    format_archive_timestamp(timestamp, sizeof(timestamp));
    std::snprintf(path, size, "%s/%s-%s%s", log_dir, base_name, timestamp, extension);
}

inline bool has_archived_log_name(const char* file_name, const char* base_name, const char* extension) {
    char prefix[128];
    std::snprintf(prefix, sizeof(prefix), "%s-", base_name);

    const size_t prefix_len = std::strlen(prefix);
    const size_t file_len = std::strlen(file_name);
    const size_t extension_len = std::strlen(extension);

    return file_len > prefix_len + 4
        && std::strncmp(file_name, prefix, prefix_len) == 0
        && std::strcmp(file_name + file_len - extension_len, extension) == 0;
}

inline void prune_archived_logs(const char* log_dir, const char* base_name, const char* extension, size_t keep_count) {
    std::vector<std::string> matches;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(log_dir, ec)) {
        if (ec)
            break;

        const std::string file_name = entry.path().filename().string();
        if (has_archived_log_name(file_name.c_str(), base_name, extension))
            matches.emplace_back(file_name);
    }

    if (matches.size() <= keep_count)
        return;

    std::sort(matches.begin(), matches.end());

    char path[256];
    const size_t delete_count = matches.size() - keep_count;
    for (size_t i = 0; i < delete_count; ++i) {
        std::snprintf(path, sizeof(path), "%s/%s", log_dir, matches[i].c_str());
        ec.clear();
        std::filesystem::remove(path, ec);
    }
}

inline bool rotate_current_log(const char* log_dir, const char* base_name, const char* extension, size_t keep_count) {
    char current_path[256];
    build_current_log_path(current_path, sizeof(current_path), log_dir, base_name, extension);

    bool can_truncate = true;
    std::error_code ec;
    if (std::filesystem::exists(current_path, ec)) {
        char archived_path[256];
        build_archived_log_path(archived_path, sizeof(archived_path), log_dir, base_name, extension);
        ec.clear();
        std::filesystem::rename(current_path, archived_path, ec);
        can_truncate = !ec;
    }

    prune_archived_logs(log_dir, base_name, extension, keep_count);
    return can_truncate;
}

} // namespace switchu::log_detail
