#pragma once

#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace switchu::icons {

// Photoshop-style HSL adjust for colour icons (hue rotates every pixel;
// sat/light pivots around 0.5 = leave original). Mono glyphs ignore this.
// Flatten / grayscale are per-pack presentation modes (Theme Shop toggles).
struct HslAdjust {
    bool enabled = false;
    float hue = 0.5f;   // 0.5 = no hue shift; 0..1 → ±180°
    float sat = 0.5f;   // 0.5 = original; 0 = gray; 1 = full sat
    float light = 0.5f; // 0.5 = original; 0 = black; 1 = white
    bool flatten = false;   // silhouette mask (white×alpha)
    bool grayscale = false; // force gray look / mono when untinted
};

// Flatten / grayscale / sat≈0 rules that force a chrome-follow mono mask.
// Flatten never forces chrome B/W — it keeps a flat fill (sampled hue, or
// Custom tint / Light). Flatten+Grayscale is a flat gray silhouette.
inline bool hslTreatsAsMono(const HslAdjust& a) {
    if (a.flatten)
        return false;
    if (a.grayscale && !a.enabled)
        return true;
    // Saturation fully down → mono, unless grayscale+tint (L-only colour path).
    if (a.enabled && !a.grayscale && a.sat < 0.02f)
        return true;
    return false;
}

// Loads a PNG/JPG into `out`. Returns true on success.
//
// Monochrome glyphs (black, white, or grayscale gradients) are normalized to a
// white RGB × coverage-alpha mask - luminance is baked into alpha so mid-tones
// survive when chrome tints the icon white or black. Colour artwork is uploaded
// unchanged (*outAdaptive false → identity white tint), optionally HSL-shifted.
//
// When outMeanLuma is set, writes the sparse-sampled mean relative luminance.
// When outContrastMask is set and the icon is colour (not mono), also uploads a
// white×alpha *luminance* twin (from pre-shift pixels) for chrome follow.
// CPU-only decode (safe on worker threads). Uploading is the caller's job.
struct DecodedAdaptiveIcon {
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint8_t> contrastMask; // empty when mono / unused
    int w = 0;
    int h = 0;
    bool adaptive = false;
    float meanLuma = 0.5f;
};

bool decodeMaybeAdaptiveIcon(const std::string& path,
                             DecodedAdaptiveIcon& out,
                             int maxSide = 128,
                             const HslAdjust* hslAdjust = nullptr,
                             bool wantContrastMask = false);

bool loadMaybeAdaptiveIcon(nxui::Texture& out,
                           nxui::GpuDevice& gpu,
                           nxui::Renderer& ren,
                           const std::string& path,
                           bool* outAdaptive,
                           int maxSide = 128,
                           float* outMeanLuma = nullptr,
                           nxui::Texture* outContrastMask = nullptr,
                           const HslAdjust* hslAdjust = nullptr);

// Decide whether a colour icon should keep authored colours or force a
// black/white silhouette against the current backdrop luminance.
enum class ColourIconForce : std::uint8_t { None = 0, White, Black };

ColourIconForce resolveColourIconContrast(float iconMeanLuma,
                                          float backdropLuma,
                                          ColourIconForce previous);

// True when the PNG/JPG has a clear colourful majority (not a mono glyph).
bool probeIconLooksColour(const std::string& path);

// Mean HSL of opaque pixels in a PNG/JPG (same sampler Flatten uses).
bool probeIconMeanHsl(const std::string& path, float& outH, float& outS, float& outL);

// Photoshop-style pivots that move pack mean HSL toward a theme accent.
// Used by Match theme (with or without Solid/Flatten). Weak accents get a
// sat floor so Auto does not collapse to gray.
struct MatchThemePivots {
    float hue = 0.5f;
    float sat = 0.5f;
    float light = 0.5f;
};
MatchThemePivots computeMatchThemePivots(float meanH, float meanS, float meanL,
                                        float accentH, float accentS, float accentL);

} // namespace switchu::icons
