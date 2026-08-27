#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace switchu::manager {

struct ReleaseInfo {
    std::string version;
    std::string name;
    std::string downloadUrl;
    std::string sha256;
    std::uint64_t downloadSize = 0;
    bool updateAvailable = false;
};

struct UpdateInstallResult {
    bool success = false;
    std::string version;
    std::string error;
};

enum class UpdateWorkerStage : int {
    Idle,
    Downloading,
    Verifying,
    Extracting,
    Installing,
};

class ReleaseUpdater final {
public:
    static constexpr const char* kCurrentVersion = SWITCHU_VERSION;

    static ReleaseInfo checkLatest();
    static UpdateInstallResult install(const ReleaseInfo& release,
                                       bool preserveDisabledOverride,
                                       std::atomic<float>& progress,
                                       std::atomic<int>& stage);
    static void shutdownNetwork();
};

} // namespace switchu::manager
