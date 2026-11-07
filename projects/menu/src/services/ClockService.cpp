#include "ClockService.hpp"

#include "services/SystemTime.hpp"

#include <ctime>

namespace switchu::services {

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
    if (R_FAILED(rc))
        return m_valid ? extrapolated(rc) : next;

    next.valid = true;
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
