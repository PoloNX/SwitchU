#include "SwitchUInstallation.hpp"

#include <switchu/file_log.hpp>
#include <switch.h>

#include <cerrno>
#include <filesystem>
#include <system_error>

namespace switchu::manager {
namespace {

bool regularFileExists(const char* path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

ToggleResult failure(ToggleError error, int detail,
                     const SwitchUInstallation& installation) {
    return {false, error, detail, installation.inspect()};
}

} // namespace

InstallationSnapshot SwitchUInstallation::inspect() const {
    const bool active = regularFileExists(kActiveOverride);
    const bool disabled = regularFileExists(kDisabledOverride);

    InstallationSnapshot snapshot;
    snapshot.menuPayloadPresent = regularFileExists(kMenuMain)
        && regularFileExists(kMenuNpdm);

    if (active && disabled)
        snapshot.state = InstallationState::Conflict;
    else if (active)
        snapshot.state = InstallationState::Enabled;
    else if (disabled)
        snapshot.state = InstallationState::Disabled;
    else
        snapshot.state = InstallationState::Missing;
    return snapshot;
}

ToggleResult SwitchUInstallation::setEnabled(bool enabled) const {
    const InstallationSnapshot before = inspect();
    const InstallationState required = enabled
        ? InstallationState::Disabled
        : InstallationState::Enabled;
    if (before.state != required) {
        switchu::FileLog::log("[toggle] refused target=%s state=%u",
                              enabled ? "enabled" : "disabled",
                              static_cast<unsigned>(before.state));
        return failure(ToggleError::InvalidState, 0, *this);
    }
    if (enabled && !before.menuPayloadPresent) {
        switchu::FileLog::log("[toggle] refused enable: external menu payload missing");
        return failure(ToggleError::MissingMenuPayload, 0, *this);
    }

    std::string source = enabled ? kDisabledOverride : kActiveOverride;
    std::string destination = enabled ? kActiveOverride : kDisabledOverride;
    std::error_code ec;
    std::filesystem::rename(source, destination, ec);
    if (ec) {
        switchu::FileLog::log("[toggle] rename FAIL target=%s ec=%d",
                              enabled ? "enabled" : "disabled", ec.value());
        return failure(ToggleError::RenameFailed, ec.value(), *this);
    }

    const InstallationSnapshot renamed = inspect();
    const InstallationState expected = enabled
        ? InstallationState::Enabled
        : InstallationState::Disabled;
    if (renamed.state != expected) {
        switchu::FileLog::log("[toggle] verification FAIL expected=%u actual=%u",
                              static_cast<unsigned>(expected),
                              static_cast<unsigned>(renamed.state));
        ec.clear();
        std::filesystem::rename(destination, source, ec);
        fsdevCommitDevice("sdmc");
        return failure(ec ? ToggleError::RollbackFailed
                          : ToggleError::VerificationFailed,
                       ec.value(), *this);
    }

    const Result commitResult = fsdevCommitDevice("sdmc");
    if (R_FAILED(commitResult)) {
        switchu::FileLog::log("[toggle] SD commit FAIL rc=0x%X; rolling back",
                              commitResult);
        ec.clear();
        std::filesystem::rename(destination, source, ec);
        const Result rollbackCommit = fsdevCommitDevice("sdmc");
        if (ec || R_FAILED(rollbackCommit)) {
            const int detail = ec ? ec.value() : static_cast<int>(rollbackCommit);
            switchu::FileLog::log(
                "[toggle] rollback FAIL ec=%d commit=0x%X",
                ec.value(), rollbackCommit);
            return failure(ToggleError::RollbackFailed, detail, *this);
        }
        return failure(ToggleError::CommitFailed,
                       static_cast<int>(commitResult), *this);
    }

    switchu::FileLog::log("[toggle] success target=%s",
                          enabled ? "enabled" : "disabled");
    return {true, ToggleError::None, 0, inspect()};
}

} // namespace switchu::manager
