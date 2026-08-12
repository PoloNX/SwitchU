#include "TabbedOverlayScreen.hpp"
#include "core/DebugLog.hpp"

#include <algorithm>

namespace {

float easeOutCubic(float t) {
    float f = 1.f - t;
    return 1.f - f * f * f;
}

float easeInCubic(float t) {
    return t * t * t;
}

} // namespace

void TabbedOverlayScreen::refreshTranslations() {
    DebugLog::log("[settings] refreshTranslations()");
    int oldTab = m_tabIndex;
    int oldContent = m_contentIdx;

    buildTabs();

    if (!m_tabs.empty()) {
        m_tabIndex = std::clamp(oldTab, 0, (int)m_tabs.size() - 1);
    } else {
        m_tabIndex = 0;
    }

    clampContentIdx();
    if (focusableCount() > 0)
        m_contentIdx = std::clamp(oldContent, 0, focusableCount() - 1);
    else
        m_contentIdx = 0;

    rebuildTabBar();
    rebuildContentItems();
}

void TabbedOverlayScreen::rebuildCurrentTab() {
    int oldTab = m_tabIndex;
    int oldFocus = m_contentIdx;
    float oldScroll = m_scrollTarget;

    const bool needsStructuralRebuild = !usesCustomContentLayout() || m_tabs.empty();
    if (needsStructuralRebuild)
        buildTabs();

    if (!m_tabs.empty()) {
        m_tabIndex = std::clamp(oldTab, 0, (int)m_tabs.size() - 1);
    } else {
        m_tabIndex = 0;
    }

    clampContentIdx();
    if (focusableCount() > 0)
        m_contentIdx = std::clamp(oldFocus, 0, focusableCount() - 1);
    else
        m_contentIdx = 0;
    m_scrollTarget = oldScroll;
    m_scrollY = oldScroll;

    if (needsStructuralRebuild || !usesCustomContentLayout())
        rebuildTabBar();
    rebuildContentItems();
}

void TabbedOverlayScreen::refreshCurrentTabWidgets() {
    if (usesCustomContentLayout()) {
        rebuildContentItems();
        return;
    }

    if (m_tabIndex >= 0 && m_tabIndex < (int)m_cachedTabContentWidgets.size())
        m_cachedTabContentWidgets[(size_t)m_tabIndex].clear();

    clampContentIdx();
    m_contentIdx = std::clamp(m_contentIdx, 0, std::max(0, focusableCount() - 1));
    rebuildContentItems();
}

void TabbedOverlayScreen::requestDialog(const std::string& title, const std::string& msg,
                                   std::vector<DialogButtonDef> buttons) {
    if (m_dialogRequestCb) m_dialogRequestCb(title, msg, std::move(buttons));
}

void TabbedOverlayScreen::requestToast(const std::string& msg, float holdSeconds) {
    if (msg.empty()) return;
    m_toastText = msg;
    m_trackToastHold = std::max(0.f, holdSeconds);
    m_trackToastFading = false;
    m_trackToastAnim.setImmediate(1.f);
}

void TabbedOverlayScreen::warmup() {
    int oldTab = m_tabIndex;
    int oldContent = m_contentIdx;
    float oldScrollTarget = m_scrollTarget;
    float oldScrollY = m_scrollY;

    buildTabs();

    if (m_tabs.empty()) {
        m_tabIndex = 0;
        m_contentIdx = 0;
        m_scrollTarget = 0.f;
        m_scrollY = 0.f;
        return;
    }

    m_tabIndex = std::clamp(oldTab, 0, (int)m_tabs.size() - 1);
    clampContentIdx();
    if (focusableCount() > 0)
        m_contentIdx = std::clamp(oldContent, 0, focusableCount() - 1);
    else
        m_contentIdx = 0;
    m_scrollTarget = oldScrollTarget;
    m_scrollY = oldScrollY;

    rebuildTabBar();

    // Deliberately instantiate only the selected tab. Building every tab used
    // to synchronously enumerate storage, profiles, network and Bluetooth on
    // the menu-opening frame.
    rebuildContentItems();
}

bool TabbedOverlayScreen::itemFocusable(const SettingItem& item) const {
    if (item.focusable())
        return true;
    const bool isSection = item.type == ItemType::Section;
    if (item.type != ItemType::Info && !isSection)
        return false;
    if (m_accessibilityVoiceEnabled)
        return true;
    return !isSection && tabIsTextOnly();
}

bool TabbedOverlayScreen::tabIsTextOnly() const {
    if (m_tabIndex < 0 || m_tabIndex >= static_cast<int>(m_tabs.size()))
        return false;
    for (const auto& item : m_tabs[m_tabIndex].items) {
        if (item.focusable())
            return false;
    }
    return true;
}

int TabbedOverlayScreen::focusableCount() const {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return 0;
    int count = 0;
    for (auto& item : m_tabs[m_tabIndex].items)
        if (itemFocusable(item)) ++count;
    return count;
}

int TabbedOverlayScreen::rawIndexFromFocusable(int focIdx) const {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return 0;
    auto& items = m_tabs[m_tabIndex].items;
    int count = 0;
    for (int i = 0; i < (int)items.size(); ++i) {
        if (itemFocusable(items[i])) {
            if (count == focIdx) return i;
            ++count;
        }
    }
    return 0;
}

void TabbedOverlayScreen::clampContentIdx() {
    int count = focusableCount();
    if (count <= 0) m_contentIdx = 0;
    else m_contentIdx = std::clamp(m_contentIdx, 0, count - 1);
}

float TabbedOverlayScreen::visibilityProgress() const {
    float t = std::clamp(m_animT, 0.f, 1.f);
    return m_showing ? easeOutCubic(t) : 1.f - easeInCubic(t);
}

void TabbedOverlayScreen::syncPanelState(float eased) {
    setOpacity(std::max(eased, 0.001f));
    setScale(0.92f + 0.08f * eased);
}

void TabbedOverlayScreen::invalidateBackdropCache() {
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;
}

nxui::Rect TabbedOverlayScreen::panelRect() const {
    return rect();
}

nxui::Rect TabbedOverlayScreen::panelRect(float scale) const {
    nxui::Rect panel = panelRect();
    if (scale < 1.f) {
        float width = panel.width * scale;
        float height = panel.height * scale;
        panel.x += (panel.width - width) * 0.5f;
        panel.y += (panel.height - height) * 0.5f;
        panel.width = width;
        panel.height = height;
    }
    return panel;
}

nxui::Rect TabbedOverlayScreen::tabsRect() const {
    nxui::Rect panel = panelRect();
    return tabsRect(panel);
}

nxui::Rect TabbedOverlayScreen::tabsRect(const nxui::Rect& panel) const {
    const float headerH = overlayHeaderHeight();
    return { panel.x + kInnerPad, panel.y + kInnerPad + headerH,
             overlayTabWidth(), panel.height - 2 * kInnerPad - headerH };
}

nxui::Rect TabbedOverlayScreen::contentRect() const {
    nxui::Rect panel = panelRect();
    return contentRect(panel);
}

nxui::Rect TabbedOverlayScreen::contentRect(const nxui::Rect& panel) const {
    const float headerH = overlayHeaderHeight();
    float left = panel.x + kInnerPad + overlayTabWidth() + kInnerPad;
    return { left, panel.y + kInnerPad + headerH,
             panel.right() - kInnerPad - left, panel.height - 2 * kInnerPad - headerH };
}

float TabbedOverlayScreen::contentTotalHeight() const {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return 0;
    const float width = contentRect().width;
    float height = 0;
    for (auto& item : m_tabs[m_tabIndex].items)
        height += itemHeight(item, width);
    return height;
}
