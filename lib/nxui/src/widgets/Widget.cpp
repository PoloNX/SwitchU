#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <algorithm>

namespace nxui {

// Tree

void Widget::addChild(Ptr child) {
    if (!child) return;
    child->m_parent = this;
    m_children.push_back(std::move(child));
}

void Widget::removeChild(Widget* child) {
    m_children.erase(
        std::remove_if(m_children.begin(), m_children.end(),
            [child](const auto& c) {
                if (c.get() == child) { c->m_parent = nullptr; return true; }
                return false;
            }),
        m_children.end());
}

void Widget::clearChildren() {
    for (auto& c : m_children) c->m_parent = nullptr;
    m_children.clear();
}

// Content rect

Rect Widget::contentRect() const {
    return {
        m_rect.x + m_padding.left,
        m_rect.y + m_padding.top,
        m_rect.width  - m_padding.horizontal(),
        m_rect.height - m_padding.vertical()
    };
}

// Custom navigation

void Widget::setCustomNavigation(FocusDirection dir, Widget* target) {
    m_customNav[static_cast<int>(dir)] = target;
}

Widget* Widget::getCustomNavigation(FocusDirection dir) const {
    auto it = m_customNav.find(static_cast<int>(dir));
    return (it != m_customNav.end()) ? it->second : nullptr;
}

// Actions

void Widget::addAction(uint64_t button, std::function<void()> cb) {
    m_actions[button] = std::move(cb);
}

void Widget::clearActions() {
    m_actions.clear();
}

uint64_t Widget::fireActions(const Input& input) const {
    uint64_t consumed = 0;
    // Snapshot — callbacks may call clearActions() and invalidate iterators.
    auto snapshot = m_actions;
    for (auto& [btn, cb] : snapshot) {
        if (input.isDown(static_cast<Button>(btn))) {
            if (cb) cb();
            consumed |= btn;
        }
    }
    return consumed;
}

bool Widget::fireAction(uint64_t button) const {
    auto it = m_actions.find(button);
    if (it != m_actions.end() && it->second) {
        // Copy the callback — invoking it may call clearActions().
        auto cb = it->second;
        cb();
        return true;
    }
    return false;
}

void Widget::addDirectionAction(FocusDirection dir, std::function<void()> cb) {
    switch (dir) {
        case FocusDirection::LEFT:
            addAction(static_cast<uint64_t>(Button::DLeft), cb);
            addAction(static_cast<uint64_t>(Button::LStickL), cb);
            addAction(static_cast<uint64_t>(Button::RStickL), cb);
            break;
        case FocusDirection::RIGHT:
            addAction(static_cast<uint64_t>(Button::DRight), cb);
            addAction(static_cast<uint64_t>(Button::LStickR), cb);
            addAction(static_cast<uint64_t>(Button::RStickR), cb);
            break;
        case FocusDirection::UP:
            addAction(static_cast<uint64_t>(Button::DUp), cb);
            addAction(static_cast<uint64_t>(Button::LStickU), cb);
            addAction(static_cast<uint64_t>(Button::RStickU), cb);
            break;
        case FocusDirection::DOWN:
            addAction(static_cast<uint64_t>(Button::DDown), cb);
            addAction(static_cast<uint64_t>(Button::LStickD), cb);
            addAction(static_cast<uint64_t>(Button::RStickD), cb);
            break;
    }
}

// Focus collection

void Widget::collectFocusable(std::vector<Widget*>& out) {
    if (!m_visible) return;
    if (isFocusable()) out.push_back(this);
    for (auto& c : m_children)
        c->collectFocusable(out);
}

// Lifecycle

void Widget::update(float dt) {
    if (!m_visible) return;
    onUpdate(dt);
    // Snapshot children — onUpdate() may modify m_children.
    auto kids = m_children;
    for (auto& c : kids) c->update(dt);
}

void Widget::render(Renderer& ren) {
    if (!m_visible || m_opacity <= 0.f) return;
    onRender(ren);
    // Snapshot children — onRender() may modify m_children.
    auto kids = m_children;
    for (auto& c : kids) c->render(ren);
}

} // namespace nxui
