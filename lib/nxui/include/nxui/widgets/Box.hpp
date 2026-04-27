#pragma once
#include <nxui/widgets/Widget.hpp>

namespace nxui {

// Layout enums inspired by CSS flexbox.

enum class Axis {
    ROW,        // children flow left → right
    COLUMN,     // children flow top → bottom
};

enum class Direction {
    INHERIT,         // default (ROW = LTR, COLUMN = TTB)
    LEFT_TO_RIGHT,
    RIGHT_TO_LEFT,
};

enum class JustifyContent {
    FLEX_START,       // pack at start
    CENTER,           // center along main axis
    FLEX_END,         // pack at end
    SPACE_BETWEEN,    // equal space between items
    SPACE_AROUND,     // equal space around items
    SPACE_EVENLY,     // equal space around & between
};

enum class AlignItems {
    AUTO,             // no cross-axis alignment
    FLEX_START,       // cross-axis start
    CENTER,           // cross-axis center
    FLEX_END,         // cross-axis end
    STRETCH,          // stretch to fill cross-axis
};

/// A container widget that arranges its children using flexbox-like layout.
///
/// Example:
///   auto row = std::make_shared<Box>();
///   row->setAxis(Axis::ROW);
///   row->setJustifyContent(JustifyContent::SPACE_BETWEEN);
///   row->setAlignItems(AlignItems::CENTER);
///   row->setGap(16.f);
///   row->addChild(btn1);
///   row->addChild(btn2);
///
class Box : public Widget {
public:
    Box() = default;
    explicit Box(Axis axis) : m_axis(axis) {}
    Box(Axis axis, JustifyContent jc) : m_axis(axis), m_justify(jc) {}
    Box(Axis axis, JustifyContent jc, AlignItems ai) : m_axis(axis), m_justify(jc), m_align(ai) {}

    // Layout properties
    void setAxis(Axis a)              { m_axis = a; }
    Axis axis() const                 { return m_axis; }

    void setDirection(Direction d)    { m_direction = d; }
    Direction direction() const       { return m_direction; }

    void setJustifyContent(JustifyContent jc) { m_justify = jc; }
    JustifyContent justifyContent() const     { return m_justify; }

    void setAlignItems(AlignItems ai) { m_align = ai; }
    AlignItems alignItems() const     { return m_align; }

    void  setGap(float g) { m_gap = g; }
    float gap() const     { return m_gap; }

    void setWireframeEnabled(bool enabled) { m_wireframeEnabled = enabled; }
    bool wireframeEnabled() const { return m_wireframeEnabled; }

    bool isBox() const override { return true; }

    /// Perform the layout pass on all visible children.
    /// Must be called after adding/removing children or changing sizes.
    void layout();

    /// Same as layout(), but also sets this box's rect first.
    void layout(const Rect& bounds);

    /// Render children first, then optional debug wireframe overlay.
    void render(Renderer& ren) override;

protected:
    void onRender(Renderer& ren) override;

private:
    void layoutChildren();

    Axis            m_axis      = Axis::ROW;
    Direction       m_direction = Direction::INHERIT;
    JustifyContent  m_justify   = JustifyContent::FLEX_START;
    AlignItems      m_align     = AlignItems::AUTO;
    float           m_gap       = 0.f;
    bool            m_wireframeEnabled = true;
};

} // namespace nxui
