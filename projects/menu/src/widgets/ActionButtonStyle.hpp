#pragma once

#include <nxui/widgets/GlassBox.hpp>
#include <nxui/Theme.hpp>
#include <algorithm>

namespace switchu::ui {

inline void prepareActionButton(nxui::GlassBox& button, float cornerRadius) {
    button.setPadding(8.f, 12.f, 8.f, 12.f);
    button.setAlignItems(nxui::AlignItems::CENTER);
    button.setJustifyContent(nxui::JustifyContent::CENTER);
    button.setCornerRadius(cornerRadius);
    button.setBorderWidth(1.f);
}

inline void applyActionButtonStyle(nxui::GlassBox& button,
                                   const nxui::Theme* theme,
                                   float opacity,
                                   float emphasis,
                                   float accentMix = -1.f) {
    emphasis = std::clamp(emphasis, 0.f, 1.f);
    accentMix = accentMix < 0.f ? emphasis : std::clamp(accentMix, 0.f, 1.f);

    float pulse = 0.18f + 0.16f * emphasis;

    nxui::Color idleBase = theme ? theme->panelBase.withAlpha(0.10f * opacity)
                                 : nxui::Color(0.28f, 0.32f, 0.38f, 0.10f * opacity);
    nxui::Color accentBase = theme ? theme->cursorNormal.withAlpha(pulse * opacity)
                                   : nxui::Color(0.20f, 0.46f, 0.86f, pulse * opacity);
    nxui::Color idleBorder = theme ? theme->panelBorder.withAlpha(0.40f * opacity)
                                   : nxui::Color(0.5f, 0.5f, 0.65f, 0.40f * opacity);
    nxui::Color accentBorder = theme ? theme->panelBorder.withAlpha(0.65f * opacity)
                                     : nxui::Color(0.5f, 0.5f, 0.65f, 0.65f * opacity);
    nxui::Color idleHighlight = theme ? theme->panelHighlight.withAlpha(0.03f * opacity)
                                      : nxui::Color(1.f, 1.f, 1.f, 0.03f * opacity);
    nxui::Color accentHighlight = theme ? theme->panelHighlight.withAlpha(0.09f * opacity)
                                        : nxui::Color(1.f, 1.f, 1.f, 0.09f * opacity);

    button.setBaseColor(nxui::Color::lerp(idleBase, accentBase, accentMix));
    button.setBorderColor(nxui::Color::lerp(idleBorder, accentBorder, accentMix));
    button.setHighlightColor(nxui::Color::lerp(idleHighlight, accentHighlight, accentMix));
    button.setBorderWidth(1.0f + 0.2f * accentMix);
    button.setScale(1.f + 0.03f * accentMix);
}

} // namespace switchu::ui