#pragma once

#include <cstdint>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace switchu::usb {

// Starts the background MTP responder thread so PC sees the Switch SD card as a drive.
void mtpStart();

// Stops the server and releases the USB interface so games / applets have exclusive USB access.
void mtpStop();

// Checks if the console is actively connected to a USB host.
bool mtpConnected();

enum class MtpOp { None, Receive, Send, Delete };

struct MtpStatus {
    bool     connected = false;
    MtpOp    op = MtpOp::None;
    uint64_t done = 0;
    uint64_t total = 0;
    uint64_t bytes = 0;
    uint32_t received = 0;
    uint32_t sent = 0;
    uint32_t deleted = 0;
    char     name[128] = {};
};

void mtpGetStatus(MtpStatus& out);

} // namespace switchu::usb
