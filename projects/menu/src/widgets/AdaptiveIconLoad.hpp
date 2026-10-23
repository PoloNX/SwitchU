#pragma once

#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <string>

namespace switchu::icons {

// Loads a PNG/JPG into `out`. Returns true on success.
//
// Monochrome glyphs (black, white, or grayscale gradients) are normalized to a
// white RGB × coverage-alpha mask — luminance is baked into alpha so mid-tones
// survive when chrome tints the icon white or black. Colour artwork is uploaded
// unchanged (*outAdaptive false → identity white tint).
bool loadMaybeAdaptiveIcon(nxui::Texture& out,
                           nxui::GpuDevice& gpu,
                           nxui::Renderer& ren,
                           const std::string& path,
                           bool* outAdaptive,
                           int maxSide = 128);

} // namespace switchu::icons
