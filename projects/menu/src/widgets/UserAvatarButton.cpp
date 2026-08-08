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
