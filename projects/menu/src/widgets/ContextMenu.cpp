#include "ContextMenu.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>

ContextMenu::ContextMenu() {
    setVisible(false);
    setFocusable(true);
    setFrameworkTouchEnabled(false);
    setTag("context_menu");
    setCornerRadius(20.f);
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setLiquidGlassShaderEnabled(false);
    setBlurEnabled(false);
    setPanelOpacity(0.96f);

    addDirectionAction(nxui::FocusDirection::UP, [this]() { moveSelection(-1); });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() { moveSelection(1); });
    addAction(static_cast<std::uint64_t>(nxui::Button::A),
              [this]() { activateSelected(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::B),
              [this]() { cancel(); });
}

int ContextMenu::firstEnabled(int preferred) const {
    if (m_items.empty()) return -1;
    preferred = std::clamp(preferred, 0, static_cast<int>(m_items.size()) - 1);
    if (m_items[static_cast<std::size_t>(preferred)].enabled) return preferred;
    for (int distance = 1; distance < static_cast<int>(m_items.size()); ++distance) {
        const int after = preferred + distance;
        if (after < static_cast<int>(m_items.size()) &&
            m_items[static_cast<std::size_t>(after)].enabled)
            return after;
        const int before = preferred - distance;
        if (before >= 0 && m_items[static_cast<std::size_t>(before)].enabled)
            return before;
    }
    return -1;
}

void ContextMenu::show(const nxui::Rect& anchor, std::string title,
                       std::vector<Item> items, int selected,
                       VoidCallback onCancel) {
    m_title = std::move(title);
    m_items = std::move(items);
    m_selected = firstEnabled(selected);
    m_scroll = 0;
    m_visibleItems = std::min(kMaximumVisibleItems,
                              static_cast<int>(m_items.size()));
    m_onCancel = std::move(onCancel);
    m_touchItem = -1;
    m_touchWasSelected = false;

    const float titleBand = m_title.empty() ? 0.f : kTitleHeight + kItemGap;
    const float panelHeight = kPanelPadding * 2.f + titleBand
        + m_visibleItems * kItemHeight
        + std::max(0, m_visibleItems - 1) * kItemGap;
    constexpr float kScreenPadding = 18.f;
    constexpr float kAnchorGap = 16.f;
    const float rightX = anchor.x + anchor.width + kAnchorGap;
    const float leftX = anchor.x - kAnchorGap - kPanelWidth;
    const bool fitsRight = rightX + kPanelWidth <= 1280.f - kScreenPadding;
    float x = fitsRight ? rightX : leftX;
    x = std::clamp(x, kScreenPadding, 1280.f - kScreenPadding - kPanelWidth);
    float y = anchor.y + anchor.height * 0.5f - panelHeight * 0.5f;
    y = std::clamp(y, kScreenPadding, 720.f - kScreenPadding - panelHeight);
    setRect({x, y, kPanelWidth, panelHeight});

    if (m_theme) {
        setBaseColor(m_theme->panelBase.withAlpha(
            m_theme->mode == nxui::ThemeMode::Dark ? 0.96f : 0.98f));
        setBorderColor(m_theme->panelBorder.withAlpha(0.52f));
        setHighlightColor(m_theme->panelHighlight.withAlpha(0.14f));
    }
    setAccessibilityLabel(m_title.empty() ? "Menu" : m_title);
    setAccessibilityRole("menu");
    setAccessibilityHint("Up and down to choose. A to confirm. B to close.");
    keepSelectedVisible();
    m_reveal.setImmediate(0.86f);
    m_reveal.set(1.f, 0.16f, nxui::Easing::outBack);
    m_active = true;
    setVisible(true);
}

void ContextMenu::hide(bool notify) {
    if (!m_active && !isVisible()) return;
    m_active = false;
    setVisible(false);
    m_items.clear();
    m_onCancel = {};
    if (notify && m_onClose) m_onClose();
}

void ContextMenu::moveSelection(int direction) {
    if (!m_active || m_items.empty() || m_selected < 0) return;
    const int count = static_cast<int>(m_items.size());
    int next = m_selected;
    for (int tries = 0; tries < count; ++tries) {
        next = (next + direction + count) % count;
        if (m_items[static_cast<std::size_t>(next)].enabled) {
            m_selected = next;
            keepSelectedVisible();
            if (m_onNavigate) m_onNavigate();
            return;
        }
    }
}

void ContextMenu::activateSelected() {
    if (!m_active || m_selected < 0 ||
        m_selected >= static_cast<int>(m_items.size())) return;
    Item item = m_items[static_cast<std::size_t>(m_selected)];
    if (!item.enabled) return;
    if (m_onActivate) m_onActivate();
    if (item.onPress) item.onPress();
}

void ContextMenu::cancel() {
    if (!m_active) return;
    auto callback = std::move(m_onCancel);
    hide();
    if (callback) callback();
}

void ContextMenu::keepSelectedVisible() {
    if (m_selected < 0) return;
    if (m_selected < m_scroll) m_scroll = m_selected;
    if (m_selected >= m_scroll + m_visibleItems)
        m_scroll = m_selected - m_visibleItems + 1;
    m_scroll = std::clamp(m_scroll, 0,
        std::max(0, static_cast<int>(m_items.size()) - m_visibleItems));
}

nxui::Rect ContextMenu::itemRect(int visibleIndex) const {
    const float titleBand = m_title.empty() ? 0.f : kTitleHeight + kItemGap;
    return {m_rect.x + kPanelPadding,
            m_rect.y + kPanelPadding + titleBand
                + visibleIndex * (kItemHeight + kItemGap),
            m_rect.width - kPanelPadding * 2.f,
            kItemHeight};
}

void ContextMenu::handleTouch(nxui::Input& input) {
    if (!m_active) return;
    if (input.touchDown()) {
        m_touchItem = -1;
        m_touchWasSelected = false;
        for (int visible = 0; visible < m_visibleItems; ++visible) {
            if (itemRect(visible).expanded(5.f).contains(input.touchX(), input.touchY())) {
                m_touchItem = m_scroll + visible;
                m_touchWasSelected = m_touchItem == m_selected;
                break;
            }
        }
    }
    if (!input.touchUp()) return;
    const float dx = input.touchDeltaX();
    const float dy = input.touchDeltaY();
    if (dx * dx + dy * dy <= 400.f) {
        if (m_touchItem >= 0 && m_touchItem < static_cast<int>(m_items.size()) &&
            m_items[static_cast<std::size_t>(m_touchItem)].enabled) {
            if (m_touchWasSelected) {
                activateSelected();
            } else {
                m_selected = m_touchItem;
                keepSelectedVisible();
                if (m_onNavigate) m_onNavigate();
            }
        } else if (!m_rect.contains(input.touchX(), input.touchY())) {
            cancel();
        }
    }
    m_touchItem = -1;
    m_touchWasSelected = false;
}

void ContextMenu::update(float dt) {
    if (!m_active) return;
    m_reveal.update(dt);
    nxui::GlassWidget::update(dt);
}

void ContextMenu::render(nxui::Renderer& renderer) {
    if (!m_active || !isVisible()) return;
    const float reveal = std::clamp(m_reveal.value(), 0.f, 1.f);
    const nxui::Rect saved = rect();
    const float width = saved.width * reveal;
    const float height = saved.height * reveal;
    setRect({saved.x + (saved.width - width) * 0.5f,
             saved.y + (saved.height - height) * 0.5f,
             width, height});
    nxui::GlassWidget::render(renderer);
    setRect(saved);

    const nxui::Color primary = m_theme ? m_theme->textPrimary : nxui::Color::white();
    const nxui::Color secondary = m_theme ? m_theme->textSecondary
        : nxui::Color(0.78f, 0.82f, 0.88f, 1.f);
    const nxui::Color accent = m_theme ? m_theme->cursorNormal
        : nxui::Color(0.35f, 0.75f, 1.f, 1.f);
    if (!m_title.empty() && m_font) {
        renderer.drawText(m_title,
            {saved.x + kPanelPadding + 6.f, saved.y + kPanelPadding + 2.f},
            m_font, primary.withAlpha(reveal), 0.72f);
    }

    nxui::Font* itemFont = m_smallFont ? m_smallFont : m_font;
    for (int visible = 0; visible < m_visibleItems; ++visible) {
        const int index = m_scroll + visible;
        if (index >= static_cast<int>(m_items.size())) break;
        const auto& item = m_items[static_cast<std::size_t>(index)];
        const nxui::Rect row = itemRect(visible);
        if (index == m_selected) {
            renderer.drawRoundedRect(row,
                accent.withAlpha(0.18f * reveal), 13.f);
            renderer.drawRoundedRectOutline(row,
                accent.withAlpha(0.92f * reveal), 13.f, 2.f);
        }
        if (itemFont) {
            const nxui::Vec2 measure = itemFont->measure(item.label);
            float scale = 0.88f;
            if (measure.x > 0.f)
                scale = std::min(scale, (row.width - 24.f) / measure.x);
            scale = std::max(0.55f, scale);
            const float textHeight = measure.y * scale;
            renderer.drawText(item.label,
                {row.x + 12.f, row.y + (row.height - textHeight) * 0.5f},
                itemFont,
                item.enabled ? primary.withAlpha(reveal)
                             : secondary.withAlpha(0.42f * reveal),
                scale);
        }
    }

    if (itemFont && m_scroll > 0)
        renderer.drawText("▲", {saved.x + saved.width - 31.f, saved.y + 8.f},
                          itemFont, secondary.withAlpha(reveal), 0.6f);
    if (itemFont && m_scroll + m_visibleItems < static_cast<int>(m_items.size()))
        renderer.drawText("▼", {saved.x + saved.width - 31.f,
                                  saved.y + saved.height - 25.f},
                          itemFont, secondary.withAlpha(reveal), 0.6f);
}
