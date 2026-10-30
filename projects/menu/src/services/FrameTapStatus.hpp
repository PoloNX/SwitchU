#pragma once

#include <cstdint>
#include <switch.h>

namespace switchu::services {

// Polls Frame Tap's `sftap` control service (GetStatus / cmd 0).
// When Frame Tap is not installed, probe() fails quietly and viewerConnected()
// stays false — SwitchU stays on the normal (full) memory path.
class FrameTapStatus {
public:
    // Matches ams::mitm::applet::StreamStatus (64 bytes).
    struct StreamStatus {
        std::uint32_t version = 0;
        std::uint32_t state = 0;       // LiveState
        std::uint32_t enabled = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t fps_x10 = 0;
        std::uint32_t game_fps_x10 = 0;
        std::uint32_t sessions = 0;
        std::uint32_t shot_enabled = 0;
        std::uint32_t shots = 0;
        std::uint32_t shot_fails = 0;  // while NeedHeadroom: requested MB
        std::uint32_t reserved = 0;
        std::uint64_t app_tid = 0;
        std::uint32_t app_excluded = 0;
        std::uint32_t reserved3 = 0;
    };
    static_assert(sizeof(StreamStatus) == 64);

    enum LiveState : std::uint32_t {
        LiveState_Starting = 0,
        LiveState_Off = 1,
        LiveState_WaitViewer = 2,
        LiveState_WaitGame = 3,
        LiveState_Streaming = 4,
        LiveState_Wedged = 5,
        LiveState_NotArmed = 6,
        LiveState_Unsupported = 7,
        LiveState_Excluded = 8,
    };

    // reserved3 bit 2: network viewer present (FT v0.7+).
    static constexpr std::uint32_t kReserved3NetViewer = 4u;
    // reserved3 bit 6: FT needs applet headroom before donor remount.
    static constexpr std::uint32_t kReserved3NeedHeadroom = 64u;

    // Refresh at most every pollIntervalMs. Returns true when status was read.
    bool probe(float dtSeconds, float pollIntervalMs = 500.f);

    bool available() const { return m_available; }
    bool viewerConnected() const { return m_viewerConnected; }
    bool needsHeadroom() const { return m_needsHeadroom; }
    std::uint32_t headroomNeedMb() const { return m_headroomNeedMb; }
    const StreamStatus& status() const { return m_status; }

    // sftap cmd 15 — tell FT the hard reclaim finished (free_kb from svcGetInfo).
    bool ackHeadroom(std::uint32_t freeKb);

    // Soak/debug: treat viewer as connected while this file exists.
    static constexpr const char* kForceLightPath =
        "sdmc:/config/SwitchU/ft_light_force";
    static constexpr const char* kNeedHeadroomFile =
        "sdmc:/config/switch-frame-tap/need_headroom";
    static constexpr const char* kHeadroomReadyFile =
        "sdmc:/config/SwitchU/ft_headroom_ready";

private:
    bool refresh();
    void pollNeedHeadroomFile();

    Service m_service{};
    bool m_serviceOpen = false;
    bool m_available = false;
    bool m_viewerConnected = false;
    bool m_forceLight = false;
    bool m_needsHeadroom = false;
    std::uint32_t m_headroomNeedMb = 0;
    StreamStatus m_status{};
    float m_pollAccumMs = 0.f;
    float m_retryAccumMs = 0.f;
};

} // namespace switchu::services
