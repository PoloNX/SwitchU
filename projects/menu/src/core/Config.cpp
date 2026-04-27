#include "Config.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <string>
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
    readJsonOpt(j, "themePreset", themePreset);
    readJsonOpt(j, "themeMode", themeMode);
    readJsonOpt(j, "accentH", accentH);
    readJsonOpt(j, "accentS", accentS);
    readJsonOpt(j, "accentL", accentL);
    readJsonOpt(j, "bgH", bgH);
    readJsonOpt(j, "bgS", bgS);
    readJsonOpt(j, "bgL", bgL);
    readJsonOpt(j, "bgAccH", bgAccH);
    readJsonOpt(j, "bgAccS", bgAccS);
    readJsonOpt(j, "bgAccL", bgAccL);
    readJsonOpt(j, "shapeH", shapeH);
    readJsonOpt(j, "shapeS", shapeS);
    readJsonOpt(j, "shapeL", shapeL);

    if (musicVolume < 0.f) musicVolume = 0.f;
    if (musicVolume > 1.f) musicVolume = 1.f;
    if (sfxVolume   < 0.f) sfxVolume   = 0.f;
    if (sfxVolume   > 1.f) sfxVolume   = 1.f;
    gridColumns = std::clamp(gridColumns, 3, 8);
    gridRows = std::clamp(gridRows, 2, 5);
    if (uiLanguageOverride.empty()) uiLanguageOverride = "auto";
    if (soundPreset.empty()) soundPreset = "wiiu";
    if (themePreset.empty()) themePreset = "Default Dark";

    auto clampHSL = [](float& v) { if (v < 0.f) v = -1.f; else if (v > 1.f) v = 1.f; };
    clampHSL(accentH); clampHSL(accentS); clampHSL(accentL);
    clampHSL(bgH);     clampHSL(bgS);     clampHSL(bgL);
    clampHSL(bgAccH);  clampHSL(bgAccS);  clampHSL(bgAccL);
    clampHSL(shapeH);  clampHSL(shapeS);  clampHSL(shapeL);

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
    j["themePreset"] = themePreset;
    j["themeMode"] = themeMode;

    j["accentH"] = accentH;
    j["accentS"] = accentS;
    j["accentL"] = accentL;
    j["bgH"] = bgH;
    j["bgS"] = bgS;
    j["bgL"] = bgL;
    j["bgAccH"] = bgAccH;
    j["bgAccS"] = bgAccS;
    j["bgAccL"] = bgAccL;
    j["shapeH"] = shapeH;
    j["shapeS"] = shapeS;
    j["shapeL"] = shapeL;

    std::ofstream f(kConfigPath, std::ios::trunc);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
}
