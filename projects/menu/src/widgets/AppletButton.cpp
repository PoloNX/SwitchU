#include "AppletButton.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>


AppletButton::AppletButton() {
    setCornerRadius(16.f);
    setPadding(6.f);
    setLiquidGlassEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
    setBorderWidth(2.2f);
    m_i18nListenerId = nxui::I18n::instance().addLanguageChangedListener([this]() {
        refreshLocalizedLabel();
    });
}

AppletButton::~AppletButton() {
    nxui::I18n::instance().removeLanguageChangedListener(m_i18nListenerId);
}

void AppletButton::setLabel(const std::string& l) {
    m_useLabelKey = false;
    m_labelSource = l;
    refreshLocalizedLabel();
}

void AppletButton::setLabelKey(const std::string& key, const std::string& fallback) {
    m_useLabelKey = true;
    m_labelKey = key;
    m_labelFallback = fallback.empty() ? key : fallback;
    refreshLocalizedLabel();
}

void AppletButton::refreshLocalizedLabel() {
    if (m_useLabelKey)
        m_label = nxui::I18n::instance().tr(m_labelKey, m_labelFallback);
    else
        m_label = nxui::I18n::instance().tr(m_labelSource, m_labelSource);
    setAccessibilityLabel(m_label);
}

void AppletButton::syncIconTint() {
    m_drawContrastMask = false;
    if (m_iconAdaptive && !m_iconUsesColorArtwork) {
        m_iconTint = m_iconChromeTint;
        return;
    }

    // Animated WebP frames keep authored colour.
    if (m_iconUsesColorArtwork) {
        m_iconTint = nxui::Color::white();
        return;
    }

    // Theme > Options > Adaptive icon contrast: when armed (single-row + hero),
    // colour packs follow HOME chrome textPrimary — same light/dark flip as
    // mono glyphs. Off / no hero keeps authored (or custom-tint) pixels.
    if (m_colourContrastAdapt && m_iconContrastMask && m_iconContrastMask->valid()
        && !m_iconAdaptive) {
        m_drawContrastMask = true;
        m_iconTint = m_iconChromeTint;
        return;
    }

    // Custom tint is baked into the colour texture via HSL adjust on load —
    // draw authored (shifted) pixels with identity multiply.
    m_iconTint = nxui::Color::white();
}

void AppletButton::onContentRender(nxui::Renderer& ren) {
    nxui::Texture* tex = m_icon;
    if (m_drawContrastMask && m_iconContrastMask && m_iconContrastMask->valid())
        tex = m_iconContrastMask;
    if (!tex || !tex->valid()) return;

    nxui::Rect cr = contentRect();

    float iconSz = std::min(cr.width, cr.height);
    float ix = cr.x + (cr.width  - iconSz) * 0.5f;
    float iy = cr.y + (cr.height - iconSz) * 0.5f;

    float iconCorner = m_iconCircular ? (iconSz * 0.5f) : (cornerRadius() - 4.f);
    ren.drawTextureRounded(tex, {ix, iy, iconSz, iconSz},
                           iconCorner,
                           m_iconTint.withAlpha(m_iconTint.a * m_opacity));
}
