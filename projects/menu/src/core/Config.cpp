#include "Config.hpp"
#include <sys/stat.h>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace {

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
    readJsonOpt(j, "uiLanguageOverride", uiLanguageOverride);
    readJsonOpt(j, "soundPreset", soundPreset);
    readJsonOpt(j, "defaultProfileEnabled", defaultProfileEnabled);
    readJsonOpt(j, "defaultProfileUid", defaultProfileUid);
    readJsonOpt(j, "tutorialCompleted", tutorialCompleted);
    readJsonOpt(j, "clockUse12Hour", clockUse12Hour);
    readJsonOpt(j, "themePreset", themePreset);

    if (musicVolume < 0.f) musicVolume = 0.f;
    if (musicVolume > 1.f) musicVolume = 1.f;
    if (sfxVolume   < 0.f) sfxVolume   = 0.f;
    if (sfxVolume   > 1.f) sfxVolume   = 1.f;
    gridColumns = std::clamp(gridColumns, 3, 8);
    gridRows = std::clamp(gridRows, 2, 5);
    if (uiLanguageOverride.empty()) uiLanguageOverride = "auto";
    if (soundPreset.empty()) soundPreset = "wiiu";
    if (!defaultProfileEnabled) defaultProfileUid.clear();
    if (themePreset.empty()) themePreset = "Default Light";

    return true;
}

bool AppConfig::save() const {
    mkdir("sdmc:/config", 0777);
    mkdir(kConfigDir, 0777);

    nlohmann::json j;
    j["musicEnabled"] = musicEnabled;
    j["musicVolume"] = musicVolume;
    j["sfxVolume"] = sfxVolume;
    j["gridColumns"] = std::clamp(gridColumns, 3, 8);
    j["gridRows"] = std::clamp(gridRows, 2, 5);
    j["uiLanguageOverride"] = uiLanguageOverride;
    j["soundPreset"] = soundPreset;
    j["defaultProfileEnabled"] = defaultProfileEnabled;
    j["defaultProfileUid"] = defaultProfileEnabled ? defaultProfileUid : std::string();
    j["tutorialCompleted"] = tutorialCompleted;
    j["clockUse12Hour"] = clockUse12Hour;
    j["themePreset"] = themePreset;

    std::ofstream f(kConfigPath, std::ios::trunc);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
}
