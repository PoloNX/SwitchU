#pragma once

#include "ThemeCatalogClient.hpp"

#include <functional>
#include <string>
#include <vector>

class ThemePackageInstaller {
public:
    enum class Mode {
        InstallOnly,
        InstallAndApply,
    };

    struct Result {
        bool success = false;
        bool installed = false;
        std::string themeId;
        std::string destinationPath;
        std::string message;
    };

    using ProgressCallback = std::function<void(std::string, float)>;

    // One downloadable file of a catalog folder (icon packs reuse this).
    struct RemoteFile {
        std::string relativePath;
        std::string downloadUrl;
    };

    // Lists every blob under `folderPath` of the GitHub repo backing
    // `catalogUrl`, with paths relative to that folder. Empty when the catalog
    // is not a raw.githubusercontent URL or the listing fails.
    static std::vector<RemoteFile> listRemoteFolder(const std::string& catalogUrl,
                                                    const std::string& folderPath);

    static std::string destinationRootFor(const std::string& themeId, Mode mode);
    static Result run(const std::string& catalogUrl,
                      const ThemeCatalogClient::Entry& entry,
                      Mode mode,
                      ProgressCallback onProgress = {});
};