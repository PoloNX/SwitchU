#pragma once

#include "services/ClockService.hpp"

#include <memory>
#include <optional>
#include <string>

namespace switchu::services {

// How the day/night boundary is decided.
enum class AutoThemeMode {
    Off,          // feature disabled, manual theme selection stays authoritative
    ManualHours,  // fixed day/night hours configured by the user
    Geolocation,  // IP-based geolocation -> sunrise/sunset (placeholder for now)
};

enum class DayPhase {
    Day,
    Night,
};

// Approximate device location used to compute local sunrise/sunset.
struct GeoLocation {
    double latitude = 0.0;   // degrees, north positive
    double longitude = 0.0;  // degrees, east positive
};

// Local sunrise/sunset for a given date and location.
struct SunTimes {
    bool   valid = false;      // false when there is no location
    bool   polarDay = false;   // sun never sets on this date
    bool   polarNight = false; // sun never rises on this date
    double sunriseLocalHours = 0.0;
    double sunsetLocalHours = 0.0;
};

struct AutoThemeSettings {
    AutoThemeMode mode = AutoThemeMode::Off;
    std::string   dayPreset;          // preset id/name applied during the day
    std::string   nightPreset;        // preset id/name applied during the night
    int           dayStartHour = 7;   // manual boundary: when day begins [0..23]
    int           nightStartHour = 19;// manual boundary: when night begins [0..23]
};

// Abstraction over the day/night boundary so the manual schedule can be swapped
// for a geolocation-based sunrise/sunset provider later without touching the
// scheduler.
class DaylightProvider {
public:
    virtual ~DaylightProvider() = default;
    virtual DayPhase phaseFor(const CalendarSnapshot& now) const = 0;
    // Whether the provider can currently produce a meaningful answer.
    virtual bool available() const { return true; }
};

// Day when dayStartHour <= hour < nightStartHour, otherwise night. Handles
// ranges that wrap past midnight (nightStart < dayStart).
class ManualScheduleProvider final : public DaylightProvider {
public:
    ManualScheduleProvider(int dayStartHour, int nightStartHour);
    DayPhase phaseFor(const CalendarSnapshot& now) const override;

private:
    int m_dayStartHour;
    int m_nightStartHour;
};

// Derives the day/night phase from local sunrise/sunset at an IP-geolocated
// position. The Switch has no GPS, so the location is resolved once over the
// network (see WiiUMenuApp) and pushed in via setLocation(); this provider only
// does the (cheap, offline) solar math on each evaluation.
class GeolocationProvider final : public DaylightProvider {
public:
    void setLocation(std::optional<GeoLocation> location) { m_location = location; }
    std::optional<GeoLocation> location() const { return m_location; }

    DayPhase phaseFor(const CalendarSnapshot& now) const override;
    bool available() const override { return m_location.has_value(); }

private:
    std::optional<GeoLocation> m_location;
};

// Decides which theme preset should be active based on the current time and the
// configured settings. Stateful only in that it remembers the last applied
// phase to avoid re-applying a theme every evaluation tick.
class AutoThemeService {
public:
    void configure(const AutoThemeSettings& settings);

    // Returns the preset id/name that should be applied *now*, but only when the
    // computed phase differs from the last one it reported. Returns std::nullopt
    // when: the feature is off, the snapshot is invalid, the active provider is
    // unavailable, the phase is unchanged, or the target preset is empty.
    std::optional<std::string> evaluate(const CalendarSnapshot& now);

    // Forget the last phase so the next evaluate() re-applies unconditionally
    // (e.g. after the console wakes from sleep or the settings changed).
    void reset() { m_lastPhase.reset(); }

    AutoThemeMode mode() const { return m_settings.mode; }

    // Push the IP-resolved location into the geolocation provider.
    void setGeoLocation(std::optional<GeoLocation> location) {
        m_geo.setLocation(location);
        m_lastPhase.reset();
    }
    std::optional<GeoLocation> geoLocation() const { return m_geo.location(); }

    // Local sunrise/sunset for the resolved geolocation on the snapshot's date.
    // Returns valid=false when no position is available.
    SunTimes geoSunTimes(const CalendarSnapshot& now) const;

    // True when geolocation mode is active but no position is available yet, so
    // the app should trigger an IP-geolocation lookup.
    bool needsGeoLocation() const {
        return m_settings.mode == AutoThemeMode::Geolocation && !m_geo.available();
    }

private:
    const DaylightProvider* activeProvider() const;

    AutoThemeSettings           m_settings;
    ManualScheduleProvider      m_manual{7, 19};
    GeolocationProvider         m_geo;
    std::optional<DayPhase>     m_lastPhase;
};

} // namespace switchu::services
