#include "InstallStamp.hpp"

#include "core/DebugLog.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

namespace themeshop {
namespace {

std::string joinPath(const std::string& base, const std::string& relative) {
    if (base.empty())
        return relative;
    if (relative.empty())
        return base;
    if (base.back() == '/' || base.back() == '\\')
        return base + relative;
    return base + "/" + relative;
}

std::int64_t fileMtime(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0)
        return 0;
    return static_cast<std::int64_t>(st.st_mtime);
}

std::string iso8601Utc(std::int64_t unixSeconds) {
    std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tm {};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32] {};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

} // namespace

bool writeInstallStamp(const std::string& directory) {
    if (directory.empty())
        return false;

    const auto now = std::chrono::system_clock::now();
    const std::int64_t unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    nlohmann::json root = {
        {"installedAt", iso8601Utc(unixSeconds)},
        {"installedAtUnix", unixSeconds},
    };

    const std::string path = joinPath(directory, kInstallStampFile);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        DebugLog::log("[install-stamp] write failed path=%s", path.c_str());
        return false;
    }
    out << root.dump(2);
    out << '\n';
    return static_cast<bool>(out);
}

std::int64_t resolveSortStamp(const std::string& directory, const char* preferredRelative) {
    if (directory.empty())
        return 0;

    const std::string stampPath = joinPath(directory, kInstallStampFile);
    try {
        std::ifstream in(stampPath, std::ios::binary);
        if (in) {
            nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
            if (root.is_object()) {
                if (root.contains("installedAtUnix") && root["installedAtUnix"].is_number_integer())
                    return root["installedAtUnix"].get<std::int64_t>();
                if (root.contains("installedAtUnix") && root["installedAtUnix"].is_number())
                    return static_cast<std::int64_t>(root["installedAtUnix"].get<double>());
            }
        }
    } catch (...) {
        // Fall through to mtime.
    }

    if (preferredRelative && preferredRelative[0] != '\0') {
        const std::int64_t preferred = fileMtime(joinPath(directory, preferredRelative));
        if (preferred != 0)
            return preferred;
    }

    // Common manifest names when caller did not specify one.
    for (const char* name : {"theme.json", "pack.json", "icons.json"}) {
        const std::int64_t stamp = fileMtime(joinPath(directory, name));
        if (stamp != 0)
            return stamp;
    }

    return fileMtime(directory);
}

} // namespace themeshop
