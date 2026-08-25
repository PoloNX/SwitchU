#include "FolderBackdrop.hpp"

#include <nxui/core/Renderer.hpp>

void FolderBackdrop::show(bool instant) {
    setVisible(true);
    if (instant) {
        m_opacity.setImmediate(1.f);
        return;
    }
    m_opacity.setImmediate(0.f);
    m_opacity.set(1.f, 0.28f, nxui::Easing::outCubic);
}

void FolderBackdrop::hide() {
    m_opacity.set(0.f, 0.24f, nxui::Easing::outCubic);
}

void FolderBackdrop::onUpdate(float) {
    if (isVisible() && m_opacity.value() <= 0.005f && m_opacity.target() <= 0.f)
        setVisible(false);
}

void FolderBackdrop::onRender(nxui::Renderer& renderer) {
    const float alpha = m_opacity.value() * opacity();
    if (alpha <= 0.005f)
        return;
    if (renderer.gpu().offscreenReady()) {
        renderer.drawOffscreen(2, {0.f, 0.f, 1280.f, 720.f},
                               nxui::Color::white().withAlpha(alpha));
    }
    renderer.drawRect({0.f, 0.f, 1280.f, 720.f},
                      nxui::Color(0.025f, 0.045f, 0.09f, 0.30f * alpha));
}
