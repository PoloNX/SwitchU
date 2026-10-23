#pragma once

#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>

#include <cstdint>
#include <string>

namespace switchu::icons {

// Photoshop-style HSL adjust for colour icons (hue rotates every pixel;
// sat/light pivots around 0.5 = leave original). Mono glyphs ignore this.
struct HslAdjust {
    bool enabled = false;
    float hue = 0.5f;   // 0.5 = no hue shift; 0..1 → ±180°
    float sat = 0.5f;   // 0.5 = original; 0 = gray; 1 = full sat
    float light = 0.5f; // 0.5 = original; 0 = black; 1 = white
};

// Loads a PNG/JPG into `out`. Returns true on success.
//
// Monochrome glyphs (black, white, or grayscale gradients) are normalized to a
// white RGB × coverage-alpha mask — luminance is baked into alpha so mid-tones
// survive when chrome tints the icon white or black. Colour artwork is uploaded
// unchanged (*outAdaptive false → identity white tint), optionally HSL-shifted.
//
// When outMeanLuma is set, writes the sparse-sampled mean relative luminance.
// When outContrastMask is set and the icon is colour (not mono), also uploads a
// white×alpha *luminance* twin (from pre-shift pixels) for chrome follow.
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
// Used to mark colour icon packs for spectrum badge / custom tint UI.
bool probeIconLooksColour(const std::string& path);

} // namespace switchu::icons
