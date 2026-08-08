#pragma once

#include <switch.h>
#include <cstdio>
#include <cstring>

namespace switchu {

// fsdev writes may have returned while filesystem metadata is still dirty.
// Keep this helper independent from FileLog: the log itself lives on the SD
// card and must not create another write while a power action is being sealed.
inline bool commitSdCard(const char* reason) {
    const Result rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(rc)) {
        char message[112]{};
        std::snprintf(message, sizeof(message),
                      "[sd] commit failed (%s) rc=0x%X",
                      reason ? reason : "unspecified", rc);
        svcOutputDebugString(message, std::strlen(message));
        return false;
    }
    return true;
}

} // namespace switchu
