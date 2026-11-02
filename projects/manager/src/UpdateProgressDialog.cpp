#include "UpdateProgressDialog.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>
#include <string>

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
    m_overallProgress = 0.f;
    m_hasOverall = false;
    m_cancellable = false;
    m_active = true;
    setVisible(true);
}

void UpdateProgressDialog::setProgress(float downloadProgress, float installProgress,
                                       const std::string& status, float overallProgress,
                                       bool cancellable) {
    m_downloadProgress = std::clamp(downloadProgress, 0.f, 1.f);
    m_installProgress = std::clamp(installProgress, 0.f, 1.f);
    m_status = status;
    m_hasOverall = overallProgress >= 0.f;
    m_overallProgress = m_hasOverall ? std::clamp(overallProgress, 0.f, 1.f) : 0.f;
    m_cancellable = cancellable;
}

void UpdateProgressDialog::hide() {
    m_active = false;
    m_cancellable = false;
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
    const float percentWidth = m_bodyFont->measure(percent).x * 0.78f;
    renderer.drawText(percent, {track.right() - percentWidth, track.y - 34.f},
                      m_bodyFont, m_theme->textSecondary, 0.78f);

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
    const nxui::Rect panel{260.f, 132.f, 760.f, 456.f};
    renderer.drawRoundedRect(panel, m_theme->panelBase.withAlpha(0.98f), 30.f);
    renderer.drawRoundedRectOutline(panel, m_theme->panelBorder.withAlpha(0.56f),
                                    30.f, 1.5f);
    renderer.drawRoundedRectOutline(panel.shrunk(2.f),
                                    m_theme->panelHighlight.withAlpha(0.12f),
                                    28.f, 1.f);

    if (m_titleFont) {
        const float titleWidth = m_titleFont->measure(m_title).x;
        renderer.drawText(m_title, {640.f - titleWidth * 0.5f, panel.y + 30.f},
                          m_titleFont, m_theme->textPrimary, 1.f);
    }

    if (m_hasOverall && m_titleFont) {
        const std::string overall = std::to_string(
            static_cast<int>(std::round(m_overallProgress * 100.f))) + "%";
        const float overallWidth = m_titleFont->measure(overall).x * 0.92f;
        renderer.drawText(overall, {640.f - overallWidth * 0.5f, panel.y + 78.f},
                          m_titleFont, m_theme->cursorNormal, 0.92f);
        drawProgressBar(renderer,
                        {panel.x + 52.f, panel.y + 148.f, panel.width - 104.f, 16.f},
                        "Overall", m_overallProgress);
    }

    if (m_bodyFont && !m_status.empty()) {
        const float statusScale = 0.78f;
        const float statusWidth = m_bodyFont->measure(m_status).x * statusScale;
        const float statusY = m_hasOverall ? panel.y + 190.f : panel.y + 94.f;
        renderer.drawText(m_status, {640.f - statusWidth * 0.5f, statusY},
                          m_bodyFont, m_theme->textSecondary, statusScale);
    }

    const float barsTop = m_hasOverall ? panel.y + 250.f : panel.y + 180.f;
    drawProgressBar(renderer, {panel.x + 52.f, barsTop,
                               panel.width - 104.f, 18.f},
                    m_downloadLabel, m_downloadProgress);
    drawProgressBar(renderer, {panel.x + 52.f, barsTop + 94.f,
                               panel.width - 104.f, 18.f},
                    m_installLabel, m_installProgress);

    if (m_cancellable && m_bodyFont) {
        const std::string hint = "B  Cancel";
        const float hintScale = 0.82f;
        const float hintWidth = m_bodyFont->measure(hint).x * hintScale;
        const nxui::Rect pill{
            640.f - (hintWidth + 48.f) * 0.5f,
            panel.bottom() - 58.f,
            hintWidth + 48.f,
            36.f};
        renderer.drawRoundedRect(pill, m_theme->panelBorder.withAlpha(0.35f), 18.f);
        renderer.drawRoundedRectOutline(pill, m_theme->cursorNormal.withAlpha(0.55f),
                                        18.f, 1.4f);
        renderer.drawText(hint, {pill.x + 24.f, pill.y + 8.f},
                          m_bodyFont, m_theme->textPrimary, hintScale);
    }
}

} // namespace switchu::manager
