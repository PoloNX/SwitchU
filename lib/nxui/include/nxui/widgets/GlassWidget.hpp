#pragma once
#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/core/Types.hpp>

namespace nxui {

/**
 * GlassWidget: a GlassPanel that manages padding, content area,
 * and auto-sizing. Subclasses override onContentRender / onContentUpdate
 * instead of onRender / onUpdate — the glass background is drawn
 * automatically before the content.
 */
class GlassWidget : public GlassPanel {
public:
    GlassWidget();

    // Content geometry
    /// Inner rect after removing padding from the widget rect.
    Rect glassContentRect() const;

    /// Resize the widget so that its rect fits the content + padding.
    /// Position (x, y) is kept; only width / height are changed.
    void sizeToFit();

protected:
    // Template-method hooks for subclasses
    void onRender(Renderer& ren) override;
    void onUpdate(float dt) override;

    /// Override to draw your content (glass is already rendered).
    virtual void onContentRender(Renderer& /*ren*/) {}
    /// Override for per-frame logic.
    virtual void onContentUpdate(float /*dt*/) {}

    /// Return desired content size (used by sizeToFit).
    virtual Vec2 computeContentSize() const { return {0.f, 0.f}; }
};

} // namespace nxui
