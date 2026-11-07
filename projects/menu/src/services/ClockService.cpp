#include "ClockService.hpp"

#include "core/DebugLog.hpp"
#include "services/SystemTime.hpp"

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
    if (m_reseedWallClock) {
        m_reseedWallClock = false;
        system_time::reseedWallClock("clock changed");
    }

    std::uint64_t timestamp = 0;
    Result rc = system_time::currentPosixTime(&timestamp);
    if (R_FAILED(rc))
        return m_valid ? extrapolated(rc) : CalendarSnapshot{.result = rc};

    if (m_valid && !m_forceConvert
        && timestamp >= m_minuteStart && timestamp < m_minuteStart + 60) {
        m_snapshot.calendar.second = static_cast<u8>(timestamp - m_minuteStart);
        m_snapshot.result = rc;
        return m_snapshot;
    }

    CalendarSnapshot next{};
    rc = timeToCalendarTimeWithMyRule(timestamp, &next.calendar, &next.additional);
    next.result = rc;
    if (R_FAILED(rc)) {
        // Timezone rule unavailable (busy time:s / missing location) —
        // still show a clock from POSIX UTC so HOME chrome stays up.
        if (calendarFromUtcPosix(timestamp, next.calendar)) {
            next.valid = true;
        } else {
            return m_valid ? extrapolated(rc) : next;
        }
    } else {
        next.valid = true;
    }

    m_snapshot = next;
    m_valid = true;
    m_forceConvert = false;
    m_minuteStart = timestamp - next.calendar.second;
    m_baseUptimeNs = system_time::uptimeNs() - next.calendar.second * 1'000'000'000ULL;
    return m_snapshot;
}

CalendarSnapshot ClockService::extrapolated(Result rc) const {
    const std::uint64_t elapsed =
        (system_time::uptimeNs() - m_baseUptimeNs) / 1'000'000'000ULL;
    const std::time_t local = static_cast<std::time_t>(m_minuteStart + elapsed)
                            + m_snapshot.additional.offset;
    std::tm tm{};
    if (!gmtime_r(&local, &tm))
        return m_snapshot;

    CalendarSnapshot out = m_snapshot;
    out.calendar.year = static_cast<u16>(tm.tm_year + 1900);
    out.calendar.month = static_cast<u8>(tm.tm_mon + 1);
    out.calendar.day = static_cast<u8>(tm.tm_mday);
    out.calendar.hour = static_cast<u8>(tm.tm_hour);
    out.calendar.minute = static_cast<u8>(tm.tm_min);
    out.calendar.second = static_cast<u8>(tm.tm_sec);
    out.additional.wday = static_cast<u32>(tm.tm_wday);
    out.additional.yday = static_cast<u32>(tm.tm_yday);
    out.result = rc;
    return out;
}

} // namespace switchu::services
