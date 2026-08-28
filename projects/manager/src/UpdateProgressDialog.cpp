#include "UpdateProgressDialog.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>

namespace switchu::manager {

UpdateProgressDialog::UpdateProgressDialog() {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setVisible(false);
    setFocusable(false);
    setFrameworkTouchEnabled(false);
}

void UpdateProgressDialog::show(const std::string& title) {
    m_title = title;
    m_status.clear();
    m_downloadProgress = 0.f;
    m_installProgress = 0.f;
    m_active = true;
    setVisible(true);
}

void UpdateProgressDialog::setProgress(float downloadProgress, float installProgress,
                                       const std::string& status) {
    m_downloadProgress = std::clamp(downloadProgress, 0.f, 1.f);
    m_installProgress = std::clamp(installProgress, 0.f, 1.f);
    m_status = status;
}

void UpdateProgressDialog::hide() {
    m_active = false;
    setVisible(false);
}

void UpdateProgressDialog::drawProgressBar(nxui::Renderer& renderer,
                                           const nxui::Rect& track,
                                           const std::string& label,
                                           float progress) const {
    if (!m_theme || !m_bodyFont)
        return;
    const float value = std::clamp(progress, 0.f, 1.f);
    renderer.drawText(label, {track.x, track.y - 34.f}, m_bodyFont,
                      m_theme->textPrimary, 0.76f);

    const std::string percent = std::to_string(
        static_cast<int>(std::round(value * 100.f))) + "%";
    const float percentWidth = m_bodyFont->measure(percent).x * 0.72f;
    renderer.drawText(percent, {track.right() - percentWidth, track.y - 34.f},
                      m_bodyFont, m_theme->textSecondary, 0.72f);

    renderer.drawRoundedRect(track, m_theme->panelBorder.withAlpha(0.28f), 9.f);
    if (value > 0.f) {
        nxui::Rect fill = track;
        fill.width = std::max(track.height, track.width * value);
        renderer.drawRoundedRect(fill, m_theme->cursorNormal.withAlpha(0.92f), 9.f);
    }
}

void UpdateProgressDialog::onRender(nxui::Renderer& renderer) {
    if (!m_active || !m_theme)
        return;

    renderer.drawRect(rect(), nxui::Color(0.f, 0.f, 0.f, 0.72f));
    const nxui::Rect panel{300.f, 164.f, 680.f, 392.f};
    renderer.drawRoundedRect(panel, m_theme->panelBase.withAlpha(0.98f), 30.f);
    renderer.drawRoundedRectOutline(panel, m_theme->panelBorder.withAlpha(0.56f),
                                    30.f, 1.5f);
    renderer.drawRoundedRectOutline(panel.shrunk(2.f),
                                    m_theme->panelHighlight.withAlpha(0.12f),
                                    28.f, 1.f);

    if (m_titleFont) {
        const float titleWidth = m_titleFont->measure(m_title).x;
        renderer.drawText(m_title, {640.f - titleWidth * 0.5f, panel.y + 38.f},
                          m_titleFont, m_theme->textPrimary, 1.f);
    }
    if (m_bodyFont) {
        const float statusWidth = m_bodyFont->measure(m_status).x * 0.76f;
        renderer.drawText(m_status, {640.f - statusWidth * 0.5f, panel.y + 94.f},
                          m_bodyFont, m_theme->textSecondary, 0.76f);
    }

    drawProgressBar(renderer, {panel.x + 52.f, panel.y + 180.f,
                               panel.width - 104.f, 18.f},
                    m_downloadLabel, m_downloadProgress);
    drawProgressBar(renderer, {panel.x + 52.f, panel.y + 274.f,
                               panel.width - 104.f, 18.f},
                    m_installLabel, m_installProgress);
}

} // namespace switchu::manager
