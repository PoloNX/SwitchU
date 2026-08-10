#pragma once

namespace settings::debug {

struct SettingsGlassTuning {
    // One overlapping half-resolution pass avoids the six full-screen passes
    // previously produced by radius 4 while the local glass shader preserves
    // the pronounced frosted look.
    float preBlurRadius = 2.5f;
    int blurIterations = 1;
    float shaderBlurIntensity = 2.25f;
    float refractionIntensity = 0.32f;
    float glowIntensity = 0.62f;
    float saturation = 1.04f;
    float roughness = 0.008f;
    float powerFactor = 8.0f;
    float inset = 4.0f;
    float tintAlphaDark = 0.78f;
    float tintAlphaLight = 0.72f;
    float shade = 0.08f;
};

inline SettingsGlassTuning& settingsGlassTuning() {
    static SettingsGlassTuning tuning;
    return tuning;
}

inline void resetSettingsGlassTuning() {
    settingsGlassTuning() = SettingsGlassTuning{};
}

} // namespace settings::debug
