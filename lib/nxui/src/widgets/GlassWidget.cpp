#include <nxui/widgets/GlassWidget.hpp>

namespace nxui {

GlassWidget::GlassWidget() {
    setLiquidGlassEnabled(true);
    setBlurEnabled(false);
    setBlurRadius(1.5f);
    setBlurPasses(1);
    setCornerRadius(24.f);
    setBaseColor({0.28f, 0.32f, 0.38f, 0.20f});
    setBorderColor({0.96f, 0.97f, 1.0f, 0.16f});
    setHighlightColor({1.0f, 1.0f, 1.0f, 0.05f});
    setBorderWidth(1.0f);
}

Rect GlassWidget::glassContentRect() const {
    return {
        m_rect.x + m_padding.left,
        m_rect.y + m_padding.top,
        m_rect.width  - m_padding.left - m_padding.right,
        m_rect.height - m_padding.top  - m_padding.bottom
    };
}

void GlassWidget::sizeToFit() {
    Vec2 cs = computeContentSize();
    m_rect.width  = cs.x + m_padding.left + m_padding.right;
    m_rect.height = cs.y + m_padding.top  + m_padding.bottom;
}

void GlassWidget::onRender(Renderer& ren) {
    GlassPanel::onRender(ren);
    onContentRender(ren);
}

void GlassWidget::onUpdate(float dt) {
    onContentUpdate(dt);
}

} // namespace nxui
