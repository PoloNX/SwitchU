#pragma once

// Shared spacing and type roles for HOME chrome (HUD, sidebars, hints, widget
// captions). Everything is authored for the 1280x720 logical canvas.
//
// Base fonts are loaded at 18 px (small) and 24 px (normal); text is drawn by
// scaling those, so roles are written in target pixels and converted here
// instead of scattering 0.57 / 0.7 / 0.75 multipliers.
namespace ui {

// One outer margin for everything that hugs a screen edge.
inline constexpr float kMargin = 20.f;

inline constexpr float kSmallFontPx = 18.f;
inline constexpr float kNormalFontPx = 24.f;
inline constexpr float kIconFontPx = 24.f;

// Type roles (px on a 720p canvas). 15 px is the floor for anything that has
// to be read at arm's length on the handheld screen.
inline constexpr float kCaptionPx = 15.f;
inline constexpr float kLabelPx = 16.f;
inline constexpr float kHintIconPx = 21.f;

constexpr float fontScale(float targetPx, float basePx) {
    return targetPx / basePx;
}

} // namespace ui
