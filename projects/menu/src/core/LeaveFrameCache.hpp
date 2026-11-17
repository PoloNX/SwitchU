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
    // Captured HOME chrome — resume recaptures when these diverge.
    std::uint32_t appLayoutMode = 0;     // AppLayoutMode
    std::uint32_t homeOrientation = 0;  // HomeOrientation
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
// Last Lock view paint — used when wake relaunches into LockScreen so the
// crossfade matches Lock instead of the stale HOME grid capture.
bool save(const LeaveFrameSession& session,
          const std::uint8_t* rgba, int width, int height);

bool saveToPath(const char* path, const LeaveFrameSession& session,
                const std::uint8_t* rgba, int width, int height,
                bool frost = true);

/// Soft-blur + light chroma (HOME leave / Lock wake covers).
/// radius≈2 suits 640×360; use ~4 for full 1280×720 so softness matches.
void frostRgbaInPlace(std::uint8_t* rgba, int width, int height, int radius = 2);

bool load(LeaveFrameSession& session, LeaveFrameImage& image);

bool loadFromPath(const char* path, LeaveFrameSession& session, LeaveFrameImage& image);

bool loadSessionOnly(LeaveFrameSession& session);

} // namespace LeaveFrameCache
