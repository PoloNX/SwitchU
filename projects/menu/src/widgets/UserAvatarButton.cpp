#include "UserAvatarButton.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>

void UserAvatarButton::onContentRender(nxui::Renderer& ren) {
    // Render directly in the full widget rect (56x56) with the set corner radius (28).
    // This ensures a perfect circle when radius = half-size.
    float a = opacity();

    if (!m_ownTex.valid()) {
        // Placeholder when avatar image is not loaded.
        ren.drawRoundedRect(m_rect, nxui::Color(0.5f, 0.5f, 0.5f, 0.4f * a), cornerRadius());
        return;
    }

    ren.drawTextureRounded(&m_ownTex, m_rect, cornerRadius(), nxui::Color::white().withAlpha(a));
}
