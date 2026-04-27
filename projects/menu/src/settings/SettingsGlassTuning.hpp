#pragma once

namespace settings::debug {

struct SettingsGlassTuning {
    float preBlurRadius = 3.0f;
    int blurIterations = 15;
    float shaderBlurIntensity = 0.0f;
    float refractionIntensity = 0.035f;
    float glowIntensity = 0.14f;
    float saturation = 0.94f;
    float roughness = 0.004f;
    float powerFactor = 20.0f;
    float inset = 10.0f;
    float tintAlphaDark = 0.22f;
    float tintAlphaLight = 0.18f;
    float shade = 0.12f;
};

inline SettingsGlassTuning& settingsGlassTuning() {
    static SettingsGlassTuning tuning;
    return tuning;
}

inline void resetSettingsGlassTuning() {
    settingsGlassTuning() = SettingsGlassTuning{};
}

} // namespace settings::debug