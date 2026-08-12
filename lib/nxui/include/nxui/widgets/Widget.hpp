#pragma once
#include <nxui/core/Types.hpp>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

namespace nxui {

class Renderer;
class Input;

// Focus direction for spatial navigation.
enum class FocusDirection { UP, DOWN, LEFT, RIGHT };

class Widget : public std::enable_shared_from_this<Widget> {
public:
    using Ptr = std::shared_ptr<Widget>;

    Widget() = default;
    virtual ~Widget(); // unregisters from every FocusManager caching this pointer

    // Tree
    void addChild(Ptr child);
    void removeChild(Widget* child);
    void clearChildren();
    const std::vector<Ptr>& children() const { return m_children; }
    Widget* parent() const { return m_parent; }

    // Geometry
    void setPosition(float x, float y) { m_rect.x = x; m_rect.y = y; }
    void setSize(float w, float h)     { m_rect.width = w; m_rect.height = h; }
    void setRect(const Rect& r)        { m_rect = r; }
    const Rect& rect() const           { return m_rect; }

    // Visibility and opacity
    void  setVisible(bool v)   { m_visible = v; }
    bool  isVisible() const    { return m_visible; }
    void  setOpacity(float a)  { m_opacity = a; }
    float opacity() const      { return m_opacity; }

    // Margin
    void setMargin(float all)                                  { m_margin = EdgeInsets(all); }
    void setMargin(float top, float right, float bottom, float left) { m_margin = {top, right, bottom, left}; }
    void setMarginTop(float v)    { m_margin.top = v; }
    void setMarginRight(float v)  { m_margin.right = v; }
    void setMarginBottom(float v) { m_margin.bottom = v; }
    void setMarginLeft(float v)   { m_margin.left = v; }
    const EdgeInsets& margin() const { return m_margin; }

    // Padding
    void setPadding(float all)                                   { m_padding = EdgeInsets(all); }
    void setPadding(float top, float right, float bottom, float left) { m_padding = {top, right, bottom, left}; }
    void setPaddingTop(float v)    { m_padding.top = v; }
    void setPaddingRight(float v)  { m_padding.right = v; }
    void setPaddingBottom(float v) { m_padding.bottom = v; }
    void setPaddingLeft(float v)   { m_padding.left = v; }
    const EdgeInsets& padding() const { return m_padding; }

    Rect contentRect() const;

    // Flex (used by parent Box)
    void  setGrow(float g)   { m_grow = g; }
    float grow() const       { return m_grow; }
    void  setShrink(float s) { m_shrink = s; }
    float shrink() const     { return m_shrink; }

    // Size constraints
    void  setMinWidth(float v)  { m_minWidth = v; }
    void  setMinHeight(float v) { m_minHeight = v; }
    void  setMaxWidth(float v)  { m_maxWidth = v; }
    void  setMaxHeight(float v) { m_maxHeight = v; }
    float minWidth()  const { return m_minWidth; }
    float minHeight() const { return m_minHeight; }
    float maxWidth()  const { return m_maxWidth; }
    float maxHeight() const { return m_maxHeight; }

    // Focus
    void setFocusable(bool f)           { m_focusable = f; }
    virtual bool isFocusable() const    { return m_focusable; }
    virtual Rect focusRect()   const    { return m_rect; }
    virtual void onFocusGained() {}
    virtual void onFocusLost()   {}

    /// Custom navigation: override spatial focus for a specific direction.
    /// e.g. widget->setCustomNavigation(FocusDirection::RIGHT, otherWidget);
    void setCustomNavigation(FocusDirection dir, Widget* target);
    Widget* getCustomNavigation(FocusDirection dir) const;

    // Input bindings on the focused widget
    /// Register a callback for a specific button press while this widget has focus.
    /// e.g. btn->addAction(Button::A, [](){ ... });
    void addAction(uint64_t button, std::function<void()> cb);
    void clearActions();
    /// Fire matching actions for buttons pressed this frame.
    /// Returns a bitmask of consumed buttons (buttons that had actions).
    uint64_t fireActions(const Input& input) const;
    /// Fire a single action by button value (does NOT check input state).
    /// Returns true if the action existed and was triggered.
    bool fireAction(uint64_t button) const;

    /// Convenience: register a callback for a focus direction (D-pad + left stick).
    /// Registers actions for both the D-pad button and the corresponding stick direction.
    void addDirectionAction(FocusDirection dir, std::function<void()> cb);

    // Legacy activation callback (also triggered by Button::A action)
    void setOnActivate(std::function<void()> cb) { m_onActivate = std::move(cb); }
    virtual bool activate() { if (m_onActivate) { m_onActivate(); return true; } return false; }

    void setFrameworkTouchEnabled(bool enabled) { m_frameworkTouchEnabled = enabled; }
    bool frameworkTouchEnabled() const { return m_frameworkTouchEnabled; }

    void setTag(const std::string& t) { m_tag = t; }
    const std::string& tag() const    { return m_tag; }

    // Accessibility metadata used by screen-reader style narration.
    void setAccessibilityLabel(const std::string& label) { m_accessibilityLabel = label; }
    const std::string& accessibilityLabel() const { return m_accessibilityLabel; }
    void setAccessibilityHint(const std::string& hint) { m_accessibilityHint = hint; }
    const std::string& accessibilityHint() const { return m_accessibilityHint; }
    void setAccessibilityRole(const std::string& role) { m_accessibilityRole = role; }
    const std::string& accessibilityRole() const { return m_accessibilityRole; }
    std::string accessibilitySummary() const;

    // Lifecycle
    virtual void update(float dt);
    virtual void render(Renderer& ren);

    virtual void onLayout() {}

    virtual bool isBox() const { return false; }

    // Hit testing
    bool hitTest(float x, float y) const {
        return x >= m_rect.x && x < m_rect.x + m_rect.width
            && y >= m_rect.y && y < m_rect.y + m_rect.height;
    }

    // Collect focusable descendants.
    void collectFocusable(std::vector<Widget*>& out);

protected:
    virtual void onUpdate(float dt) {}
    virtual void onRender(Renderer& ren) {}

    void beginChildTraversal();
    void endChildTraversal();

    Rect  m_rect;
    float m_opacity = 1.f;
    bool  m_visible = true;

    EdgeInsets m_margin;
    EdgeInsets m_padding;

    float m_grow   = 0.f;
    float m_shrink = 1.f;

    float m_minWidth  = 0.f;
    float m_minHeight = 0.f;
    float m_maxWidth  = 1e9f;
    float m_maxHeight = 1e9f;

    bool m_focusable = false;
    bool m_frameworkTouchEnabled = true;
    std::string m_tag;
    std::string m_accessibilityLabel;
    std::string m_accessibilityHint;
    std::string m_accessibilityRole;
    std::function<void()> m_onActivate;
    Widget* m_parent = nullptr;
    std::vector<Ptr> m_children;

private:
    enum class ChildMutationType { Add, Remove, Clear };
    struct ChildMutation {
        ChildMutationType type;
        Ptr child;
        Widget* target = nullptr;
    };
    void applyChildMutations();

    unsigned m_childTraversalDepth = 0;
    std::vector<ChildMutation> m_childMutations;

protected:
    // Custom navigation overrides (direction → target widget)
    std::unordered_map<int, Widget*> m_customNav;

    // Action bindings (button → callback)
    std::unordered_map<uint64_t, std::function<void()>> m_actions;

public:
    /// Read-only access to the action map (used by FocusManager for bubbling).
    const std::unordered_map<uint64_t, std::function<void()>>& actions() const { return m_actions; }

};

} // namespace nxui
