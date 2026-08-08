#pragma once

namespace settings::debug {

struct SettingsGlassTuning {
    float preBlurRadius = 0.0f;
    int blurIterations = 0;
    float shaderBlurIntensity = 0.0f;
    float refractionIntensity = 0.035f;
    float glowIntensity = 0.14f;
    float saturation = 0.94f;
    float roughness = 0.004f;
    float powerFactor = 20.0f;
    float inset = 4.0f;
    float tintAlphaDark = 0.48f;
    float tintAlphaLight = 0.42f;
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
