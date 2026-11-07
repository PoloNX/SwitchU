#pragma once

#include <switch.h>

#include <cstdint>

namespace switchu::services {

struct CalendarSnapshot {
    bool valid = false;
    TimeCalendarTime calendar{};
    TimeCalendarAdditionalInfo additional{};
    Result result = 0;
};

// Converts the Horizon clock through the device's active timezone rule. This
// avoids relying on process-global libc TZ state in the custom library applet.
//
// The timezone conversion is an IPC round trip, so it runs once per minute;
// seconds inside the minute come from the POSIX time alone. Once a time has
// been read, a failing time service no longer blanks or freezes the clock: it
// keeps running from the CPU counter with the last known UTC offset.
class ClockService final {
public:
    CalendarSnapshot refresh();
    // The clock or timezone changed (or the console woke): convert again on the
    // next refresh and re-seed libnx's wall clock.
    void invalidate() { m_forceConvert = true; m_reseedWallClock = true; }

private:
    CalendarSnapshot extrapolated(Result rc) const;

    bool m_valid = false;
    bool m_forceConvert = true;
    bool m_reseedWallClock = false;
    CalendarSnapshot m_snapshot{};
    std::uint64_t m_minuteStart = 0;  // POSIX time of m_snapshot's minute.
    std::uint64_t m_baseUptimeNs = 0; // Uptime when m_minuteStart was read.
};

} // namespace switchu::services
