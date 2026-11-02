#include "UpdateProgressDialog.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace switchu::manager {
namespace {

bool navigatedTowardCancel(const nxui::Input& input) {
    return input.isDown(nxui::Button::DDown)
        || input.isDown(nxui::Button::DLeft)
        || input.isDown(nxui::Button::DRight)
        || input.isDown(nxui::Button::DUp)
        || input.isDown(nxui::Button::LStickD)
        || input.isDown(nxui::Button::LStickL)
        || input.isDown(nxui::Button::LStickR)
        || input.isDown(nxui::Button::LStickU);
}

} // namespace

UpdateProgressDialog::UpdateProgressDialog() {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setVisible(false);
    setFocusable(false);
    setFrameworkTouchEnabled(false);
}

void UpdateProgressDialog::show(const std::string& title) {
    m_title = title;
    m_status = "Preparing";
    m_overallProgress = 0.f;
    m_cancellable = false;
    m_cancelFocused = false;
    m_cancelRequested = false;
    m_touchOnCancel = false;
    m_awaitButtonRelease = true;
    m_active = true;
    setVisible(true);
}

void UpdateProgressDialog::setProgress(float overallProgress, const std::string& status,
                                       bool cancellable) {
    m_overallProgress = std::clamp(overallProgress, 0.f, 1.f);
    // Never clear status mid-update — blank frames confuse users.
    if (!status.empty())
        m_status = status;
    if (cancellable && !m_cancellable) {
        // Start unfocused so the Install-confirm A press cannot cancel, and so
        // accidental A does not abort the download.
        m_cancelFocused = false;
        m_awaitButtonRelease = true;
    }
    m_cancellable = cancellable;
    if (!m_cancellable) {
        m_cancelFocused = false;
        m_touchOnCancel = false;
        m_awaitButtonRelease = false;
    }
}

void UpdateProgressDialog::hide() {
    m_active = false;
    m_cancellable = false;
    m_cancelFocused = false;
    m_cancelRequested = false;
    m_touchOnCancel = false;
    m_awaitButtonRelease = false;
    setVisible(false);
}

bool UpdateProgressDialog::consumeCancelRequest() {
    if (!m_cancelRequested)
        return false;
    m_cancelRequested = false;
    return true;
}

nxui::Rect UpdateProgressDialog::cancelRect() const {
    constexpr float kW = 220.f;
    constexpr float kH = 48.f;
    const nxui::Rect panel{300.f, 176.f, 680.f, 368.f};
    return {640.f - kW * 0.5f, panel.bottom() - 70.f, kW, kH};
}

void UpdateProgressDialog::handleInput(nxui::Input& input) {
    if (!m_active || !m_cancellable)
        return;

    const nxui::Rect hit = cancelRect().expanded(8.f);
    if (input.touchDown()) {
        m_touchOnCancel = hit.contains(input.touchX(), input.touchY());
        if (m_touchOnCancel)
            m_cancelFocused = true;
    }
    if (input.touchUp()) {
        if (m_touchOnCancel && hit.contains(input.touchX(), input.touchY()))
            m_cancelRequested = true;
        m_touchOnCancel = false;
    }

    if (navigatedTowardCancel(input))
        m_cancelFocused = true;

    // Wait out the Install-confirm A hold so isDown(A) cannot fire cancel.
    const bool aHeld = input.isHeld(nxui::Button::A);
    const bool bHeld = input.isHeld(nxui::Button::B);
    if (m_awaitButtonRelease) {
        if (!aHeld && !bHeld)
            m_awaitButtonRelease = false;
        return;
    }

    // B always cancels. A only cancels when Cancel is focused.
    if (input.isDown(nxui::Button::B))
        m_cancelRequested = true;
    else if (m_cancelFocused && input.isDown(nxui::Button::A))
        m_cancelRequested = true;
}

void UpdateProgressDialog::onRender(nxui::Renderer& renderer) {
    if (!m_active || !m_theme)
        return;

    renderer.drawRect(rect(), nxui::Color(0.f, 0.f, 0.f, 0.72f));
    const nxui::Rect panel{300.f, 176.f, 680.f, 368.f};
    renderer.drawRoundedRect(panel, m_theme->panelBase.withAlpha(0.98f), 30.f);
    renderer.drawRoundedRectOutline(panel, m_theme->panelBorder.withAlpha(0.56f),
                                    30.f, 1.5f);
    renderer.drawRoundedRectOutline(panel.shrunk(2.f),
                                    m_theme->panelHighlight.withAlpha(0.12f),
                                    28.f, 1.f);

    if (m_titleFont) {
        const float titleWidth = m_titleFont->measure(m_title).x;
        renderer.drawText(m_title, {640.f - titleWidth * 0.5f, panel.y + 32.f},
                          m_titleFont, m_theme->textPrimary, 1.f);
    }

    const float value = m_overallProgress;
    const int percentValue = static_cast<int>(std::round(value * 100.f));
    const std::string percentText = std::to_string(percentValue) + "%";
    const nxui::Color accent = m_theme->cursorNormal;

    if (m_titleFont) {
        const float percentScale = 1.15f;
        const float percentWidth = m_titleFont->measure(percentText).x * percentScale;
        renderer.drawText(percentText,
                          {640.f - percentWidth * 0.5f, panel.y + 88.f},
                          m_titleFont, accent, percentScale);
    }

    if (m_bodyFont && !m_status.empty()) {
        const float statusScale = 0.88f;
        const float statusWidth = m_bodyFont->measure(m_status).x * statusScale;
        renderer.drawText(m_status, {640.f - statusWidth * 0.5f, panel.y + 148.f},
                          m_bodyFont, m_theme->textPrimary, statusScale);
    }

    const nxui::Rect track{panel.x + 52.f, panel.y + 210.f, panel.width - 104.f, 22.f};
    renderer.drawRoundedRect(track, m_theme->panelBorder.withAlpha(0.28f), 11.f);
    if (value > 0.f) {
        nxui::Rect fill = track;
        fill.width = std::max(track.height, track.width * value);
        renderer.drawRoundedRect(fill, accent.withAlpha(0.92f), 11.f);
    }

    if (m_cancellable && m_bodyFont) {
        const nxui::Rect pill = cancelRect();
        const float emphasis = m_cancelFocused || m_touchOnCancel ? 1.f : 0.45f;
        renderer.drawRoundedRect(
            pill, m_theme->panelBorder.withAlpha(0.22f + 0.28f * emphasis), 18.f);
        renderer.drawRoundedRectOutline(
            pill, accent.withAlpha(0.28f + 0.50f * emphasis), 18.f,
            1.3f + 0.9f * emphasis);
        const std::string hint = "Cancel";
        const float hintScale = 0.90f;
        const float hintWidth = m_bodyFont->measure(hint).x * hintScale;
        renderer.drawText(hint,
                          {pill.x + (pill.width - hintWidth) * 0.5f, pill.y + 12.f},
                          m_bodyFont, m_theme->textPrimary, hintScale);
        if (m_cancelFocused) {
            renderer.drawRoundedRectOutline(pill.expanded(4.f), accent, 20.f, 2.4f);
        }
    }
}

} // namespace switchu::manager
