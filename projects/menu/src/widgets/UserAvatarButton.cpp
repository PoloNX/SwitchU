#include "UserAvatarButton.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>

UserAvatarButton::UserAvatarButton() {
    setCornerRadius(28.f);
    setPadding(0.f);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setForceLiquidGlass(false);
    setPanelOpacity(0.90f);
    setBorderWidth(1.f);
    setPadding(4.f);
    setBaseColor(nxui::Color(0.20f, 0.22f, 0.28f, 0.94f));
    setBorderColor(nxui::Color::white().withAlpha(0.24f));
    setHighlightColor(nxui::Color::white().withAlpha(0.08f));
}

void UserAvatarButton::setTheme(const nxui::Theme* theme) {
    if (!theme)
        return;
    setBaseColor(theme->iconDefault.withAlpha(theme->mode == nxui::ThemeMode::Dark ? 0.92f : 0.94f));
    setBorderColor(theme->panelBorder.withAlpha(0.36f));
    setHighlightColor(theme->panelHighlight.withAlpha(0.10f));
}

void UserAvatarButton::setAddUserMode(bool enabled) {
    m_addUserMode = enabled;
    if (!enabled)
        return;
    auto& i18n = nxui::I18n::instance();
    m_nickname = i18n.tr("userselect.add_user", "Add user");
    setAccessibilityLabel(m_nickname);
    setAccessibilityRole(i18n.tr("accessibility.roles.action", "action"));
    setAccessibilityHint(i18n.tr("userselect.add_user_hint",
                                  "A to create a new user."));
}

void UserAvatarButton::setChromeEnabled(bool enabled) {
    m_chromeEnabled = enabled;
    setPadding(enabled ? 4.f : 0.f);
    setLiquidGlassEnabled(enabled);
    setForceLiquidGlass(enabled);
    setPanelOpacity(enabled ? 0.90f : 0.f);
    setBorderWidth(enabled ? 1.8f : 0.f);
}

void UserAvatarButton::loadAvatar(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const void* data, std::size_t size) {
    m_avatarTexture.loadFromMemory(gpu, ren, static_cast<const std::uint8_t*>(data), size, 96);
}

void UserAvatarButton::onContentRender(nxui::Renderer& ren) {
    const float alpha = opacity();
    nxui::Rect avatarRect = m_chromeEnabled ? glassContentRect() : rect();
    const float side = std::min(avatarRect.width, avatarRect.height);
    avatarRect.x += (avatarRect.width - side) * 0.5f;
    avatarRect.y += (avatarRect.height - side) * 0.5f;
    avatarRect.width = side;
    avatarRect.height = side;
    const float radius = m_chromeEnabled
        ? std::max(0.f, cornerRadius() - padding().top)
        : side * 0.5f;

    if (m_addUserMode) {
        const nxui::Vec2 center{avatarRect.x + side * 0.5f,
                                avatarRect.y + side * 0.5f};
        const float arm = side * 0.25f;
        const float bar = std::max(3.f, side * 0.075f);
        const nxui::Color plus = nxui::Color::white().withAlpha(0.92f * alpha);
        ren.drawRoundedRect({center.x - arm, center.y - bar * 0.5f,
                             arm * 2.f, bar}, plus, bar * 0.5f);
        ren.drawRoundedRect({center.x - bar * 0.5f, center.y - arm,
                             bar, arm * 2.f}, plus, bar * 0.5f);
        return;
    }

    if (m_avatarTexture.valid()) {
        ren.drawTextureRounded(&m_avatarTexture,
                               avatarRect,
                               radius,
                               nxui::Color::white().withAlpha(alpha));
        return;
    }

    ren.drawRoundedRect(avatarRect,
                        nxui::Color(0.42f, 0.42f, 0.50f, 0.42f * alpha),
                        radius);
}
