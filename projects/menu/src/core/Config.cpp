#include "Config.hpp"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <system_error>

namespace {

static constexpr const char* kLegacyConfigPath = "sdmc:/config/SwitchU/config.json";

template <typename T>
void readJsonOpt(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) {
        try {
            out = it->get<T>();
        } catch (...) {
        }
    }
}

} // namespace

bool AppConfig::load() {
    std::ifstream f(kConfigPath);
    if (!f.is_open()) {
        f.clear();
        f.open(kLegacyConfigPath);
    }
    if (!f.is_open()) return false;

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return false;
    }

    readJsonOpt(j, "musicEnabled", musicEnabled);
    readJsonOpt(j, "musicVolume", musicVolume);
    readJsonOpt(j, "sfxVolume", sfxVolume);
    readJsonOpt(j, "gridColumns", gridColumns);
    readJsonOpt(j, "gridRows", gridRows);
    {
        std::string modeStr;
        readJsonOpt(j, "appLayoutMode", modeStr);
        if (modeStr == "dynamic_line" || modeStr == "line")
            appLayoutMode = AppLayoutMode::DynamicLine;
        else if (modeStr == "grid")
            appLayoutMode = AppLayoutMode::Grid;
    }
    readJsonOpt(j, "actionHintStyle", actionHintStyle);
    readJsonOpt(j, "uiLanguageOverride", uiLanguageOverride);
    readJsonOpt(j, "soundPreset", soundPreset);
    readJsonOpt(j, "defaultProfileEnabled", defaultProfileEnabled);
    readJsonOpt(j, "defaultProfileUid", defaultProfileUid);
    readJsonOpt(j, "tutorialCompleted", tutorialCompleted);
    readJsonOpt(j, "clockUse12Hour", clockUse12Hour);
    readJsonOpt(j, "accessibilityEnabled", accessibilityEnabled);
    readJsonOpt(j, "accessibilitySpeakHints", accessibilitySpeakHints);
    readJsonOpt(j, "accessibilitySpeakContextEveryFocus", accessibilitySpeakContextEveryFocus);
    readJsonOpt(j, "accessibilitySpeakPosition", accessibilitySpeakPosition);
    readJsonOpt(j, "accessibilitySpeechRate", accessibilitySpeechRate);
    readJsonOpt(j, "steamGridDbEnabled", steamGridDbEnabled);
    readJsonOpt(j, "steamGridDbApiKey", steamGridDbApiKey);
    readJsonOpt(j, "themePreset", themePreset);
    readJsonOpt(j, "autoThemeMode", autoThemeMode);
    readJsonOpt(j, "autoThemeDayPreset", autoThemeDayPreset);
    readJsonOpt(j, "autoThemeNightPreset", autoThemeNightPreset);
    readJsonOpt(j, "autoThemeDayStartHour", autoThemeDayStartHour);
    readJsonOpt(j, "autoThemeNightStartHour", autoThemeNightStartHour);
    readJsonOpt(j, "autoThemeGeoResolved", autoThemeGeoResolved);
    readJsonOpt(j, "autoThemeGeoLat", autoThemeGeoLat);
    readJsonOpt(j, "autoThemeGeoLon", autoThemeGeoLon);
    readJsonOpt(j, "autoThemeGeoCity", autoThemeGeoCity);

    if (musicVolume < 0.f) musicVolume = 0.f;
    if (musicVolume > 1.f) musicVolume = 1.f;
    if (sfxVolume   < 0.f) sfxVolume   = 0.f;
    if (sfxVolume   > 1.f) sfxVolume   = 1.f;
    gridColumns = std::clamp(gridColumns, 3, 8);
    gridRows = std::clamp(gridRows, 2, 5);
    if (actionHintStyle != "panel" && actionHintStyle != "capsules")
        actionHintStyle = "capsules";
    if (uiLanguageOverride.empty()) uiLanguageOverride = "auto";
    if (soundPreset.empty()) soundPreset = "wiiu";
    if (!defaultProfileEnabled) defaultProfileUid.clear();
    accessibilitySpeechRate = std::clamp(accessibilitySpeechRate, 120, 320);
    if (themePreset.empty()) themePreset = "Default Light";

    if (autoThemeMode != "off" && autoThemeMode != "manual" && autoThemeMode != "geo")
        autoThemeMode = "off";
    autoThemeDayStartHour = std::clamp(autoThemeDayStartHour, 0, 23);
    autoThemeNightStartHour = std::clamp(autoThemeNightStartHour, 0, 23);
    if (autoThemeGeoLat < -90.0 || autoThemeGeoLat > 90.0
        || autoThemeGeoLon < -180.0 || autoThemeGeoLon > 180.0) {
        autoThemeGeoResolved = false;
        autoThemeGeoLat = 0.0;
        autoThemeGeoLon = 0.0;
        autoThemeGeoCity.clear();
    }

    return true;
}

bool AppConfig::save() const {
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory(kConfigDir, ec);

    nlohmann::json j;
    j["musicEnabled"] = musicEnabled;
    j["musicVolume"] = musicVolume;
    j["sfxVolume"] = sfxVolume;
    j["gridColumns"] = std::clamp(gridColumns, 3, 8);
    j["gridRows"] = std::clamp(gridRows, 2, 5);
    j["appLayoutMode"] = (appLayoutMode == AppLayoutMode::DynamicLine) ? "dynamic_line" : "grid";
    j["actionHintStyle"] = actionHintStyle == "panel" ? "panel" : "capsules";
    j["uiLanguageOverride"] = uiLanguageOverride;
    j["soundPreset"] = soundPreset;
    j["defaultProfileEnabled"] = defaultProfileEnabled;
    j["defaultProfileUid"] = defaultProfileEnabled ? defaultProfileUid : std::string();
    j["tutorialCompleted"] = tutorialCompleted;
    j["clockUse12Hour"] = clockUse12Hour;
    j["accessibilityEnabled"] = accessibilityEnabled;
    j["accessibilitySpeakHints"] = accessibilitySpeakHints;
    j["accessibilitySpeakContextEveryFocus"] = accessibilitySpeakContextEveryFocus;
    j["accessibilitySpeakPosition"] = accessibilitySpeakPosition;
    j["accessibilitySpeechRate"] = std::clamp(accessibilitySpeechRate, 120, 320);
    j["steamGridDbEnabled"] = steamGridDbEnabled;
    j["steamGridDbApiKey"] = steamGridDbApiKey;
    j["themePreset"] = themePreset;
    j["autoThemeMode"] = (autoThemeMode == "manual" || autoThemeMode == "geo") ? autoThemeMode : "off";
    j["autoThemeDayPreset"] = autoThemeDayPreset;
    j["autoThemeNightPreset"] = autoThemeNightPreset;
    j["autoThemeDayStartHour"] = std::clamp(autoThemeDayStartHour, 0, 23);
    j["autoThemeNightStartHour"] = std::clamp(autoThemeNightStartHour, 0, 23);
    j["autoThemeGeoResolved"] = autoThemeGeoResolved;
    j["autoThemeGeoLat"] = autoThemeGeoLat;
    j["autoThemeGeoLon"] = autoThemeGeoLon;
    j["autoThemeGeoCity"] = autoThemeGeoCity;

    std::ofstream f(kConfigPath, std::ios::trunc);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
}
