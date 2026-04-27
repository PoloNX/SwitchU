#include "SettingsScreen.hpp"
#include "tabs/TabBuilders.hpp"
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

void SettingsScreen::buildTabs() {
    DebugLog::log("[settings] buildTabs() start");
    m_tabs.clear();
    DebugLog::log("[settings]   SystemTab...");
    m_tabs.push_back(settings::tabs::SystemTab::build(*this));
    DebugLog::log("[settings]   StorageTab...");
    m_tabs.push_back(settings::tabs::StorageTab::build(*this));
    DebugLog::log("[settings]   AudioTab...");
    m_tabs.push_back(settings::tabs::AudioTab::build(*this));
    DebugLog::log("[settings]   DisplayTab...");
    m_tabs.push_back(settings::tabs::DisplayTab::build(*this));
    DebugLog::log("[settings]   InternetTab...");
    m_tabs.push_back(settings::tabs::InternetTab::build(*this));
    DebugLog::log("[settings]   ControllersTab...");
    m_tabs.push_back(settings::tabs::ControllersTab::build(*this));
    DebugLog::log("[settings]   BluetoothTab...");
    m_tabs.push_back(settings::tabs::BluetoothTab::build(*this));
    DebugLog::log("[settings]   SleepTab...");
    m_tabs.push_back(settings::tabs::SleepTab::build(*this));
    DebugLog::log("[settings]   MusicTab...");
    m_tabs.push_back(settings::tabs::MusicTab::build(*this));
    DebugLog::log("[settings]   ThemeTab...");
    m_tabs.push_back(settings::tabs::ThemeTab::build(*this));
    DebugLog::log("[settings]   AboutTab...");
    m_tabs.push_back(settings::tabs::AboutTab::build(*this));
    DebugLog::log("[settings] buildTabs() done (%d tabs)", (int)m_tabs.size());

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());

    if (m_tabBar) rebuildTabBar();
    if (m_tabContent) rebuildContentItems();
}

void SettingsScreen::refreshTranslations() {
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

void SettingsScreen::rebuildCurrentTab() {
    int oldTab = m_tabIndex;
    int oldFocus = m_contentIdx;
    float oldScroll = m_scrollTarget;

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

    rebuildTabBar();
    rebuildContentItems();
}

void SettingsScreen::requestDialog(const std::string& title, const std::string& msg,
                                   std::vector<DialogButtonDef> buttons) {
    if (m_dialogRequestCb) m_dialogRequestCb(title, msg, std::move(buttons));
}

void SettingsScreen::requestToast(const std::string& msg, float holdSeconds) {
    if (msg.empty()) return;
    m_toastText = msg;
    m_trackToastHold = std::max(0.f, holdSeconds);
    m_trackToastFading = false;
    m_trackToastAnim.setImmediate(1.f);
}

void SettingsScreen::warmup() {
    if (!m_tabs.empty()) return;
    buildTabs();
}

void SettingsScreen::updateThemeSliders(const ThemeColorSet& colors) {
    m_themeColors = colors;

    int themeTabIdx = (int)m_tabs.size() - 1;
    if (themeTabIdx < 0 || themeTabIdx >= (int)m_tabs.size()) return;
    auto& items = m_tabs[themeTabIdx].items;

    struct ColorMapping { int idx; float h, s, l; };
    ColorMapping mappings[] = {
        { 2, colors.accentH, colors.accentS, colors.accentL },
        { 3, colors.bgH,     colors.bgS,     colors.bgL     },
        { 4, colors.bgAccH,  colors.bgAccS,  colors.bgAccL  },
        { 5, colors.shapeH,  colors.shapeS,  colors.shapeL  },
    };

    for (auto& mapping : mappings) {
        if (mapping.idx < (int)items.size() && items[mapping.idx].type == ItemType::ColorPicker) {
            items[mapping.idx].colorH = mapping.h;
            items[mapping.idx].colorS = mapping.s;
            items[mapping.idx].colorL = mapping.l;
        }
    }
}

void SettingsScreen::updateThemePresetList(const std::vector<std::string>& names,
                                            const std::string& activeName) {
    m_themePresetNames = names;
    m_themePresetName = activeName;

    int themeTabIdx = (int)m_tabs.size() - 1;
    if (themeTabIdx < 0 || themeTabIdx >= (int)m_tabs.size()) return;
    auto& items = m_tabs[themeTabIdx].items;

    if (!items.empty() && items[0].type == ItemType::Selector) {
        items[0].options = names;
        for (int i = 0; i < (int)names.size(); ++i) {
            if (names[i] == activeName) {
                items[0].intVal = i;
                break;
            }
        }
    }
}

int SettingsScreen::focusableCount() const {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return 0;
    int count = 0;
    for (auto& item : m_tabs[m_tabIndex].items)
        if (item.focusable()) ++count;
    return count;
}

int SettingsScreen::rawIndexFromFocusable(int focIdx) const {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return 0;
    auto& items = m_tabs[m_tabIndex].items;
    int count = 0;
    for (int i = 0; i < (int)items.size(); ++i) {
        if (items[i].focusable()) {
            if (count == focIdx) return i;
            ++count;
        }
    }
    return 0;
}

void SettingsScreen::clampContentIdx() {
    int count = focusableCount();
    if (count <= 0) m_contentIdx = 0;
    else m_contentIdx = std::clamp(m_contentIdx, 0, count - 1);
}

float SettingsScreen::visibilityProgress() const {
    float t = std::clamp(m_animT, 0.f, 1.f);
    return m_showing ? easeOutCubic(t) : 1.f - easeInCubic(t);
}

void SettingsScreen::syncPanelState(float eased) {
    setOpacity(std::max(eased, 0.001f));
    setScale(0.92f + 0.08f * eased);
}

void SettingsScreen::invalidateBackdropCache() {
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;
}

nxui::Rect SettingsScreen::panelRect() const {
    return rect();
}

nxui::Rect SettingsScreen::panelRect(float scale) const {
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

nxui::Rect SettingsScreen::tabsRect() const {
    nxui::Rect panel = panelRect();
    return tabsRect(panel);
}

nxui::Rect SettingsScreen::tabsRect(const nxui::Rect& panel) const {
    return { panel.x + kInnerPad, panel.y + kInnerPad, kTabWidth, panel.height - 2 * kInnerPad };
}

nxui::Rect SettingsScreen::contentRect() const {
    nxui::Rect panel = panelRect();
    return contentRect(panel);
}

nxui::Rect SettingsScreen::contentRect(const nxui::Rect& panel) const {
    float left = panel.x + kInnerPad + kTabWidth + kInnerPad;
    return { left, panel.y + kInnerPad,
             panel.right() - kInnerPad - left, panel.height - 2 * kInnerPad };
}

float SettingsScreen::contentTotalHeight() const {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return 0;
    float height = 0;
    for (auto& item : m_tabs[m_tabIndex].items)
        height += (item.type == ItemType::Section ? kSectionHeight : kRowHeight);
    return height;
}