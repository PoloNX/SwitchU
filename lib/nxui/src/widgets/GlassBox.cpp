#include <nxui/widgets/GlassBox.hpp>
#include <nxui/core/Renderer.hpp>

namespace nxui {

void GlassBox::onRender(Renderer& ren) {
    if (m_scale < 0.01f) return;

    Rect r = m_rect;
    if (m_scale < 1.f) {
        float w = r.width  * m_scale;
        float h = r.height * m_scale;
        r.x += (r.width  - w) * 0.5f;
        r.y += (r.height - h) * 0.5f;
        r.width  = w;
        r.height = h;
    }

    float op = m_opacity * m_panelOpacity;

    // Main translucent body.
    ren.drawRoundedRect(r, m_base.withAlpha(m_base.a * op), m_radius);

    // Subtle inner glow.
    Rect inner = r.shrunk(1.f);
    ren.drawRoundedRect(inner, m_base.withAlpha(m_base.a * 0.25f * op), m_radius - 1.f);

    // Thin border.
    if (m_borderW > 0.f)
        ren.drawRoundedRectOutline(r, m_border.withAlpha(m_border.a * op),
                                   m_radius, m_borderW);

    // Box::onRender does nothing, but children are rendered by Widget::render
}

} // namespace nxui
