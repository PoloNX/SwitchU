#include "ClockService.hpp"

#include "core/DebugLog.hpp"

#include <ctime>

namespace switchu::services {

namespace {

bool calendarFromUtcPosix(u64 timestamp, TimeCalendarTime& calendar) {
    const time_t posix = static_cast<time_t>(timestamp);
    struct tm utc {};
    if (!gmtime_r(&posix, &utc))
        return false;
    calendar.year = static_cast<u16>(utc.tm_year + 1900);
    calendar.month = static_cast<u8>(utc.tm_mon + 1);
    calendar.day = static_cast<u8>(utc.tm_mday);
    calendar.hour = static_cast<u8>(utc.tm_hour);
    calendar.minute = static_cast<u8>(utc.tm_min);
    calendar.second = static_cast<u8>(utc.tm_sec);
    return true;
}

} // namespace

Result ClockService::applyPosix(u64 posixUtc) {
    const Result userRc = timeSetCurrentTime(TimeType_UserSystemClock, posixUtc);
    const Result localRc = timeSetCurrentTime(TimeType_LocalSystemClock, posixUtc);
    const Result netRc = timeSetCurrentTime(TimeType_NetworkSystemClock, posixUtc);
    DebugLog::log(
        "[settings-time] menu time:a apply user=0x%X local=0x%X net=0x%X posix=%llu",
        userRc, localRc, netRc, static_cast<unsigned long long>(posixUtc));
    if (R_SUCCEEDED(userRc) || R_SUCCEEDED(localRc) || R_SUCCEEDED(netRc))
        return 0;
    return R_FAILED(userRc) ? userRc : (R_FAILED(localRc) ? localRc : netRc);
}

CalendarSnapshot ClockService::refresh() {
    u64 timestamp = 0;
    Result rc = timeGetCurrentTime(TimeType_UserSystemClock, &timestamp);
    if (R_FAILED(rc))
        rc = timeGetCurrentTime(TimeType_NetworkSystemClock, &timestamp);
    if (R_FAILED(rc))
        rc = timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp);

    CalendarSnapshot next{};
    next.result = rc;
    if (R_SUCCEEDED(rc)) {
        Result calRc = timeToCalendarTimeWithMyRule(timestamp, &next.calendar, &next.additional);
        if (R_SUCCEEDED(calRc)) {
            next.valid = true;
        } else if (calendarFromUtcPosix(timestamp, next.calendar)) {
            // Timezone rule unavailable (busy time:s / missing location) —
            // still show a clock from POSIX UTC so HOME chrome stays up.
            next.valid = true;
            next.result = calRc;
        } else {
            next.result = calRc;
        }
    }

    if (next.valid || !m_valid) {
        m_snapshot = next;
        m_valid = next.valid;
    }
    return m_snapshot;
}

} // namespace switchu::services
