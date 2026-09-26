#include "ProfileLockButton.hpp"
#include "ProfileDrawing.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>

ProfileLockButton::ProfileLockButton() {
    setTag("profileLock");
    setFocusable(true);
    setCornerRadius(26.f);
    setPadding(6.f);
    setLiquidGlassEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
    setPanelOpacity(0.90f);
    setBorderWidth(1.8f);
    setBaseColor(nxui::Color(0.20f, 0.22f, 0.28f, 0.94f));
    setBorderColor(nxui::Color::white().withAlpha(0.24f));
    setHighlightColor(nxui::Color::white().withAlpha(0.08f));
    refreshAccessibility();
}

void ProfileLockButton::setTheme(const nxui::Theme* theme) {
    m_theme = theme;
    if (!theme)
        return;
    setBaseColor(theme->iconDefault.withAlpha(theme->mode == nxui::ThemeMode::Dark ? 0.92f : 0.94f));
    setBorderColor(theme->panelBorder);
    setHighlightColor(theme->panelHighlight);
}

void ProfileLockButton::setLockedProfile(std::shared_ptr<nxui::Texture> avatar,
                                         const std::string& nickname) {
    m_avatar = std::move(avatar);
    m_nickname = nickname;
    m_locked = true;
    refreshAccessibility();
}

void ProfileLockButton::clearLockedProfile() {
    m_avatar.reset();
    m_nickname.clear();
    m_locked = false;
    refreshAccessibility();
}

void ProfileLockButton::refreshAccessibility() {
    auto& i18n = nxui::I18n::instance();
    m_statusText = m_locked
        ? i18n.tr("profilelock.pill_locked", "Playing as") + " " + m_nickname
        : i18n.tr("profilelock.pill_unlocked", "Ask who's playing");
    setAccessibilityLabel(m_statusText);
    setAccessibilityRole(i18n.tr("accessibility.roles.profile", "profile"));
    setAccessibilityHint(i18n.tr("profilelock.button_hint", "A to change."));
}

void ProfileLockButton::onContentRender(nxui::Renderer& ren) {
    const float alpha = opacity();
    nxui::Rect avatarRect = glassContentRect();
    const float side = std::min(avatarRect.width, avatarRect.height);
    avatarRect.x += (avatarRect.width - side) * 0.5f;
    avatarRect.y += (avatarRect.height - side) * 0.5f;
    avatarRect.width = side;
    avatarRect.height = side;
    const float radius = std::max(0.f, cornerRadius() - padding().top);

    if (m_locked && m_avatar && m_avatar->valid()) {
        ren.drawTextureRounded(m_avatar.get(), avatarRect, radius,
                               nxui::Color::white().withAlpha(alpha));
    } else {
        const nxui::Color fill = m_theme
            ? m_theme->panelBase.withAlpha(0.55f * alpha)
            : nxui::Color(0.42f, 0.42f, 0.50f, 0.42f * alpha);
        ren.drawRoundedRect(avatarRect, fill, radius);
        const nxui::Color glyph = m_theme
            ? m_theme->textSecondary.withAlpha(0.70f * alpha)
            : nxui::Color::white().withAlpha(0.70f * alpha);
        switchu::ui::drawProfileSilhouette(ren, avatarRect.shrunk(side * 0.12f), glyph);
    }
}
