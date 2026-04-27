#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>

namespace nxui {

GlassPanel::GlassPanel() {}

void GlassPanel::onRender(Renderer& ren) {
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

    // Opaque backing: fully blocks anything drawn underneath
    if (m_backingEnabled && op > 0.01f) {
        ren.drawRoundedRect(r, m_backingColor.withAlpha(op), m_radius);
    }

    float shortestSide = std::max(1.0f, std::min(r.width, r.height));
    float longestSide = std::max(r.width, r.height);
    float aspectRatio = longestSide / shortestSide;

    // Very wide panels use a softer frosted fallback instead of the squircle shader.
    bool useLiquidGlassShader = m_liquidGlassEnabled && aspectRatio <= 1.35f;
    bool useWideLiquidGlassFallback = m_liquidGlassEnabled && aspectRatio > 1.35f;
    bool useAnyLiquidGlass = useLiquidGlassShader || useWideLiquidGlassFallback;
    bool needsBackdrop = (m_blurEnabled || useAnyLiquidGlass) && op > 0.01f;
    bool canReuseBackdropCapture = useAnyLiquidGlass && !m_blurEnabled;

    if (needsBackdrop) {
        ren.captureToOffscreen(canReuseBackdropCapture);
        if (m_blurEnabled) {
            ren.applyBlur(m_blurRadius, m_blurPasses);
        }
    }

    if (useLiquidGlassShader && op > 0.01f) {
        if (ren.liquidGlassDebugRawBackdrop()) {
            ren.drawOffscreenRounded(0, r, m_radius, Color::white().withAlpha(op));
        } else {
            ren.drawLiquidGlass(0, r, m_radius, m_base, op, m_liquidGlassShade);
        }
    } else if (useWideLiquidGlassFallback && op > 0.01f) {
        ren.drawOffscreenRounded(0, r, m_radius, Color::white().withAlpha(op));

        Color body = m_base;
        body.a *= 0.70f;
        ren.drawRoundedRect(r, body.withAlpha(body.a * op), m_radius);
    } else {
        if (m_blurEnabled && op > 0.01f) {
            ren.drawOffscreenRounded(0, r, m_radius, Color::white().withAlpha(op));
        }

        ren.drawRoundedRect(r, m_base.withAlpha(m_base.a * op), m_radius);
    }

    if (useAnyLiquidGlass) {
        if (m_highlight.a > 0.01f && op > 0.01f) {
            ren.drawRoundedRectOutline(r.shrunk(1.f),
                                       m_highlight.withAlpha(m_highlight.a * 0.55f * op),
                                       std::max(0.0f, m_radius - 1.f), 1.f);
        }

        if (m_borderW > 0.f) {
            ren.drawRoundedRectOutline(r,
                                       m_border.withAlpha(m_border.a * 0.80f * op),
                                       m_radius, m_borderW);
        }
        return;
    }

    bool drawClassicOutline = !useAnyLiquidGlass;

    if (drawClassicOutline && m_highlight.a > 0.01f && op > 0.01f) {
        ren.drawRoundedRectOutline(r.shrunk(0.5f),
                                   m_highlight.withAlpha(m_highlight.a * 0.55f * op),
                                   m_radius - 0.5f, 1.f);
    }

    if (drawClassicOutline && m_borderW > 0.f)
        ren.drawRoundedRectOutline(r, m_border.withAlpha(m_border.a * op),
                                   m_radius, m_borderW);
}

} // namespace nxui
