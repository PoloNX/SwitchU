#pragma once

#include "themeshop/ThemeCatalogClient.hpp"

#include <functional>
#include <string>

namespace switchu::icons {

// Downloads a catalog icon pack into the SD icon directory
// (sdmc:/switch/SwitchU/icons/<id>/), the same place hand-made packs live.
class IconPackInstaller {
public:
    using ProgressCallback = std::function<void(std::string, float)>;

    struct Result {
        bool success = false;
        std::string packId;
        std::string destinationPath;
        std::string message;
    };

    static std::string destinationRootFor(const std::string& packId);

    static Result install(const std::string& catalogUrl,
                          const ThemeCatalogClient::IconEntry& entry,
                          ProgressCallback onProgress = {});

    // Deletes a downloaded pack. Returns false when the directory is missing or
    // could not be removed.
    static bool remove(const std::string& packId);
};

} // namespace switchu::icons
