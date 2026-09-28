#include "services/AutoThemeService.hpp"

#include <algorithm>
#include <cmath>

namespace switchu::services {

namespace {

int clampHour(int hour) {
    return std::clamp(hour, 0, 23);
}

constexpr double kPi = 3.14159265358979323846;

double deg2rad(double d) { return d * kPi / 180.0; }
double rad2deg(double r) { return r * 180.0 / kPi; }

double normalizeDeg(double d) {
    d = std::fmod(d, 360.0);
    return d < 0.0 ? d + 360.0 : d;
}

double normalizeHours(double h) {
    h = std::fmod(h, 24.0);
    return h < 0.0 ? h + 24.0 : h;
}

int dayOfYear(int year, int month, int day) {
    static const int cumulative[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int n = cumulative[std::clamp(month, 1, 12) - 1] + day;
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (leap && month > 2)
        n += 1;
    return n;
}

// Sunrise/Sunset algorithm from the Almanac for Computers (official zenith
// 90.833deg). Longitude is east-positive; tzOffsetHours converts UTC to local.
// Returns times in local hours [0..24).
SunTimes computeSunTimes(const GeoLocation& loc, int year, int month, int day,
                         double tzOffsetHours) {
    constexpr double kZenith = 90.833;
    const double lngHour = loc.longitude / 15.0;
    const int N = dayOfYear(year, month, day);

    auto solve = [&](bool rising) -> std::optional<double> {
        const double t = rising ? (N + ((6.0 - lngHour) / 24.0))
                                : (N + ((18.0 - lngHour) / 24.0));

        const double M = (0.9856 * t) - 3.289;                       // mean anomaly
        double L = M + (1.916 * std::sin(deg2rad(M)))
                     + (0.020 * std::sin(deg2rad(2.0 * M))) + 282.634; // true longitude
        L = normalizeDeg(L);

        double RA = rad2deg(std::atan(0.91764 * std::tan(deg2rad(L)))); // right ascension
        RA = normalizeDeg(RA);
        // Put RA in the same quadrant as L.
        const double Lquadrant = std::floor(L / 90.0) * 90.0;
        const double RAquadrant = std::floor(RA / 90.0) * 90.0;
        RA = (RA + (Lquadrant - RAquadrant)) / 15.0;

        const double sinDec = 0.39782 * std::sin(deg2rad(L));
        const double cosDec = std::cos(std::asin(sinDec));

        const double cosH = (std::cos(deg2rad(kZenith)) - (sinDec * std::sin(deg2rad(loc.latitude))))
                          / (cosDec * std::cos(deg2rad(loc.latitude)));
        if (cosH > 1.0 || cosH < -1.0)
            return std::nullopt; // sun never rises / never sets on this date

        double H = rising ? (360.0 - rad2deg(std::acos(cosH)))
                          : rad2deg(std::acos(cosH));
        H /= 15.0;

        const double T = H + RA - (0.06571 * t) - 6.622; // local mean time of event
        const double UT = normalizeHours(T - lngHour);   // in UTC
        return normalizeHours(UT + tzOffsetHours);       // in local time
    };

    SunTimes out;
    out.valid = true;
    auto sunrise = solve(true);
    auto sunset = solve(false);

    if (!sunrise || !sunset) {
        // Polar case: decide from the sun's declination vs latitude. When the
        // event does not occur, the sun is either always up or always down.
        // Approximate using solar declination for the date.
        const double decl = 23.44 * std::sin(deg2rad((360.0 / 365.0) * (N - 81)));
        const bool sunUp = (loc.latitude >= 0.0) ? (decl > (90.0 - loc.latitude - 0.833))
                                                  : (decl < -(90.0 + loc.latitude - 0.833));
        out.polarDay = sunUp;
        out.polarNight = !sunUp;
        return out;
    }

    out.sunriseLocalHours = *sunrise;
    out.sunsetLocalHours = *sunset;
    return out;
}

} // namespace

ManualScheduleProvider::ManualScheduleProvider(int dayStartHour, int nightStartHour)
    : m_dayStartHour(clampHour(dayStartHour)),
      m_nightStartHour(clampHour(nightStartHour)) {}

DayPhase ManualScheduleProvider::phaseFor(const CalendarSnapshot& now) const {
    const int hour = clampHour(static_cast<int>(now.calendar.hour));

    // Degenerate configuration: no meaningful boundary -> treat everything as day.
    if (m_dayStartHour == m_nightStartHour)
        return DayPhase::Day;

    if (m_dayStartHour < m_nightStartHour) {
        // Day window sits within a single calendar day.
        const bool isDay = hour >= m_dayStartHour && hour < m_nightStartHour;
        return isDay ? DayPhase::Day : DayPhase::Night;
    }

    // Day window wraps past midnight (e.g. day starts 19h, night starts 7h).
    const bool isDay = hour >= m_dayStartHour || hour < m_nightStartHour;
    return isDay ? DayPhase::Day : DayPhase::Night;
}

DayPhase GeolocationProvider::phaseFor(const CalendarSnapshot& now) const {
    if (!m_location)
        return DayPhase::Day; // never reached while available() is false

    const double tzOffsetHours = static_cast<double>(now.additional.offset) / 3600.0;
    const SunTimes sun = computeSunTimes(*m_location,
                                         static_cast<int>(now.calendar.year),
                                         static_cast<int>(now.calendar.month),
                                         static_cast<int>(now.calendar.day),
                                         tzOffsetHours);

    if (sun.polarDay)
        return DayPhase::Day;
    if (sun.polarNight)
        return DayPhase::Night;

    const double nowLocal = static_cast<double>(now.calendar.hour)
                          + static_cast<double>(now.calendar.minute) / 60.0;

    // Normal case: sunrise before sunset within the same local day.
    if (sun.sunriseLocalHours <= sun.sunsetLocalHours) {
        const bool isDay = nowLocal >= sun.sunriseLocalHours && nowLocal < sun.sunsetLocalHours;
        return isDay ? DayPhase::Day : DayPhase::Night;
    }

    // Wrapped case (can happen from the UTC->local shift): daytime spans midnight.
    const bool isDay = nowLocal >= sun.sunriseLocalHours || nowLocal < sun.sunsetLocalHours;
    return isDay ? DayPhase::Day : DayPhase::Night;
}

SunTimes AutoThemeService::geoSunTimes(const CalendarSnapshot& now) const {
    const auto loc = m_geo.location();
    if (!loc || !now.valid)
        return SunTimes{};

    const double tzOffsetHours = static_cast<double>(now.additional.offset) / 3600.0;
    return computeSunTimes(*loc,
                           static_cast<int>(now.calendar.year),
                           static_cast<int>(now.calendar.month),
                           static_cast<int>(now.calendar.day),
                           tzOffsetHours);
}

void AutoThemeService::configure(const AutoThemeSettings& settings) {
    m_settings = settings;
    m_settings.dayStartHour = clampHour(m_settings.dayStartHour);
    m_settings.nightStartHour = clampHour(m_settings.nightStartHour);
    m_manual = ManualScheduleProvider(m_settings.dayStartHour, m_settings.nightStartHour);
    // Force a fresh decision against the new configuration.
    m_lastPhase.reset();
}

const DaylightProvider* AutoThemeService::activeProvider() const {
    switch (m_settings.mode) {
        case AutoThemeMode::ManualHours:
            return &m_manual;
        case AutoThemeMode::Geolocation:
            return &m_geo;
        case AutoThemeMode::Off:
        default:
            return nullptr;
    }
}

std::optional<std::string> AutoThemeService::evaluate(const CalendarSnapshot& now) {
    if (m_settings.mode == AutoThemeMode::Off)
        return std::nullopt;

    if (!now.valid)
        return std::nullopt;

    const DaylightProvider* provider = activeProvider();
    if (!provider || !provider->available())
        return std::nullopt;

    const DayPhase phase = provider->phaseFor(now);
    if (m_lastPhase && *m_lastPhase == phase)
        return std::nullopt;

    m_lastPhase = phase;

    const std::string& target = (phase == DayPhase::Day) ? m_settings.dayPreset
                                                         : m_settings.nightPreset;
    if (target.empty())
        return std::nullopt;

    return target;
}

} // namespace switchu::services
