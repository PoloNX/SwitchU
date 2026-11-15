#include "FolderBackdrop.hpp"

#include <nxui/core/Renderer.hpp>

namespace {
constexpr float kPushScale = 0.03f;
}

void FolderBackdrop::show(bool instant, const nxui::Rect& anchor, float dur) {
    setVisible(true);
    m_anchor = anchor.width > 0.f
        ? anchor.center()
        : nxui::Vec2{m_canvasW * 0.5f, m_canvasH * 0.5f};
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
        // Offscreen is captured in FB space — cover the physical screen under
        // portrait projection instead of stretching a 1280×720 logical quad.
        const float s = 1.f + kPushScale * t;
        renderer.drawOffscreenFramebufferCover(2, nxui::Color::white().withAlpha(alpha), s);
    }
    renderer.drawRect({0.f, 0.f, m_canvasW, m_canvasH},
                      nxui::Color(0.025f, 0.045f, 0.09f, 0.30f * alpha));
}
