#pragma once

#include <cstddef>
#include <cstdint>

namespace nxui {

class Font;
class GpuDevice;
class Renderer;

// Nintendo shared fonts via pl:u (Standard includes Japanese; Chinese + Korean
// faces are attached as glyph fallbacks). Safe to call repeatedly.
namespace shared_fonts {

// Ensure pl shared-font faces are mapped. Returns false if none loaded.
bool ensureLoaded();

// Number of successfully opened shared-font blobs (0 if unavailable).
std::size_t faceCount();

// Load UI text faces into `font` at ptSize: Standard primary, then Simplified /
// Ext-Simplified / Traditional Chinese and Korean as fallbacks. Falls back to
// false if shared fonts are unavailable (caller should open DejaVu).
bool loadUiFont(Font& font, GpuDevice& gpu, Renderer& ren, int ptSize);

// Attach Chinese/Korean (and Standard, if not already primary) as fallbacks on
// an already-loaded theme/custom font so CJK titles still render.
bool attachFallbacks(Font& font);

} // namespace shared_fonts

} // namespace nxui
