#include "SelectionCursor.hpp"
#include <nxui/core/Renderer.hpp>
#include <cmath>


SelectionCursor::SelectionCursor() {
    m_x.setImmediate(0); m_y.setImmediate(0);
    m_w.setImmediate(0); m_h.setImmediate(0);
    m_cornerRadius.setImmediate(18.f);
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
    m_time += dt;
}

void SelectionCursor::onRender(nxui::Renderer& ren) {
    if (!m_initialized || m_opacity <= 0.01f) return;

    float x = m_x.value(), y = m_y.value();
    float w = m_w.value(), h = m_h.value();
    if (w < 1.f || h < 1.f) return;

    nxui::Rect r = {x, y, w, h};
    float cr = m_cornerRadius.value();

    float wave = std::sin(m_time * m_waveSpeed) * 0.5f + 0.5f;

    constexpr int BLOOM_LAYERS = 5;
    constexpr float bloomExpand[] = {12.f, 9.f, 6.f, 4.f, 2.f};
    constexpr float bloomAlpha[]  = {0.03f, 0.05f, 0.08f, 0.12f, 0.16f};
    for (int i = 0; i < BLOOM_LAYERS; ++i) {
        float expand = bloomExpand[i] * (1.f + 0.15f * wave);
        nxui::Rect glowRect = r.expanded(expand);
        float a = bloomAlpha[i] * m_opacity * (0.8f + 0.2f * wave);
        nxui::Color gc = m_color.withAlpha(a);
        ren.drawRoundedRect(glowRect, gc, cr + expand + 2.f);
    }

    nxui::Color mainC = m_color.withAlpha(m_opacity);
    ren.drawRoundedRectOutline(r, mainC, cr, m_borderWidth);

    nxui::Rect inner = r.shrunk(m_borderWidth * 0.5f);
    nxui::Color innerC = nxui::Color(0.3f, 0.85f, 1.f, 0.25f * m_opacity * (0.7f + 0.3f * wave));
    ren.drawRoundedRectOutline(inner, innerC, cr - 2.f, 1.5f);
}

