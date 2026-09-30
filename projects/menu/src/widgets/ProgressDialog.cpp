#include "ProgressDialog.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace {
nxui::Rect scaledRect(const nxui::Rect& rect, float scale) {
    nxui::Rect out = rect;
    float w = out.width * scale;
    float h = out.height * scale;
    out.x += (out.width - w) * 0.5f;
    out.y += (out.height - h) * 0.5f;
    out.width = w;
    out.height = h;
    return out;
}

std::string ellipsize(nxui::Font* font, const std::string& text, float maxWidth, float scale) {
    if (!font || text.empty() || font->measure(text).x * scale <= maxWidth)
        return text;
    std::string out = text;
    while (!out.empty()) {
        out.pop_back();
        std::string candidate = out + "...";
        if (font->measure(candidate).x * scale <= maxWidth)
            return candidate;
    }
    return "...";
}
}

ProgressDialog::ProgressDialog() {
    // The visual scrim and the touch target cover the whole screen. This makes
    // the dialog genuinely modal for both controller and touch input.
    setRect({0.f, 0.f, 1280.f, 720.f});
    setVisible(false);
    setFocusable(true);
    setCornerRadius(kPanelRadius);
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setLiquidGlassShaderEnabled(false);
    setBlurEnabled(false);
    setPanelOpacity(0.90f);
    setFrameworkTouchEnabled(false);
    wireCancelAction();
}

void ProgressDialog::wireCancelAction() {
    clearActions();
    addAction(static_cast<std::uint64_t>(nxui::Button::B), [this]() {
        requestCancel();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::A), [this]() {
        requestCancel();
    });
}

void ProgressDialog::setCancellable(bool cancellable, std::function<void()> onCancel) {
    m_cancellable = cancellable;
    m_onCancel = std::move(onCancel);
    if (!cancellable)
        m_cancelRequested = false;
}

void ProgressDialog::requestCancel() {
    if (!m_active || !m_cancellable || m_cancelRequested)
        return;
    m_cancelRequested = true;
    if (m_onCancel)
        m_onCancel();
}

nxui::Rect ProgressDialog::panelRect() const {
    return {(1280.f - kPanelW) * 0.5f, (720.f - kPanelH) * 0.5f, kPanelW, kPanelH};
}

nxui::Rect ProgressDialog::cancelButtonRect(const nxui::Rect& panel) const {
    constexpr float btnW = 196.f;
    constexpr float btnH = 46.f;
    return {panel.x + (panel.width - btnW) * 0.5f, panel.bottom() - 64.f, btnW, btnH};
}

void ProgressDialog::show(const std::string& title, const std::string& message, float progress01) {
    m_title = title;
    m_message = message;
    m_progress01 = progress01;
    m_active = true;
    m_animatingOut = false;
    m_cancelRequested = false;
    m_touchOnCancel = false;
    m_overlayAlpha.setImmediate(0.f);
    m_overlayAlpha.set(1.f, 0.18f, nxui::Easing::outCubic);
    m_panelScale.setImmediate(0.92f);
    m_panelScale.set(1.f, 0.20f, nxui::Easing::outCubic);
    m_progressAnim.setImmediate(std::clamp(progress01, 0.f, 1.f));
    setVisible(true);
}

void ProgressDialog::updateState(const std::string& message, float progress01) {
    m_message = message;
    m_progress01 = progress01;
    if (progress01 >= 0.f)
        m_progressAnim.set(std::clamp(progress01, 0.f, 1.f), 0.16f, nxui::Easing::outCubic);
}

void ProgressDialog::hide() {
    if (!m_active)
        return;
    m_active = false;
    m_animatingOut = true;
    m_cancellable = false;
    m_onCancel = {};
    m_cancelRequested = false;
    m_touchOnCancel = false;
    m_overlayAlpha.set(0.f, 0.16f, nxui::Easing::outCubic);
    m_panelScale.set(0.96f, 0.16f, nxui::Easing::outCubic);
}

void ProgressDialog::handleTouch(nxui::Input& input) {
    if (!m_active || !m_cancellable || m_cancelRequested)
        return;
    const nxui::Rect btn = cancelButtonRect(panelRect());
    if (input.touchDown() && btn.contains(input.touchX(), input.touchY())) {
        m_touchOnCancel = true;
        return;
    }
    if (input.touchUp()) {
        const bool wasOn = m_touchOnCancel;
        m_touchOnCancel = false;
        if (wasOn && btn.contains(input.touchX(), input.touchY()))
            requestCancel();
    }
}

void ProgressDialog::update(float dt) {
    m_overlayAlpha.update(dt);
    m_panelScale.update(dt);
    m_progressAnim.update(dt);
    m_spinnerT += dt;
    if (m_animatingOut && m_overlayAlpha.value() <= 0.01f) {
        m_animatingOut = false;
        setVisible(false);
    }
}

void ProgressDialog::render(nxui::Renderer& ren) {
    if (!isActive() || !m_theme)
        return;

    float alpha = m_overlayAlpha.value();
    if (alpha <= 0.01f)
        return;

    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, nxui::Color(0.f, 0.f, 0.f, 0.50f * alpha));

    nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    nxui::Color panelFill = m_theme->panelBase.withAlpha((m_theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f) * alpha);
    ren.drawRoundedRect(panel, panelFill, kPanelRadius);
    ren.drawRoundedRectOutline(panel, m_theme->panelBorder.withAlpha(0.34f * alpha), kPanelRadius, 1.2f);
    ren.drawRoundedRectOutline(panel.shrunk(1.5f), m_theme->panelHighlight.withAlpha(0.08f * alpha), kPanelRadius - 1.5f, 1.f);

    float pad = 38.f;
    if (m_font) {
        ren.drawText(ellipsize(m_font, m_title, panel.width - pad * 2.f, 1.f),
                     {panel.x + pad, panel.y + 34.f}, m_font,
                     m_theme->textPrimary.withAlpha(alpha), 1.f);
    }
    if (m_smallFont) {
        ren.drawText(ellipsize(m_smallFont, m_message, panel.width - pad * 2.f, 0.80f),
                     {panel.x + pad, panel.y + 86.f}, m_smallFont,
                     m_theme->textSecondary.withAlpha(0.94f * alpha), 0.80f);
    }

    nxui::Rect track = {panel.x + pad, panel.y + 140.f, panel.width - pad * 2.f, 18.f};
    ren.drawRoundedRect(track, m_theme->panelBorder.withAlpha(0.22f * alpha), 9.f);

    if (m_progress01 >= 0.f) {
        float p = std::clamp(m_progressAnim.value(), 0.f, 1.f);
        nxui::Rect fill = track;
        fill.width = std::max(18.f, track.width * p);
        ren.drawRoundedRect(fill, m_theme->cursorNormal.withAlpha(0.88f * alpha), 9.f);
        if (m_smallFont) {
            std::string pct = std::to_string((int)std::round(p * 100.f)) + "%";
            nxui::Vec2 pctSize = m_smallFont->measure(pct);
            ren.drawText(pct, {track.right() - pctSize.x * 0.72f, track.bottom() + 12.f},
                         m_smallFont, m_theme->textPrimary.withAlpha(alpha), 0.72f);
        }
    } else {
        float segmentW = track.width * 0.28f;
        float travel = track.width - segmentW;
        float phase = std::fmod(m_spinnerT * 0.65f, 1.f);
        nxui::Rect fill = {track.x + travel * phase, track.y, segmentW, track.height};
        ren.drawRoundedRect(fill, m_theme->cursorNormal.withAlpha(0.82f * alpha), 9.f);
    }

    if (m_cancellable) {
        auto& i18n = nxui::I18n::instance();
        const nxui::Rect btn = cancelButtonRect(panel);
        const bool pressed = m_touchOnCancel || m_cancelRequested;
        ren.drawRoundedRect(btn,
            m_theme->panelBorder.withAlpha((pressed ? 0.42f : 0.28f) * alpha), 12.f);
        ren.drawRoundedRectOutline(btn, m_theme->cursorNormal.withAlpha(0.55f * alpha), 12.f, 1.6f);
        if (m_smallFont) {
            const std::string label = m_cancelRequested
                ? i18n.tr("settings.steamgriddb.cancelling", "Cancelling...")
                : i18n.tr("button.cancel", "Cancel");
            const nxui::Vec2 size = m_smallFont->measure(label);
            ren.drawText(label,
                         {btn.x + (btn.width - size.x * 0.84f) * 0.5f,
                          btn.y + (btn.height - size.y * 0.84f) * 0.5f},
                         m_smallFont, m_theme->textPrimary.withAlpha(alpha), 0.84f);
        }
    }
}
