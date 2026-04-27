#pragma once
#include <nxui/widgets/Widget.hpp>
#include <vector>
#include <functional>

namespace nxui {

class Input;

/// Manages focus across a widget tree using spatial navigation.
///
/// Two modes of operation:
///   1) **Tree mode** (recommended): call navigate() with the root widget each frame.
///      FocusManager collects all focusable descendants and picks the nearest one
///      in the requested direction. Custom navigation routes are respected first.
///
///   2) **Grid mode** (legacy): call setGrid() with a flat list + column count.
///      Navigation uses row/col arithmetic. Kept for backward compatibility.
///
/// Example (tree mode):
///   FocusManager fm;
///   fm.navigate(FocusDirection::RIGHT, rootBox);
///
/// Example (action dispatch):
///   fm.dispatchActions(input);   // fires current widget's actions
///
class FocusManager {
public:
    using FocusChangedCb = std::function<void(Widget*, Widget*)>;

    // Legacy grid mode
    void setGrid(std::vector<Widget*> items, int cols);
    void moveLeft();
    void moveRight();
    void moveUp();
    void moveDown();

    // Tree mode for spatial navigation
    /// Navigate in a direction from the currently focused widget.
    /// Scans all focusable descendants of `root` and picks the best candidate.
    /// Returns true if focus actually moved.
    bool navigate(FocusDirection dir, Widget* root);

    // Common state
    void setFocus(int index);
    void setFocus(Widget* target);
    int  focusIndex()   const { return m_index; }
    Widget* current()   const;

    /// Dispatch input actions (button→callback) on the currently focused widget.
    /// Actions bubble up the parent chain: the focused widget fires first, then
    /// each ancestor up to the root. Buttons in `excludeMask` are skipped.
    /// Returns a bitmask of consumed buttons.
    uint64_t dispatchActions(const Input& input, uint64_t excludeMask = 0) const;

    void onFocusChanged(FocusChangedCb cb) { m_cb = cb; }

    /// Automatic touch-to-focus: on tap, focuses the touched widget
    /// (first tap) or activates it (tap on already-focused widget).
    /// Call this each frame from the application's input dispatch.
    /// Returns true if the touch was consumed as a tap.
    bool handleTouch(const Input& input, Widget* root);

    int  columns() const { return m_cols; }
    int  rows()    const;
    int  count()   const { return (int)m_items.size(); }

    /// Notify the focus manager that a widget is being destroyed or removed.
    /// Clears any internal references to it (m_items, m_touchTarget, etc.).
    void invalidateWidget(Widget* w);

private:
    void changeFocus(int newIdx);
    void changeFocusTo(Widget* target);

    /// Spatial scoring: find the best candidate in `dir` from `from`.
    static Widget* findNearest(Widget* from, FocusDirection dir,
                               const std::vector<Widget*>& candidates);

    std::vector<Widget*> m_items;
    int m_cols  = 1;
    int m_index = 0;
    FocusChangedCb m_cb;

    // Touch state for handleTouch()
    Widget* m_touchTarget     = nullptr;
    bool    m_touchWasFocused = false;
};

} // namespace nxui
