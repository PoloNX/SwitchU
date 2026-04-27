#include <nxui/focus/FocusManager.hpp>
#include <nxui/core/Input.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace nxui {

namespace {

static bool isTouchInteractive(Widget* node) {
    return node && node->isVisible()
        && (node->isFocusable() || !node->actions().empty());
}

static Widget* findTopHitRecursive(Widget* node, float x, float y) {
    if (!node || !node->isVisible()) return nullptr;

    // Last child is drawn on top -> reverse traversal
    const auto& ch = node->children();
    for (int i = (int)ch.size() - 1; i >= 0; --i) {
        if (auto* hit = findTopHitRecursive(ch[i].get(), x, y))
            return hit;
    }

    // Ignore structural layers that cover the screen but don't actually
    // handle touch, so lower interactive siblings can still be reached.
    if (node->hitTest(x, y) && isTouchInteractive(node))
        return node;
    return nullptr;
}

static bool containsRecursive(Widget* node, Widget* target) {
    if (!node) return false;
    if (node == target) return true;
    for (const auto& c : node->children()) {
        if (containsRecursive(c.get(), target))
            return true;
    }
    return false;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Grid mode (legacy) — unchanged behaviour
// ══════════════════════════════════════════════════════════════════════════════

void FocusManager::setGrid(std::vector<Widget*> items, int cols) {
    // Try to preserve the relative position (same row/col) across page changes
    int oldCol = (m_cols > 0 && !m_items.empty()) ? (m_index % m_cols) : 0;
    int oldRow = (m_cols > 0 && !m_items.empty()) ? (m_index / m_cols) : 0;

    m_items = std::move(items);
    m_cols  = cols;

    if (m_items.empty()) {
        m_index = 0;
        return;
    }

    // Compute target index from old row/col, clamped to new grid
    int newRows = ((int)m_items.size() + m_cols - 1) / m_cols;
    int row = std::min(oldRow, newRows - 1);
    int col = std::min(oldCol, m_cols - 1);
    int idx = row * m_cols + col;
    idx = std::min(idx, (int)m_items.size() - 1);

    m_index = std::max(idx, 0);
    m_items[m_index]->onFocusGained();
}

int FocusManager::rows() const {
    if (m_cols == 0) return 0;
    return ((int)m_items.size() + m_cols - 1) / m_cols;
}

Widget* FocusManager::current() const {
    if (m_index < 0 || m_index >= (int)m_items.size()) return nullptr;
    return m_items[m_index];
}

void FocusManager::changeFocus(int newIdx) {
    if (newIdx < 0 || newIdx >= (int)m_items.size()) return;
    if (newIdx == m_index) return;
    auto* old = current();
    if (old) old->onFocusLost();
    m_index = newIdx;
    auto* cur = current();
    if (cur) cur->onFocusGained();
    if (m_cb) m_cb(old, cur);
}

void FocusManager::changeFocusTo(Widget* target) {
    if (!target) return;
    // Find target in m_items
    for (int i = 0; i < (int)m_items.size(); ++i) {
        if (m_items[i] == target) {
            changeFocus(i);
            return;
        }
    }
    // Target not in the flat list — force-set it
    auto* old = current();
    if (old) old->onFocusLost();
    m_items.push_back(target);
    m_index = (int)m_items.size() - 1;
    target->onFocusGained();
    if (m_cb) m_cb(old, target);
}

void FocusManager::setFocus(int index) { changeFocus(index); }

void FocusManager::setFocus(Widget* target) { changeFocusTo(target); }

void FocusManager::invalidateWidget(Widget* w) {
    if (!w) return;
    if (m_touchTarget == w) m_touchTarget = nullptr;

    // Remove from m_items
    auto it = std::find(m_items.begin(), m_items.end(), w);
    if (it != m_items.end()) {
        int idx = (int)std::distance(m_items.begin(), it);
        m_items.erase(it);
        if (m_index >= (int)m_items.size())
            m_index = std::max(0, (int)m_items.size() - 1);
        else if (idx < m_index)
            --m_index;
    }
}

void FocusManager::moveLeft() {
    int col = m_index % m_cols;
    if (col > 0) changeFocus(m_index - 1);
}

void FocusManager::moveRight() {
    int col = m_index % m_cols;
    if (col < m_cols - 1 && m_index + 1 < (int)m_items.size())
        changeFocus(m_index + 1);
}

void FocusManager::moveUp() {
    int newIdx = m_index - m_cols;
    if (newIdx >= 0) changeFocus(newIdx);
}

void FocusManager::moveDown() {
    int newIdx = m_index + m_cols;
    if (newIdx < (int)m_items.size()) changeFocus(newIdx);
}

// ══════════════════════════════════════════════════════════════════════════════
// Tree mode — spatial navigation
// ══════════════════════════════════════════════════════════════════════════════

/// Score a candidate widget relative to `from` for spatial navigation in `dir`.
/// Lower score = better match.  Returns a huge value if the candidate is not
/// valid in the requested direction at all.
static float spatialScore(const Rect& from, const Rect& to, FocusDirection dir) {
    // Center points
    float fx = from.x + from.width  * 0.5f;
    float fy = from.y + from.height * 0.5f;
    float tx = to.x + to.width  * 0.5f;
    float ty = to.y + to.height * 0.5f;

    float dx = tx - fx;
    float dy = ty - fy;

    // Check that the candidate is in the correct half-plane
    constexpr float kEpsilon = 0.5f;
    switch (dir) {
        case FocusDirection::UP:    if (dy > -kEpsilon) return 1e18f; break;
        case FocusDirection::DOWN:  if (dy <  kEpsilon) return 1e18f; break;
        case FocusDirection::LEFT:  if (dx > -kEpsilon) return 1e18f; break;
        case FocusDirection::RIGHT: if (dx <  kEpsilon) return 1e18f; break;
        default: return 1e18f;
    }

    // Weighted distance: favour candidates that are aligned on the cross-axis.
    // Main-axis distance is weighted 1x, cross-axis offset is weighted 3x.
    // This makes a vertically-aligned column navigate naturally up/down even
    // if items aren't pixel-perfect.
    float mainDist = 0, crossDist = 0;
    switch (dir) {
        case FocusDirection::UP:
        case FocusDirection::DOWN:
            mainDist  = std::abs(dy);
            crossDist = std::abs(dx);
            break;
        case FocusDirection::LEFT:
        case FocusDirection::RIGHT:
            mainDist  = std::abs(dx);
            crossDist = std::abs(dy);
            break;
        default: break;
    }

    return mainDist + crossDist * 3.0f;
}

Widget* FocusManager::findNearest(Widget* from, FocusDirection dir,
                                   const std::vector<Widget*>& candidates) {
    if (!from || candidates.empty()) return nullptr;

    Rect fr = from->focusRect();
    Widget* best = nullptr;
    float bestScore = 1e18f;

    for (auto* c : candidates) {
        if (c == from) continue;
        float s = spatialScore(fr, c->focusRect(), dir);
        if (s < bestScore) {
            bestScore = s;
            best = c;
        }
    }

    return best;
}

bool FocusManager::navigate(FocusDirection dir, Widget* root) {
    if (!root) return false;

    Widget* cur = current();

    // 1) Check custom navigation first
    if (cur) {
        Widget* custom = cur->getCustomNavigation(dir);
        if (custom && custom->isFocusable() && custom->isVisible()) {
            changeFocusTo(custom);
            return true;
        }
    }

    // 2) Collect all focusable widgets from the tree
    std::vector<Widget*> focusables;
    root->collectFocusable(focusables);
    if (focusables.empty()) return false;

    // 3) If no current focus, pick the first focusable
    if (!cur || !cur->isFocusable() || !cur->isVisible()) {
        m_items = focusables;
        m_index = 0;
        focusables[0]->onFocusGained();
        return true;
    }

    // 4) Spatial search
    Widget* next = findNearest(cur, dir, focusables);
    if (!next) return false;

    // Update the flat list so index-based queries work.
    // If focusables were reordered (e.g. runtime slot swap), first remap m_index
    // to the same current widget in the new list so changeFocus() compares against
    // a coherent index and still emits focus-change callbacks correctly.
    Widget* prevCur = cur;
    m_items = focusables;
    if (prevCur) {
        auto it = std::find(m_items.begin(), m_items.end(), prevCur);
        if (it != m_items.end()) {
            m_index = (int)std::distance(m_items.begin(), it);
        } else {
            m_index = std::clamp(m_index, 0, std::max(0, (int)m_items.size() - 1));
        }
    } else {
        m_index = std::clamp(m_index, 0, std::max(0, (int)m_items.size() - 1));
    }

    changeFocusTo(next);
    return true;
}

// ══════════════════════════════════════════════════════════════════════════════
// Action dispatch
// ══════════════════════════════════════════════════════════════════════════════

uint64_t FocusManager::dispatchActions(const Input& input, uint64_t excludeMask) const {
    uint64_t consumed = 0;
    Widget* w = current();
    while (w) {
        // Snapshot the action map before iterating — callbacks may call
        // clearActions() / hide() which clears the map, invalidating iterators.
        auto snapshot = w->actions();
        for (auto& [btn, cb] : snapshot) {
            if (excludeMask & btn) continue;
            if (input.isDown(static_cast<Button>(btn))) {
                if (cb) cb();
                consumed |= btn;
            }
        }
        w = w->parent();
    }
    return consumed;
}

// ══════════════════════════════════════════════════════════════════════════════
// Touch-based focus navigation
// ══════════════════════════════════════════════════════════════════════════════

bool FocusManager::handleTouch(const Input& input, Widget* root) {
    if (!root) {
        m_touchTarget = nullptr;
        return false;
    }

    if (input.touchDown()) {
        m_touchTarget = nullptr;
        m_touchWasFocused = false;

        float tx = input.touchX();
        float ty = input.touchY();

        // Hit-test the topmost interactive widget while skipping inert layers.
        m_touchTarget = findTopHitRecursive(root, tx, ty);
        m_touchWasFocused = (m_touchTarget && m_touchTarget == current());
    }

    if (input.touchUp() && m_touchTarget) {
        constexpr float kTapThreshold = 20.f;
        constexpr float kTapMaxDuration = 0.40f;
        float dx = input.touchDeltaX();
        float dy = input.touchDeltaY();
        float dist2 = dx * dx + dy * dy;
        float duration = input.touchDuration();

        bool consumed = false;

        if (dist2 < kTapThreshold * kTapThreshold && duration <= kTapMaxDuration) {
            // Verify the target still belongs to the active tree
            bool valid = containsRecursive(root, m_touchTarget);
            if (valid) {
                if (m_touchWasFocused) {
                    constexpr uint64_t kA = static_cast<uint64_t>(Button::A);
                    if (!m_touchTarget->fireAction(kA))
                        m_touchTarget->activate();
                } else {
                    if (m_touchTarget->isFocusable()) {
                        std::vector<Widget*> focusables;
                        root->collectFocusable(focusables);
                        m_items = focusables;
                        changeFocusTo(m_touchTarget);
                    } else {
                        constexpr uint64_t kA = static_cast<uint64_t>(Button::A);
                        if (!m_touchTarget->fireAction(kA))
                            m_touchTarget->activate();
                    }
                }
                consumed = true;
            }
        }

        m_touchTarget = nullptr;
        m_touchWasFocused = false;
        return consumed;
    }

    return false;
}

} // namespace nxui
