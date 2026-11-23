// ui/Theme.cpp
#include <nxui/Theme.hpp>

namespace nxui {

Theme Theme::dark() {
    Theme t;
    t.mode              = ThemeMode::Dark;
    // Cool charcoal — less purple homebrew default.
    t.background        = Color(0.07f, 0.08f, 0.10f, 1.f);
    t.backgroundAccent  = Color(0.10f, 0.11f, 0.14f, 1.f);
    t.panelBase         = Color(0.14f, 0.15f, 0.18f, 0.94f);
    // Soft cool gray — pure white borders read too loud in Settings/Theme Shop.
    t.panelBorder       = Color(0.62f, 0.66f, 0.74f, 0.18f);
    t.panelHighlight    = Color(1.f, 1.f, 1.f, 0.08f);
    t.iconDefault       = Color(0.16f, 0.17f, 0.20f, 0.48f);
    t.cursorNormal      = Color(0.38f, 0.62f, 1.f, 1.f);
    t.cursorGlow        = Color(0.38f, 0.62f, 1.f, 0.12f);
    t.textPrimary       = Color(0.96f, 0.97f, 0.99f, 1.f);
    t.textSecondary     = Color(0.70f, 0.73f, 0.80f, 0.85f);
    t.pageIndicator     = Color(1.f, 1.f, 1.f, 0.22f);
    t.pageIndicatorActive = t.cursorNormal;
    t.shapeColor        = Color(0.35f, 0.42f, 0.58f, 0.08f);
    t.iconCornerRadius   = 20.f;
    t.cursorCornerRadius = 22.f;
    t.cursorBorderWidth  = 2.5f;
    t.panelCornerRadius  = 18.f;
    t.cellCornerRadius   = 16.f;
    return t;
}

Theme Theme::light() {
    Theme t;
    t.mode              = ThemeMode::Light;
    t.background        = Color(0.93f, 0.94f, 0.96f, 1.f);
    t.backgroundAccent  = Color(0.88f, 0.90f, 0.94f, 1.f);
    t.panelBase         = Color(1.f, 1.f, 1.f, 0.96f);
    t.panelBorder       = Color(0.08f, 0.10f, 0.14f, 0.14f);
    t.panelHighlight    = Color(1.f, 1.f, 1.f, 0.40f);
    t.iconDefault       = Color(1.f, 1.f, 1.f, 0.62f);
    t.cursorNormal      = Color(0.12f, 0.48f, 0.98f, 1.f);
    t.cursorGlow        = Color(0.12f, 0.48f, 0.98f, 0.14f);
    t.textPrimary       = Color(0.08f, 0.09f, 0.12f, 1.f);
    t.textSecondary     = Color(0.28f, 0.30f, 0.36f, 0.88f);
    t.pageIndicator     = Color(0.28f, 0.30f, 0.36f, 0.28f);
    t.pageIndicatorActive = t.cursorNormal;
    t.shapeColor        = Color(0.45f, 0.55f, 0.78f, 0.08f);
    t.iconCornerRadius   = 20.f;
    t.cursorCornerRadius = 22.f;
    t.cursorBorderWidth  = 2.5f;
    t.panelCornerRadius  = 18.f;
    t.cellCornerRadius   = 16.f;
    return t;
}

} // namespace nxui
