#include "IconPackInstaller.hpp"

#include "IconPackManager.hpp"
#include "core/DebugLog.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "themeshop/ThemePackageInstaller.hpp"

#include <nxui/core/I18n.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace switchu::icons {
namespace {

namespace fs = std::filesystem;

bool isSafePackId(const std::string& id) {
    if (id.empty() || id == "." || id == "..")
        return false;
    return std::none_of(id.begin(), id.end(), [](char c) {
        return c == '/' || c == '\\' || c == ':';
    });
}

std::string trimSlashes(std::string path) {
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    return path;
}

std::string joinPath(const std::string& base, const std::string& relative) {
    if (base.empty())
        return relative;
    if (relative.empty())
        return base;
    if (base.back() == '/')
        return base + relative;
    return base + "/" + relative;
}

std::string fileUrlFor(const std::string& catalogUrl, const std::string& relativePath) {
    const std::size_t slash = catalogUrl.find_last_of('/');
    if (slash == std::string::npos)
        return trimSlashes(relativePath);
    return catalogUrl.substr(0, slash + 1) + trimSlashes(relativePath);
}

bool ensureDirectoryRecursive(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

void writeFileBinary(const std::string& path, const std::string& data) {
    const std::size_t slash = path.find_last_of('/');
    if (slash != std::string::npos)
        ensureDirectoryRecursive(path.substr(0, slash));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Failed to open file for writing: " + path);
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output)
        throw std::runtime_error("Failed to write file: " + path);
}

// Fallback when the GitHub tree API is unavailable: trust pack.json "glyphs".
std::vector<ThemePackageInstaller::RemoteFile> filesFromManifest(
    const std::string& catalogUrl, const ThemeCatalogClient::IconEntry& entry) {
    std::vector<ThemePackageInstaller::RemoteFile> files;
    if (entry.manifest.empty())
        return files;

    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(
            themeshop::http::getText(fileUrlFor(catalogUrl, entry.manifest)));
    } catch (const std::exception& ex) {
        DebugLog::log("[icon-install] manifest read failed pack=%s: %s",
                      entry.id.c_str(), ex.what());
        return files;
    }

    auto addFile = [&](const std::string& fileName) {
        const std::string clean = trimSlashes(fileName);
        if (clean.empty())
            return;
        files.push_back({clean, fileUrlFor(catalogUrl, joinPath(entry.path, clean))});
    };

    if (auto glyphsIt = manifest.find("glyphs"); glyphsIt != manifest.end() && glyphsIt->is_array()) {
        for (const auto& glyph : *glyphsIt) {
            if (glyph.is_string())
                addFile(glyph.get<std::string>());
        }
    }
    addFile("preview.png");
    addFile("pack.json");
    return files;
}

} // namespace

std::string IconPackInstaller::destinationRootFor(const std::string& packId) {
    return joinPath(kInstalledPacksDir, packId);
}

IconPackInstaller::Result IconPackInstaller::install(
    const std::string& catalogUrl,
    const ThemeCatalogClient::IconEntry& entry,
    ProgressCallback onProgress) {
    Result result;
    result.packId = entry.id;
    auto& i18n = nxui::I18n::instance();

    if (!isSafePackId(entry.id))
        throw std::runtime_error("Invalid icon pack id: " + entry.id);

    result.destinationPath = destinationRootFor(entry.id);

    if (onProgress)
        onProgress(i18n.tr("themeshop.icons.prepare_download", "Preparing icon pack download..."),
                   0.f);

    std::vector<ThemePackageInstaller::RemoteFile> files =
        ThemePackageInstaller::listRemoteFolder(catalogUrl, entry.path);
    if (files.empty())
        files = filesFromManifest(catalogUrl, entry);
    if (files.empty())
        throw std::runtime_error("Icon pack contains no downloadable files");

    DebugLog::log("[icon-install] pack=%s files=%zu root=%s",
                  entry.id.c_str(), files.size(), entry.path.c_str());

    std::error_code ec;
    fs::remove_all(result.destinationPath, ec);
    if (!ensureDirectoryRecursive(result.destinationPath))
        throw std::runtime_error("Failed to prepare destination: " + result.destinationPath);

    int downloaded = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        if (onProgress) {
            onProgress(i18n.tr("themeshop.transfer.downloading_file", "Downloading")
                           + " " + file.relativePath + " (" + std::to_string(i + 1)
                           + "/" + std::to_string(files.size()) + ")",
                       files.size() > 1 ? static_cast<float>(i) / static_cast<float>(files.size())
                                        : 0.f);
        }

        const std::string body = themeshop::http::getText(file.downloadUrl);
        writeFileBinary(joinPath(result.destinationPath, file.relativePath), body);
        ++downloaded;
    }

    if (downloaded == 0) {
        fs::remove_all(result.destinationPath, ec);
        throw std::runtime_error("Icon pack download produced no files");
    }

    if (onProgress)
        onProgress(i18n.tr("themeshop.icons.finalize_download", "Finishing icon pack install..."),
                   1.f);

    result.success = true;
    result.message = i18n.tr("themeshop.icons.installed", "Icon pack installed.");
    return result;
}

bool IconPackInstaller::remove(const std::string& packId) {
    if (!isSafePackId(packId))
        return false;

    const std::string path = destinationRootFor(packId);
    std::error_code ec;
    if (!fs::exists(path, ec))
        return false;
    fs::remove_all(path, ec);
    if (ec) {
        DebugLog::log("[icon-install] remove failed pack=%s: %s",
                      packId.c_str(), ec.message().c_str());
        return false;
    }
    DebugLog::log("[icon-install] removed pack=%s", packId.c_str());
    return true;
}

} // namespace switchu::icons
