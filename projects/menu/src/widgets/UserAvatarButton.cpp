#include "UserAvatarButton.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>

void UserAvatarButton::onContentRender(nxui::Renderer& ren) {
    nxui::Rect r   = glassContentRect();
    float pad       = padding().top;   // uniform padding set via setPadding(x)
    float rad       = std::max(0.f, cornerRadius() - pad);
    float a         = opacity();

    if (!m_ownTex.valid()) {
        // Placeholder when avatar image is not loaded.
        ren.drawRoundedRect(r, nxui::Color(0.5f, 0.5f, 0.5f, 0.4f * a), rad);
        return;
    }

    ren.drawTextureRounded(&m_ownTex, r, rad, nxui::Color::white().withAlpha(a));
}
