#pragma once
#include <switch.h>
#include <cstdint>


namespace switchu::ns {


struct ExtApplicationRecord {
    uint64_t id;
    uint8_t  last_event;
    uint8_t  attributes;
    uint8_t  reserved[6];
    uint64_t last_updated;
};
static_assert(sizeof(ExtApplicationRecord) == sizeof(NsApplicationRecord));

enum AppViewFlag : uint32_t {
    AppViewFlag_IsValid              = (1u << 0),
    AppViewFlag_HasMainContents      = (1u << 1),
    AppViewFlag_HasContentsInstalled = (1u << 4),
    AppViewFlag_IsDownloading        = (1u << 5),
    AppViewFlag_IsGameCard           = (1u << 6),
    AppViewFlag_IsGameCardInserted   = (1u << 7),
    AppViewFlag_CanLaunch            = (1u << 8),
    AppViewFlag_NeedsUpdate          = (1u << 9),
    AppViewFlag_NeedsVerify          = (1u << 13),
};


struct ExtApplicationView {
    uint64_t app_id;
    uint32_t used32_x8;
    uint32_t flags;
    uint64_t download_progress_current;
    uint64_t download_progress_total;
    uint32_t used32_x20;
    uint8_t  used8_x24;
    uint8_t  download_has_eta;
    uint8_t  used8_x26;
    uint8_t  unk_x27;
    uint64_t used64_x28;
    uint64_t other_progress_current;
    uint64_t other_progress_total;
    uint32_t used32_x40;
    uint8_t  used8_x44;
    uint8_t  used8_x45;
    uint8_t  unk_x46[2];
    uint64_t used64_x48;
};
static_assert(sizeof(ExtApplicationView) == 0x50);

inline constexpr bool viewHasFlag(const ExtApplicationView& view, uint32_t flag) {
    return (view.flags & flag) != 0;
}

inline constexpr bool viewHasFlag(uint32_t flags, uint32_t flag) {
    return (flags & flag) != 0;
}

inline Result queryApplicationViews(const uint64_t* titleIds, int count,
                                    ExtApplicationView* outViews) {
    return nsGetApplicationView(
        reinterpret_cast<NsApplicationView*>(outViews),
        titleIds, count);
}

inline bool applicationNeedsUpdate(uint64_t titleId) {
    Result rc = nsCheckApplicationLaunchVersion(titleId);
    return rc == (Result)0x80A0A;
}

inline Result getLastGameCardMountFailure() {
    return nsGetLastGameCardMountFailureResult();
}

}
