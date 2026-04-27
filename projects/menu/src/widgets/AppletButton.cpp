#include "AppletButton.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>


AppletButton::AppletButton() {
    setCornerRadius(16.f);
    setPadding(6.f);
    setLiquidGlassEnabled(true);
    setBlurEnabled(false);
    setBorderWidth(0.f);
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
}

void AppletButton::onContentRender(nxui::Renderer& ren) {
    if (!m_icon || !m_icon->valid()) return;

    nxui::Rect cr = contentRect();

    float iconSz = std::min(cr.width, cr.height);
    float ix = cr.x + (cr.width  - iconSz) * 0.5f;
    float iy = cr.y + (cr.height - iconSz) * 0.5f;

    float iconCorner = m_iconCircular ? (iconSz * 0.5f) : (cornerRadius() - 4.f);
    ren.drawTextureRounded(m_icon, {ix, iy, iconSz, iconSz},
                           iconCorner,
                           nxui::Color::white().withAlpha(m_opacity));
}

