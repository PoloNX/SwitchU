#include "SelectionCursor.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>


SelectionCursor::SelectionCursor() {
    m_x.setImmediate(0); m_y.setImmediate(0);
    m_w.setImmediate(0); m_h.setImmediate(0);
    m_cornerRadius.setImmediate(18.f);
}

void SelectionCursor::setInstantMotion(bool instant) {
    m_instantMotion = instant;
    if (!instant || !m_initialized)
        return;

    // Switching modes while the cursor is already travelling should take
    // effect immediately, rather than waiting for the next focus change.
    m_x.setImmediate(m_x.target());
    m_y.setImmediate(m_y.target());
    m_w.setImmediate(m_w.target());
    m_h.setImmediate(m_h.target());
    m_cornerRadius.setImmediate(m_cornerRadius.target());
    m_teleportPulse = 1.f;
}

float SelectionCursor::computeAdaptiveDuration(const nxui::Rect& target,
                                               float targetCornerRadius,
                                               float baseDuration) const {
    float sx = m_x.value();
    float sy = m_y.value();
    float sw = std::max(1.f, m_w.value());
    float sh = std::max(1.f, m_h.value());
    float sr = m_cornerRadius.value();

    float sourceCx = sx + sw * 0.5f;
    float sourceCy = sy + sh * 0.5f;
    float targetCx = target.x + target.width * 0.5f;
    float targetCy = target.y + target.height * 0.5f;

    float dx = targetCx - sourceCx;
    float dy = targetCy - sourceCy;
    float distance = std::sqrt(dx * dx + dy * dy);

    float sourceDiag = std::sqrt(sw * sw + sh * sh);
    float targetW = std::max(1.f, target.width);
    float targetH = std::max(1.f, target.height);
    float targetDiag = std::sqrt(targetW * targetW + targetH * targetH);

    float sizeDelta = std::abs(targetDiag - sourceDiag) / std::max(1.f, sourceDiag);

    float sourceAspect = sw / sh;
    float targetAspect = targetW / targetH;
    float aspectDelta = std::abs(targetAspect - sourceAspect) / std::max(0.35f, sourceAspect);

    float radiusNorm = std::max(4.f, std::max(std::abs(sr), std::abs(targetCornerRadius)));
    float radiusDelta = std::abs(targetCornerRadius - sr) / radiusNorm;

    float distFactor = std::clamp(distance / 320.f, 0.f, 2.4f);
    float deformFactor = std::clamp(sizeDelta + aspectDelta + radiusDelta, 0.f, 1.8f);

    float scale = 1.f + distFactor * 0.50f + deformFactor * 0.35f;
    float adaptive = baseDuration * scale;

    return std::clamp(adaptive, baseDuration * 0.95f, baseDuration * 2.8f);
}

void SelectionCursor::moveTo(const nxui::Rect& target, float duration) {
    if (!m_initialized) {
        m_x.setImmediate(target.x);
        m_y.setImmediate(target.y);
        m_w.setImmediate(target.width);
        m_h.setImmediate(target.height);
        m_initialized = true;
        return;
    }
    constexpr float eps = 0.5f;
    if (std::abs(m_x.target() - target.x) < eps &&
        std::abs(m_y.target() - target.y) < eps &&
        std::abs(m_w.target() - target.width) < eps &&
        std::abs(m_h.target() - target.height) < eps)
        return;
    if (m_instantMotion || duration <= 0.f) {
        m_x.setImmediate(target.x);
        m_y.setImmediate(target.y);
        m_w.setImmediate(target.width);
        m_h.setImmediate(target.height);
        m_teleportPulse = 1.f;
        return;
    }
    float adaptiveDuration = computeAdaptiveDuration(target, m_cornerRadius.value(), duration);
    m_x.set(target.x, adaptiveDuration, nxui::Easing::outCubic);
    m_y.set(target.y, adaptiveDuration, nxui::Easing::outCubic);
    m_w.set(target.width, adaptiveDuration, nxui::Easing::outCubic);
    m_h.set(target.height, adaptiveDuration, nxui::Easing::outCubic);
}

void SelectionCursor::moveTo(const nxui::Rect& target, float cornerRadius, float duration) {
    if (!m_initialized) {
        m_x.setImmediate(target.x);
        m_y.setImmediate(target.y);
        m_w.setImmediate(target.width);
        m_h.setImmediate(target.height);
        m_cornerRadius.setImmediate(cornerRadius);
        m_initialized = true;
        return;
    }
    constexpr float eps = 0.5f;
    if (std::abs(m_x.target() - target.x) < eps &&
        std::abs(m_y.target() - target.y) < eps &&
        std::abs(m_w.target() - target.width) < eps &&
        std::abs(m_h.target() - target.height) < eps &&
        std::abs(m_cornerRadius.target() - cornerRadius) < eps)
        return;
    if (m_instantMotion || duration <= 0.f) {
        m_x.setImmediate(target.x);
        m_y.setImmediate(target.y);
        m_w.setImmediate(target.width);
        m_h.setImmediate(target.height);
        m_cornerRadius.setImmediate(cornerRadius);
        m_teleportPulse = 1.f;
        return;
    }
    float adaptiveDuration = computeAdaptiveDuration(target, cornerRadius, duration);
    m_x.set(target.x, adaptiveDuration, nxui::Easing::outCubic);
    m_y.set(target.y, adaptiveDuration, nxui::Easing::outCubic);
    m_w.set(target.width, adaptiveDuration, nxui::Easing::outCubic);
    m_h.set(target.height, adaptiveDuration, nxui::Easing::outCubic);
    m_cornerRadius.set(cornerRadius, adaptiveDuration, nxui::Easing::outCubic);
}

nxui::Rect SelectionCursor::currentRect() const {
    return nxui::Rect{
        std::roundf(m_x.value()),
        std::roundf(m_y.value()),
        std::roundf(m_w.value()),
        std::roundf(m_h.value())
    };
}

void SelectionCursor::onUpdate(float dt) {
    if (m_motionPaused)
        return;
    m_time += dt;
    m_teleportPulse = std::max(0.f, m_teleportPulse - dt / 0.18f);
}

void SelectionCursor::onRender(nxui::Renderer& ren) {
    if (!m_initialized || m_opacity <= 0.01f) return;

    float x = m_x.value(), y = m_y.value();
    float w = m_w.value(), h = m_h.value();
    if (w < 1.f || h < 1.f) return;

    const float arrival = m_teleportPulse * (2.f - m_teleportPulse);
    nxui::Rect r = nxui::Rect{x, y, w, h}.expanded(4.f * arrival);
    float cr = m_cornerRadius.value() + 4.f * arrival;

    float wave = std::sin(m_time * m_waveSpeed) * 0.5f + 0.5f;

    // Outline-only (no filled bloom) so folder/game art stays readable —
    // same idea as the green suspended ring on GlossyIcon.
    if (arrival > 0.01f) {
        ren.drawRoundedRectOutline(
            r.expanded(7.f * arrival),
            m_color.withAlpha(0.28f * arrival * m_opacity),
            cr + 7.f * arrival, 2.f);
    }
    const float outerAlpha = (0.35f + 0.20f * wave) * m_opacity;
    ren.drawRoundedRectOutline(r.expanded(3.f),
                               m_color.withAlpha(outerAlpha * 0.55f),
                               cr + 3.f, m_borderWidth + 1.2f);
    ren.drawRoundedRectOutline(r, m_color.withAlpha(m_opacity), cr, m_borderWidth);

    nxui::Rect inner = r.shrunk(m_borderWidth * 0.5f);
    nxui::Color innerC = nxui::Color(0.3f, 0.85f, 1.f,
                                     0.55f * m_opacity * (0.75f + 0.25f * wave));
    ren.drawRoundedRectOutline(inner, innerC, std::max(1.f, cr - 2.f), 1.5f);
}
