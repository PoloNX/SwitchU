#include "OrientationButton.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>

OrientationButton::OrientationButton() {
    setTag("orientationFlip");
    setFocusable(true);
    setActivateOnFirstTouch(true);
    setSize(52.f, 52.f);
    setMinWidth(52.f);
    setMinHeight(52.f);
    setShrink(0.f);
    setCornerRadius(26.f);
    setPadding(10.f);
    setLiquidGlassEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
    setPanelOpacity(0.90f);
    setBorderWidth(1.6f);
    setBaseColor(nxui::Color(0.20f, 0.22f, 0.28f, 0.94f));
    setBorderColor(nxui::Color::white().withAlpha(0.24f));
    setHighlightColor(nxui::Color::white().withAlpha(0.08f));
    refreshAccessibility();
}

void OrientationButton::setTheme(const nxui::Theme* theme) {
    m_theme = theme;
    if (!theme)
        return;
    setBaseColor(theme->iconDefault.withAlpha(
        theme->mode == nxui::ThemeMode::Dark ? 0.92f : 0.94f));
    setBorderColor(theme->panelBorder);
    setHighlightColor(theme->panelHighlight);
}

void OrientationButton::setOrientation(HomeOrientation orientation) {
    if (m_orientation == orientation)
        return;
    m_orientation = orientation;
    if (m_pulseArmed)
        m_pulseT = 0.f;
    m_pulseArmed = true;
    refreshAccessibility();
}

void OrientationButton::onUpdate(float dt) {
    if (m_pulseT < kPulseDuration)
        m_pulseT = std::min(kPulseDuration, m_pulseT + dt);
}

void OrientationButton::ensureIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                   const std::string& path) {
    if (m_iconLoadAttempted && m_iconPath == path)
        return;
    m_iconLoadAttempted = true;
    m_iconPath = path;
    m_icon = nxui::Texture();
    m_iconContrastMask = nxui::Texture();
    m_iconAdaptive = false;
    if (path.empty())
        return;
    float meanLuma = 0.5f;
    if (!switchu::icons::loadMaybeAdaptiveIcon(m_icon, gpu, ren, path,
                                               &m_iconAdaptive, 128,
                                               &meanLuma, &m_iconContrastMask,
                                               m_hslAdjust.enabled ? &m_hslAdjust : nullptr)) {
        m_iconAdaptive = false;
        return;
    }
    m_iconMeanLuma = meanLuma;
}

void OrientationButton::reloadIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                   const std::string& path) {
    m_iconLoadAttempted = false;
    m_iconAdaptive = false;
    ensureIcon(gpu, ren, path);
}

void OrientationButton::setColourIconContrast(bool enabled) {
    m_colourContrastAdapt = enabled;
}

void OrientationButton::setIconHslAdjust(const switchu::icons::HslAdjust& adj) {
    m_hslAdjust = adj;
}

void OrientationButton::refreshAccessibility() {
    auto& i18n = nxui::I18n::instance();
    setAccessibilityLabel(i18n.tr("hint.switch_orientation", "Switch orientation"));
    setAccessibilityRole(i18n.tr("accessibility.roles.button", "button"));
    const char* detail = "Landscape";
    switch (m_orientation) {
        case HomeOrientation::Rot90:  detail = "Portrait 90 degrees"; break;
        case HomeOrientation::Rot180: detail = "Landscape 180 degrees"; break;
        case HomeOrientation::Rot270: detail = "Portrait 270 degrees"; break;
        case HomeOrientation::Landscape0:
        default: break;
    }
    setAccessibilityHint(std::string(detail) + ". "
        + i18n.tr("layout.cycle_orientation", "Cycle HOME screen orientation."));
}

void OrientationButton::drawFallbackIcon(nxui::Renderer& ren, const nxui::Color& ink,
                                         float scale) {
    nxui::Rect c = glassContentRect();
    const float side = std::min(c.width, c.height) * scale;
    const float cx = c.x + c.width * 0.5f;
    const float cy = c.y + c.height * 0.5f;
    // Display glyph for the *current* orientation (status, not “next”).
    // Landscape = wide panel; portrait = tall panel; 180 flips the chin bar.
    const bool portrait = m_orientation == HomeOrientation::Rot90
        || m_orientation == HomeOrientation::Rot270;
    const float rw = side * (portrait ? 0.22f : 0.40f);
    const float rh = side * (portrait ? 0.36f : 0.24f);
    const float radius = std::min(rw, rh) * 0.22f;
    const nxui::Rect panel{cx - rw, cy - rh, rw * 2.f, rh * 2.f};
    ren.drawRoundedRect(panel, ink.withAlpha(ink.a * 0.22f), radius);
    ren.drawRoundedRectOutline(panel, ink, radius, 2.15f);

    // Chin / home-indicator bar — bottom for 0/90, top for 180/270 so the
    // glyph also reads “flipped” without a rotate arrow.
    const bool flipped = m_orientation == HomeOrientation::Rot180
        || m_orientation == HomeOrientation::Rot270;
    const float barW = rw * (portrait ? 0.70f : 0.55f);
    const float barH = std::max(2.f, side * 0.045f);
    const float barY = flipped
        ? (panel.y + rh * 0.22f)
        : (panel.bottom() - rh * 0.22f - barH);
    ren.drawRoundedRect({cx - barW * 0.5f, barY, barW, barH},
                        ink.withAlpha(ink.a * 0.95f), barH * 0.5f);
}

void OrientationButton::resolveDrawStyle(nxui::Color& ink,
                                         const nxui::Texture*& tex) const {
    const float a = opacity();
    tex = m_icon.valid() ? &m_icon : nullptr;

    if (m_iconAdaptive && m_theme) {
        ink = m_theme->textPrimary.withAlpha(0.92f * a);
        return;
    }

    if (m_colourContrastAdapt && !m_iconAdaptive && m_iconContrastMask.valid() && m_theme) {
        tex = &m_iconContrastMask;
        ink = m_theme->textPrimary.withAlpha(0.92f * a);
        return;
    }

    ink = nxui::Color::white().withAlpha(0.92f * a);
}

void OrientationButton::onContentRender(nxui::Renderer& ren) {
    // Always draw the procedural display glyph — pack “rotate” icons read poorly
    // at chrome size and cannot show the active orientation.
    nxui::Color ink = nxui::Color::white().withAlpha(0.92f * opacity());
    if (m_theme)
        ink = m_theme->textPrimary.withAlpha(0.92f * opacity());

    float scale = 1.f;
    if (m_pulseT < kPulseDuration) {
        const float t = std::clamp(m_pulseT / kPulseDuration, 0.f, 1.f);
        scale = 1.f + 0.16f * std::sin(t * 3.14159265f) * (1.f - t * 0.5f);
    }
    drawFallbackIcon(ren, ink, scale);
}
