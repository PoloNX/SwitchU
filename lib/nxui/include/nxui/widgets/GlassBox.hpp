#pragma once
#include <nxui/widgets/Box.hpp>

namespace nxui {

/// A Box with a frosted-glass background (rounded rect with glow + border).
/// Use this wherever you want a glass-styled container with flex layout.
class GlassBox : public Box {
public:
    GlassBox() = default;
    explicit GlassBox(Axis axis) : Box(axis) {}
    GlassBox(Axis axis, JustifyContent jc, AlignItems ai) : Box(axis, jc, ai) {}

    // Glass appearance
    void setCornerRadius(float r)       { m_radius = r; }
    float cornerRadius() const          { return m_radius; }

    void setBaseColor(const Color& c)   { m_base = c; }
    const Color& baseColor() const      { return m_base; }

    void setBorderColor(const Color& c) { m_border = c; }
    const Color& borderColor() const    { return m_border; }

    void setHighlightColor(const Color& c) { m_highlight = c; }
    const Color& highlightColor() const    { return m_highlight; }

    void setBorderWidth(float w)        { m_borderW = w; }
    float borderWidth() const           { return m_borderW; }

    void setScale(float s)              { m_scale = s; }
    float scale() const                 { return m_scale; }

    void setPanelOpacity(float o)       { m_panelOpacity = o; }
    float panelOpacity() const          { return m_panelOpacity; }

protected:
    void onRender(Renderer& ren) override;

    float m_radius       = 24.f;
    float m_borderW      = 1.f;
    float m_scale        = 1.f;
    float m_panelOpacity = 1.f;
    Color m_base      {0.14f, 0.14f, 0.22f, 0.40f};
    Color m_border    {0.50f, 0.50f, 0.65f, 0.22f};
    Color m_highlight {1.f, 1.f, 1.f, 0.05f};
};

} // namespace nxui
