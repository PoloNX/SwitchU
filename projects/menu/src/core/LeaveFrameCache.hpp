#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Persists a half-resolution snapshot of HOME for fast AppletReturn paint,
/// plus lightweight session state (page / folder / focus) so the live UI can
/// land on the same view as the splash.
struct LeaveFrameSession {
    bool valid = false;
    int page = 0;
    std::uint32_t openFolderId = 0;
    std::uint64_t focusTitleId = 0;
};

struct LeaveFrameImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool valid() const {
        return width > 0 && height > 0
            && rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

namespace LeaveFrameCache {

inline constexpr const char* kPath = "sdmc:/config/SwitchU/leave_frame.bin";

bool save(const LeaveFrameSession& session,
          const std::uint8_t* rgba, int width, int height);

bool load(LeaveFrameSession& session, LeaveFrameImage& image);

bool loadSessionOnly(LeaveFrameSession& session);

} // namespace LeaveFrameCache
