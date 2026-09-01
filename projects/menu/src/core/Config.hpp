#pragma once
#include "core/AppLayoutMode.hpp"
#include <string>

struct AppConfig {
    bool  musicEnabled = true;
    float musicVolume  = 0.4f;
    float sfxVolume    = 0.7f;
    int   gridColumns  = 5;
    int   gridRows     = 3;
    AppLayoutMode appLayoutMode = AppLayoutMode::Grid;
    std::string actionHintStyle = "capsules";
    std::string uiLanguageOverride = "auto";
    std::string soundPreset = "wiiu";
    bool  defaultProfileEnabled = false;
    std::string defaultProfileUid;
    bool  tutorialCompleted = false;
    bool  clockUse12Hour = false;
    bool  accessibilityEnabled = true;
    bool  accessibilitySpeakHints = true;
    bool  accessibilitySpeakContextEveryFocus = false;
    bool  accessibilitySpeakPosition = true;
    int   accessibilitySpeechRate = 190;
    bool  steamGridDbEnabled = true;
    std::string steamGridDbApiKey;

    std::string themePreset = "Default Light";

    // Automatic day/night theme switching.
    std::string autoThemeMode = "off";       // "off" | "manual" | "geo"
    std::string autoThemeDayPreset;          // preset id/name used during the day
    std::string autoThemeNightPreset;        // preset id/name used during the night
    int         autoThemeDayStartHour = 7;   // manual boundary [0..23]
    int         autoThemeNightStartHour = 19;// manual boundary [0..23]
    // Cached IP-geolocated position for the geolocation mode.
    bool        autoThemeGeoResolved = false;
    double      autoThemeGeoLat = 0.0;       // degrees, north positive
    double      autoThemeGeoLon = 0.0;       // degrees, east positive
    std::string autoThemeGeoCity;            // human-readable resolved location

    bool load();

    bool save() const;

    static constexpr const char* kConfigDir  = "sdmc:/config/SwitchU";
    static constexpr const char* kConfigPath = "sdmc:/config/SwitchU/settings.json";
};
