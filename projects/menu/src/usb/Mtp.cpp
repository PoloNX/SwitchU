#include "Mtp.hpp"
#include "core/DebugLog.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace switchu::usb {
namespace {

constexpr const char* kEnableFlag = "sdmc:/config/SwitchU/mtp_enable";

// Menu never links haze (GPLv2). Optional SwitchU-MTP sysmodule watches this
// flag and owns the USB MTP responder for the HOME session only.

void ensureConfigDir() {
    std::error_code ec;
    std::filesystem::create_directories("sdmc:/config/SwitchU", ec);
}

} // namespace

void mtpStart() {
#if defined(__SWITCH__)
    ensureConfigDir();
    FILE* f = std::fopen(kEnableFlag, "wb");
    if (!f) {
        DebugLog::log("[mtp] could not create enable flag (optional SwitchU-MTP sysmodule?)");
        return;
    }
    std::fclose(f);
    DebugLog::log("[mtp] enable flag set (optional sumtp/haze sysmodule)");
#else
    (void)kEnableFlag;
#endif
}

void mtpStop() {
#if defined(__SWITCH__)
    std::error_code ec;
    std::filesystem::remove(kEnableFlag, ec);
    DebugLog::log("[mtp] enable flag cleared");
#endif
}

bool mtpConnected() {
    // Without IPC into the GPL module we cannot see usb:ds state from the menu.
    // Presence of the flag means we *asked* for MTP; PC mount status is opaque.
    std::error_code ec;
    return std::filesystem::is_regular_file(kEnableFlag, ec);
}

void mtpGetStatus(MtpStatus& out) {
    out = {};
    out.connected = mtpConnected();
}

} // namespace switchu::usb
