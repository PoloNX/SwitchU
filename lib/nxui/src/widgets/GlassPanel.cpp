#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace nxui {

GlassPanel::GlassPanel() {}

namespace {

void drawPanelMaterialTexture(Renderer& ren, const Rect& r, float radius,
                              float opacity, float intensity) {
    float mat = std::clamp(opacity * intensity, 0.f, 1.f);
    if (mat <= 0.004f)
        return;

    float innerRadius = std::max(0.f, radius - 1.5f);
    Rect inner = r.shrunk(1.5f);
    Rect lower = {
        inner.x + 1.f,
        inner.y + inner.height * 0.48f,
        std::max(0.f, inner.width - 2.f),
        std::max(0.f, inner.height * 0.50f)
    };

    ren.drawRoundedRect(inner, Color::white().withAlpha(0.030f * mat), innerRadius);
    ren.drawRoundedRect(lower, Color::black().withAlpha(0.040f * mat),
                        std::max(0.f, innerRadius * 0.72f));
    ren.drawRoundedRectOutline(inner,
                               Color::white().withAlpha(0.090f * mat),
                               innerRadius,
                               1.f);
    ren.drawRoundedRectOutline(r.shrunk(3.f),
                               Color::black().withAlpha(0.050f * mat),
                               std::max(0.f, radius - 3.f),
                               1.f);

    Rect grain = r.shrunk(5.f);
    if (grain.width <= 4.f || grain.height <= 4.f)
        return;

    float step = std::clamp(grain.height * 0.28f, 9.f, 16.f);
    int lineCount = std::min(22, std::max(3, (int)((grain.width + grain.height) / step)));
    float diag = grain.height * 0.72f;
    for (int i = 0; i < lineCount; ++i) {
        float x0 = grain.x - grain.height * 0.45f + i * step;
        float x1 = x0 + diag;
        if (x1 < grain.x || x0 > grain.right())
            continue;
        Color c = (i % 2 == 0)
            ? Color::white().withAlpha(0.020f * mat)
            : Color::black().withAlpha(0.014f * mat);
        ren.drawLine({std::max(grain.x, x0), grain.bottom() - 2.f},
                     {std::min(grain.right(), x1), grain.y + 2.f},
                     c,
                     1.f);
    }

    int speckles = std::min(9, std::max(2, (int)(grain.width / 34.f)));
    for (int i = 0; i < speckles; ++i) {
        float t = (float)(i + 1) / (float)(speckles + 1);
        float x = grain.x + std::fmod((t * 37.0f + r.x * 0.013f), 1.0f) * grain.width;
        float y = grain.y + std::fmod((t * 23.0f + r.y * 0.017f), 1.0f) * grain.height;
        Color c = (i % 2 == 0)
            ? Color::white().withAlpha(0.026f * mat)
            : Color::black().withAlpha(0.020f * mat);
        ren.drawCircle({x, y}, 0.8f, c, 8);
    }
}

} // namespace

void GlassPanel::onRender(Renderer& ren) {
    if (m_scale < 0.01f) return;

    Rect r = m_rect;
    if (std::abs(m_scale - 1.f) > 0.001f) {
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

    const bool useShaderGlass = m_liquidGlassEnabled
        && m_liquidGlassShaderEnabled
        && ren.gpu().offscreenReady();
    const bool needsBackdrop = (m_blurEnabled || useShaderGlass) && op > 0.01f;
    if (needsBackdrop) {
        ren.captureToOffscreen(useShaderGlass && !m_blurEnabled);
        if (m_blurEnabled)
            ren.applyBlur(m_blurRadius, m_blurPasses);
    }

    if (useShaderGlass && op > 0.01f) {
        ren.drawLiquidGlass(0, r, m_radius, m_base, op, m_liquidGlassShade);
    } else if (m_liquidGlassEnabled && op > 0.01f) {
        Color body = m_base;
        body.a = std::max(body.a, 0.16f);
        ren.drawFrostedInset(r, body, m_border, m_highlight, m_radius, op);
    } else {
        if (m_blurEnabled && op > 0.01f) {
            ren.drawOffscreenRounded(0, r, m_radius, Color::white().withAlpha(op));
        }

        ren.drawRoundedRect(r, m_base.withAlpha(m_base.a * op), m_radius);
    }

    if (m_materialTextureEnabled) {
        drawPanelMaterialTexture(ren, r, m_radius, op, m_materialTextureIntensity);
    }

    if (m_liquidGlassEnabled) {
        if (m_highlight.a > 0.01f && op > 0.01f)
            ren.drawRoundedRectOutline(r.shrunk(1.f),
                                       m_highlight.withAlpha(m_highlight.a * 0.55f * op),
                                       std::max(0.f, m_radius - 1.f), 1.f);
        if (m_borderW > 0.f)
            ren.drawRoundedRectOutline(r, m_border.withAlpha(m_border.a * 0.80f * op),
                                       m_radius, m_borderW);
        return;
    }

    if (m_highlight.a > 0.01f && op > 0.01f) {
        ren.drawRoundedRectOutline(r.shrunk(0.5f),
                                   m_highlight.withAlpha(m_highlight.a * 0.55f * op),
                                   m_radius - 0.5f, 1.f);
    }

    if (m_borderW > 0.f)
        ren.drawRoundedRectOutline(r, m_border.withAlpha(m_border.a * op),
                                   m_radius, m_borderW);
}

} // namespace nxui
