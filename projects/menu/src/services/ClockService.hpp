#pragma once

#include <switch.h>

namespace switchu::services {

struct CalendarSnapshot {
    bool valid = false;
    TimeCalendarTime calendar{};
    TimeCalendarAdditionalInfo additional{};
    Result result = 0;
};

// Converts the Horizon clock through the device's active timezone rule. This
// avoids relying on process-global libc TZ state in the custom library applet.
class ClockService final {
public:
    CalendarSnapshot refresh();
    void invalidate() { m_valid = false; }

private:
    bool m_valid = false;
    CalendarSnapshot m_snapshot{};
};

} // namespace switchu::services
