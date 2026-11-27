#pragma once

#include "core/DebugLog.hpp"

#include <chrono>

// Lightweight scope timer for attributing main-thread hitches. Logs only when
// the scope exceeded the threshold, so it is safe to leave in hot paths.
//   PerfProbe probe("themeshop.show");            // 12 ms default threshold
//   PerfProbe probe("themeshop.prime", 5);
class PerfProbe {
public:
    explicit PerfProbe(const char* name, long thresholdMs = 12)
        : m_name(name),
          m_thresholdMs(thresholdMs),
          m_start(std::chrono::steady_clock::now()) {}

    ~PerfProbe() {
        const long ms = elapsedMs();
        if (ms >= m_thresholdMs)
            DebugLog::log("[probe] %s took %ldms", m_name, ms);
    }

    PerfProbe(const PerfProbe&) = delete;
    PerfProbe& operator=(const PerfProbe&) = delete;

    long elapsedMs() const {
        return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_start).count());
    }

private:
    const char* m_name;
    long m_thresholdMs;
    std::chrono::steady_clock::time_point m_start;
};
