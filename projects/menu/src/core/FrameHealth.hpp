#pragma once

#include <algorithm>
#include <cstdint>

// Rolling frame-time samples for soak diagnosis + art pacing.
// Cheap (fixed ring); no allocations. Call sample() once per menu tick.
// Pass swap-interval-normalized dt (dt / swapInterval) so the engine's idle
// 30 Hz present (~33 ms) does not read as a hitch storm.
struct FrameHealth {
    static constexpr int   kWindow       = 32;
    static constexpr float kSoftMs       = 18.f;  // start throttling art fill-in
    static constexpr float kHitchMs      = 22.f;  // late / hitch frame (~45 fps)
    static constexpr float kLogIntervalS = 5.f;

    void sample(float dtSeconds) {
        if (dtSeconds < 0.f)
            dtSeconds = 0.f;
        // Ignore absurd spikes (debugger pause) so they don't stick the ring.
        const float ms = std::min(dtSeconds * 1000.f, 250.f);

        if (m_count == kWindow && m_samples[m_index] >= kHitchMs && m_hitchInWindow > 0)
            --m_hitchInWindow;

        m_samples[m_index] = ms;
        if (ms >= kHitchMs) {
            ++m_hitchInWindow;
            ++m_totalHitches;
        }

        m_index = (m_index + 1) % kWindow;
        if (m_count < kWindow)
            ++m_count;

        m_logTimer += dtSeconds;
    }

    float averageMs() const {
        if (m_count <= 0)
            return 0.f;
        float sum = 0.f;
        for (int i = 0; i < m_count; ++i)
            sum += m_samples[i];
        return sum / static_cast<float>(m_count);
    }

    float lastMs() const {
        if (m_count <= 0)
            return 0.f;
        const int prev = (m_index + kWindow - 1) % kWindow;
        return m_samples[prev];
    }

    int hitchInWindow() const { return m_hitchInWindow; }
    std::uint32_t totalHitches() const { return m_totalHitches; }

    // Soft: a couple of late frames or avg creeping up — ease art work.
    bool soft() const {
        return averageMs() >= kSoftMs || m_hitchInWindow >= 2;
    }

    // Stressed: sustained lateness — keep chrome sharp, stall off-screen art.
    bool stressed() const {
        return averageMs() >= kHitchMs || m_hitchInWindow >= 4;
    }

    // True when the soak log should emit a summary line; resets the timer.
    bool consumeLogPulse() {
        if (m_logTimer < kLogIntervalS)
            return false;
        m_logTimer = 0.f;
        return true;
    }

private:
    float m_samples[kWindow]{};
    int m_index = 0;
    int m_count = 0;
    int m_hitchInWindow = 0;
    std::uint32_t m_totalHitches = 0;
    float m_logTimer = 0.f;
};
