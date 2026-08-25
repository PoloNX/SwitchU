#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <algorithm>

namespace nxui {

Widget::~Widget() {
    FocusManager::forgetWidget(this);
}

// Tree

void Widget::addChild(Ptr child) {
    if (!child) return;
    if (m_childTraversalDepth > 0) {
        m_childMutations.push_back({ChildMutationType::Add, std::move(child), nullptr});
        return;
    }
    child->m_parent = this;
    m_children.push_back(std::move(child));
}

void Widget::removeChild(Widget* child) {
    if (m_childTraversalDepth > 0) {
        m_childMutations.push_back({ChildMutationType::Remove, {}, child});
        return;
    }
    m_children.erase(
        std::remove_if(m_children.begin(), m_children.end(),
            [child](const auto& c) {
                if (c.get() == child) { c->m_parent = nullptr; return true; }
                return false;
            }),
        m_children.end());
}

void Widget::clearChildren() {
    if (m_childTraversalDepth > 0) {
        m_childMutations.push_back({ChildMutationType::Clear, {}, nullptr});
        return;
    }
    for (auto& c : m_children) c->m_parent = nullptr;
    m_children.clear();
}

void Widget::beginChildTraversal() {
    ++m_childTraversalDepth;
}

void Widget::endChildTraversal() {
    if (m_childTraversalDepth == 0)
        return;
    --m_childTraversalDepth;
    if (m_childTraversalDepth == 0)
        applyChildMutations();
}

void Widget::applyChildMutations() {
    if (m_childMutations.empty())
        return;

    auto mutations = std::move(m_childMutations);
    m_childMutations.clear();
    for (auto& mutation : mutations) {
        switch (mutation.type) {
            case ChildMutationType::Add:
                addChild(std::move(mutation.child));
                break;
            case ChildMutationType::Remove:
                removeChild(mutation.target);
                break;
            case ChildMutationType::Clear:
                clearChildren();
                break;
        }
    }
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

void Widget::removeAction(uint64_t button) {
    m_actions.erase(button);
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

std::string Widget::accessibilitySummary() const {
    std::string out;
    if (!m_accessibilityLabel.empty()) {
        out = m_accessibilityLabel;
    } else if (!m_tag.empty()) {
        out = m_tag;
    }

    if (!m_accessibilityRole.empty()) {
        if (!out.empty()) out += ", ";
        out += m_accessibilityRole;
    }

    if (!m_accessibilityHint.empty()) {
        if (!out.empty()) out += ". ";
        out += m_accessibilityHint;
    }

    return out;
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
    beginChildTraversal();
    onUpdate(dt);
    for (auto& child : m_children)
        child->update(dt);
    endChildTraversal();
}

void Widget::render(Renderer& ren) {
    if (!m_visible || m_opacity <= 0.f) return;
    beginChildTraversal();
    onRender(ren);
    for (auto& child : m_children)
        child->render(ren);
    endChildTraversal();
}

} // namespace nxui
