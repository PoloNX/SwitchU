#include "FolderBackdrop.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>

namespace {
constexpr float kPushScale = 0.03f;
}

void FolderBackdrop::setDimStrength(float strength) {
    m_dimStrength = std::clamp(strength, 0.f, 1.f);
}

void FolderBackdrop::show(bool instant, const nxui::Rect& anchor, float dur) {
    setVisible(true);
    m_anchor = anchor.width > 0.f ? anchor.center() : nxui::Vec2{640.f, 360.f};
    if (instant) {
        m_opacity.setImmediate(1.f);
        return;
    }
    m_opacity.setImmediate(0.f);
    m_opacity.set(1.f, dur, nxui::Easing::outCubic);
}

void FolderBackdrop::hide(float dur) {
    m_opacity.set(0.f, dur, nxui::Easing::inOutCubic);
}

void FolderBackdrop::onUpdate(float) {
    if (isVisible() && m_opacity.value() <= 0.005f && m_opacity.target() <= 0.f)
        setVisible(false);
}

void FolderBackdrop::onRender(nxui::Renderer& renderer) {
    const float t = m_opacity.value();
    const float alpha = t * opacity();
    if (alpha <= 0.005f)
        return;
    if (renderer.gpu().offscreenReady()) {
        // Scaling up around a point on screen always keeps the screen covered.
        const float s = 1.f + kPushScale * t;
        const nxui::Rect dest{m_anchor.x - m_anchor.x * s, m_anchor.y - m_anchor.y * s,
                              1280.f * s, 720.f * s};
        renderer.drawOffscreen(2, dest, nxui::Color::white().withAlpha(alpha));
    }
    renderer.drawRect({0.f, 0.f, 1280.f, 720.f},
                      nxui::Color(0.025f, 0.045f, 0.09f,
                                  0.30f * m_dimStrength * alpha));
}
