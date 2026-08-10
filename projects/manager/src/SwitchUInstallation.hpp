#pragma once

#include <string>

namespace switchu::manager {

enum class InstallationState {
    Enabled,
    Disabled,
    Missing,
    Conflict,
};

enum class ToggleError {
    None,
    InvalidState,
    MissingMenuPayload,
    RenameFailed,
    VerificationFailed,
    CommitFailed,
    RollbackFailed,
    RebootFailed,
};

struct InstallationSnapshot {
    InstallationState state = InstallationState::Missing;
    bool menuPayloadPresent = false;
};

struct ToggleResult {
    bool success = false;
    ToggleError error = ToggleError::None;
    int detail = 0;
    InstallationSnapshot snapshot;
};

// SwitchU is deployed as an Atmosphere ExeFS override for qlaunch. The exact
// filename determines what Atmosphere loads on the next boot. Renaming within
// the same directory keeps the operation targeted and recoverable; user
// configuration, themes, profiles and the external menu payload are untouched.
class SwitchUInstallation {
public:
    static constexpr const char* kActiveOverride =
        "sdmc:/atmosphere/contents/0100000000001000/exefs.nsp";
    static constexpr const char* kDisabledOverride =
        "sdmc:/atmosphere/contents/0100000000001000/exefs.nsp.disabled";
    static constexpr const char* kMenuMain =
        "sdmc:/switch/SwitchU/bin/menu/main";
    static constexpr const char* kMenuNpdm =
        "sdmc:/switch/SwitchU/bin/menu/main.npdm";

    InstallationSnapshot inspect() const;
    ToggleResult setEnabled(bool enabled) const;
};

} // namespace switchu::manager
