#pragma once
#include <string>

struct AppConfig {
    bool  musicEnabled = true;
    float musicVolume  = 0.4f;
    float sfxVolume    = 0.7f;
    int   gridColumns  = 5;
    int   gridRows     = 3;
    std::string uiLanguageOverride = "auto";
    std::string soundPreset = "wiiu";

    std::string themePreset = "Default Dark";
    std::string themeMode = "";
    float accentH = -1.f, accentS = -1.f, accentL = -1.f;
    float bgH     = -1.f, bgS     = -1.f, bgL     = -1.f;
    float bgAccH  = -1.f, bgAccS  = -1.f, bgAccL  = -1.f;
    float shapeH  = -1.f, shapeS  = -1.f, shapeL  = -1.f;

    bool load();

    bool save() const;

    static constexpr const char* kConfigDir  = "sdmc:/config/SwitchU";
    static constexpr const char* kConfigPath = "sdmc:/config/SwitchU/config.json";
};
