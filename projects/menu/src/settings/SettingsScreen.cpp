#include "SettingsScreen.hpp"
#include "SettingItemWidgets.hpp"
#include "tabs/TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>


static float easeOutCubic(float t) { float f = 1.f - t; return 1.f - f * f * f; }
static float easeInCubic(float t)  { return t * t * t; }


SettingsScreen::SettingsScreen() {
    m_focusCursor.setBorderWidth(2.6f);
    m_focusCursor.setCornerRadius(10.f);
    m_tabGlowY.setImmediate(0.f);
    m_contentGlowY.setImmediate(0.f);
    m_tabReveal.setImmediate(1.f);
    m_dropdownAnim.setImmediate(0.f);
    m_colorPickerAnim.setImmediate(0.f);
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_contentSlideAnim.setImmediate(1.f);
    m_tabAccentW.setImmediate(3.f);

    m_tabBar = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
    m_tabBar->setTag("tabBar");
    addChild(m_tabBar);

    m_tabContent = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
    m_tabContent->setTag("tabContent");
    addChild(m_tabContent);

    rebuildTabBar();
    rebuildContentItems();

    m_i18nListenerId = nxui::I18n::instance().addLanguageChangedListener([this]() {
        m_deferredRefresh = true;
    });
}
SettingsScreen::~SettingsScreen() {
    nxui::I18n::instance().removeLanguageChangedListener(m_i18nListenerId);
}

void SettingsScreen::show() {
    if (m_active) return;
    DebugLog::log("[settings] show()");
    m_active    = true;
    m_animating = true;
    m_showing   = true;
    m_animT     = 0.f;
    m_focusArea  = FocusArea::Tabs;
    m_tabIndex   = 0;
    m_contentIdx = 0;
    m_scrollY    = 0.f;
    m_scrollTarget = 0.f;
    m_tabGlowY.setImmediate(0.f);
    m_contentGlowY.setImmediate(0.f);
    m_tabReveal.setImmediate(0.f);
    m_tabReveal.set(1.f, 0.24f, nxui::Easing::outCubic);
    m_dropdownOpen = false;
    m_dropdownRawIdx = -1;
    m_dropdownHover = 0;
    m_dropdownAnim.setImmediate(0.f);
    m_colorPickerOpen = false;
    m_colorPickerRawIdx = -1;
    m_colorPickerSlider = 0;
    m_colorPickerAnim.setImmediate(0.f);
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_contentSlideAnim.setImmediate(1.f);
    m_tabAccentW.setImmediate(3.f);

    m_deferBuild = true;
    m_tabs.clear();
    if (m_tabBar) rebuildTabBar();
    if (m_tabContent) rebuildContentItems();

    setFocusable(true);
    setupActions();
}

void SettingsScreen::hide() {
    if (!m_active) return;
    DebugLog::log("[settings] hide()");
    if (m_closeSfxCb) m_closeSfxCb();
    m_dropdownOpen = false;
    m_dropdownRawIdx = -1;
    m_dropdownAnim.setImmediate(0.f);
    m_colorPickerOpen = false;
    m_colorPickerRawIdx = -1;
    m_colorPickerAnim.setImmediate(0.f);
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_animating = true;
    m_showing   = false;
    m_animT     = 0.f;

    setFocusable(false);
    clearActions();
}


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

void SettingsScreen::rebuildTabBar() {
    m_tabBar->clearChildren();
    nxui::Rect tr = tabsRect();
    m_tabBar->setRect(tr);

    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        auto tabBox = std::make_shared<nxui::Box>();
        tabBox->setTag(m_tabs[i].name);
        tabBox->setRect({tr.x, tr.y + i * kTabRowHeight, tr.width, kTabRowHeight});
        m_tabBar->addChild(tabBox);
    }
}

void SettingsScreen::rebuildContentItems() {
    m_tabContent->clearChildren();
    nxui::Rect cr = contentRect();
    m_tabContent->setRect(cr);

    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    auto& items = m_tabs[m_tabIndex].items;
    DebugLog::log("[settings] rebuildContent tab=%d items=%d", m_tabIndex, (int)items.size());

    float y = 0.f;
    for (int i = 0; i < (int)items.size(); ++i) {
        float h = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
        auto itemBox = makeItemWidget(items[i]);
        itemBox->setTag(items[i].label);
        itemBox->setRect({0, y, cr.width, h});
        m_tabContent->addChild(itemBox);
        y += h;
    }
}

std::shared_ptr<nxui::Box> SettingsScreen::makeItemWidget(SettingItem& item) {
    settings::widgets::SettingWidgetContext ctx;
    ctx.font = &m_font;
    ctx.smallFont = &m_smallFont;
    ctx.theme = &m_theme;
    return settings::widgets::createSettingItemWidget(item, ctx);
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

    for (auto& m : mappings) {
        if (m.idx < (int)items.size() && items[m.idx].type == ItemType::ColorPicker) {
            items[m.idx].colorH = m.h;
            items[m.idx].colorS = m.s;
            items[m.idx].colorL = m.l;
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
    int n = 0;
    for (auto& it : m_tabs[m_tabIndex].items)
        if (it.focusable()) ++n;
    return n;
}

int SettingsScreen::rawIndexFromFocusable(int focIdx) const {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return 0;
    auto& items = m_tabs[m_tabIndex].items;
    int cnt = 0;
    for (int i = 0; i < (int)items.size(); ++i) {
        if (items[i].focusable()) {
            if (cnt == focIdx) return i;
            ++cnt;
        }
    }
    return 0;
}

void SettingsScreen::clampContentIdx() {
    int fc = focusableCount();
    if (fc <= 0) m_contentIdx = 0;
    else m_contentIdx = std::clamp(m_contentIdx, 0, fc - 1);
}


void SettingsScreen::setupActions() {
    clearActions();
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() { onPressB(); });
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() { onPressA(); });
    addDirectionAction(nxui::FocusDirection::UP,    [this]() { onNavUp(); });
    addDirectionAction(nxui::FocusDirection::DOWN,  [this]() { onNavDown(); });
    addDirectionAction(nxui::FocusDirection::LEFT,  [this]() { onNavLeft(); });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() { onNavRight(); });
}

void SettingsScreen::onPressB() {
    if (!m_active || m_animating) return;
    DebugLog::log("[settings] B (focus=%d dd=%d cp=%d)", (int)m_focusArea, m_dropdownOpen ? 1 : 0, m_colorPickerOpen ? 1 : 0);

    if (m_colorPickerOpen) {
        m_colorPickerOpen = false;
        m_colorPickerRawIdx = -1;
        m_colorPickerAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
        return;
    }
    if (m_dropdownOpen) {
        m_dropdownOpen = false;
        m_dropdownRawIdx = -1;
        m_dropdownAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
        return;
    }
    if (m_focusArea == FocusArea::Content) {
        m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
        return;
    }
    hide();
}

void SettingsScreen::onPressA() {
    if (!m_active || m_animating) return;
    DebugLog::log("[settings] A (focus=%d tab=%d ci=%d dd=%d)",
                  (int)m_focusArea, m_tabIndex, m_contentIdx, m_dropdownOpen ? 1 : 0);

    if (m_focusArea == FocusArea::Tabs) {
        if (focusableCount() > 0) {
            m_focusArea = FocusArea::Content;
            m_contentIdx = 0;
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }

    auto& items = m_tabs[m_tabIndex].items;
    int rawIdx = rawIndexFromFocusable(m_contentIdx);
    auto& item = items[rawIdx];

    if (m_colorPickerOpen) {
        m_colorPickerOpen = false;
        m_colorPickerRawIdx = -1;
        m_colorPickerAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
        return;
    }

    if (m_dropdownOpen) {
        if (m_dropdownRawIdx >= 0 && m_dropdownRawIdx < (int)items.size()
            && items[m_dropdownRawIdx].type == ItemType::Selector) {
            auto& sel = items[m_dropdownRawIdx];
            int n = std::max(1, (int)sel.options.size());
            sel.intVal = std::clamp(m_dropdownHover, 0, n - 1);
            if (sel.onChange) sel.onChange(sel);
            if (m_activateSfxCb) m_activateSfxCb();
        }
        m_dropdownOpen = false;
        m_dropdownRawIdx = -1;
        m_dropdownAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
        return;
    }

    if (item.type == ItemType::Toggle) {
        item.boolVal = !item.boolVal;
        if (item.onChange) item.onChange(item);
        if (m_toggleSfxCb) m_toggleSfxCb(item.boolVal);
    } else if (item.type == ItemType::Selector) {
        m_dropdownOpen = true;
        m_dropdownRawIdx = rawIdx;
        m_dropdownHover = std::clamp(item.intVal, 0, std::max(0, (int)item.options.size() - 1));
        m_dropdownAnim.set(1.f, 0.14f, nxui::Easing::outCubic);
        if (m_activateSfxCb) m_activateSfxCb();
    } else if (item.type == ItemType::Action) {
        if (item.onChange) item.onChange(item);
        if (m_activateSfxCb) m_activateSfxCb();
    } else if (item.type == ItemType::ColorPicker) {
        m_colorPickerOpen = true;
        m_colorPickerRawIdx = rawIdx;
        m_colorPickerSlider = 0;
        m_colorPickerAnim.set(1.f, 0.18f, nxui::Easing::outCubic);
        if (m_activateSfxCb) m_activateSfxCb();
    }
}

void SettingsScreen::onNavUp() {
    if (!m_active || m_animating) return;
    DebugLog::log("[settings] Up (focus=%d tab=%d ci=%d)",
                  (int)m_focusArea, m_tabIndex, m_contentIdx);

    if (m_focusArea == FocusArea::Tabs) {
        if (m_tabIndex > 0) {
            m_tabSwitchDir = -1;
            --m_tabIndex;
            m_contentIdx = 0;
            m_scrollY = 0;
            m_scrollTarget = 0;
            m_tabReveal.setImmediate(0.f);
            m_tabReveal.set(1.f, 0.24f, nxui::Easing::outCubic);
            m_contentSlideAnim.setImmediate(0.f);
            m_contentSlideAnim.set(1.f, 0.28f, nxui::Easing::outCubic);
            m_tabAccentW.setImmediate(1.f);
            m_tabAccentW.set(3.f, 0.32f, nxui::Easing::outExpo);
            m_dropdownOpen = false;
            m_dropdownRawIdx = -1;
            m_dropdownAnim.setImmediate(0.f);
            m_colorPickerOpen = false;
            m_colorPickerRawIdx = -1;
            m_colorPickerAnim.setImmediate(0.f);
            rebuildContentItems();
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }

    if (m_colorPickerOpen) {
        if (m_colorPickerSlider > 0) {
            --m_colorPickerSlider;
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }

    if (m_dropdownOpen) {
        auto& items = m_tabs[m_tabIndex].items;
        if (m_dropdownRawIdx >= 0 && m_dropdownRawIdx < (int)items.size()
            && items[m_dropdownRawIdx].type == ItemType::Selector) {
            int n = std::max(1, (int)items[m_dropdownRawIdx].options.size());
            m_dropdownHover = (m_dropdownHover + n - 1) % n;
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }

    if (m_contentIdx > 0) {
        --m_contentIdx;
        if (m_navSfxCb) m_navSfxCb();
        scrollToFocused();
    }
}

void SettingsScreen::onNavDown() {
    if (!m_active || m_animating) return;
    DebugLog::log("[settings] Down (focus=%d tab=%d ci=%d)",
                  (int)m_focusArea, m_tabIndex, m_contentIdx);

    if (m_focusArea == FocusArea::Tabs) {
        if (m_tabIndex < (int)m_tabs.size() - 1) {
            m_tabSwitchDir = 1;
            ++m_tabIndex;
            m_contentIdx = 0;
            m_scrollY = 0;
            m_scrollTarget = 0;
            m_tabReveal.setImmediate(0.f);
            m_tabReveal.set(1.f, 0.24f, nxui::Easing::outCubic);
            m_contentSlideAnim.setImmediate(0.f);
            m_contentSlideAnim.set(1.f, 0.28f, nxui::Easing::outCubic);
            m_tabAccentW.setImmediate(1.f);
            m_tabAccentW.set(3.f, 0.32f, nxui::Easing::outExpo);
            m_dropdownOpen = false;
            m_dropdownRawIdx = -1;
            m_dropdownAnim.setImmediate(0.f);
            m_colorPickerOpen = false;
            m_colorPickerRawIdx = -1;
            m_colorPickerAnim.setImmediate(0.f);
            rebuildContentItems();
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }

    if (m_colorPickerOpen) {
        if (m_colorPickerSlider < 2) {
            ++m_colorPickerSlider;
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }

    if (m_dropdownOpen) {
        auto& items = m_tabs[m_tabIndex].items;
        if (m_dropdownRawIdx >= 0 && m_dropdownRawIdx < (int)items.size()
            && items[m_dropdownRawIdx].type == ItemType::Selector) {
            int n = std::max(1, (int)items[m_dropdownRawIdx].options.size());
            m_dropdownHover = (m_dropdownHover + 1) % n;
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }

    if (m_contentIdx < focusableCount() - 1) {
        ++m_contentIdx;
        if (m_navSfxCb) m_navSfxCb();
        scrollToFocused();
    }
}

void SettingsScreen::onNavLeft() {
    if (!m_active || m_animating) return;
    DebugLog::log("[settings] Left (focus=%d)", (int)m_focusArea);

    if (m_colorPickerOpen) {
        auto& items = m_tabs[m_tabIndex].items;
        if (m_colorPickerRawIdx >= 0 && m_colorPickerRawIdx < (int)items.size()) {
            auto& item = items[m_colorPickerRawIdx];
            float* vals[3] = { &item.colorH, &item.colorS, &item.colorL };
            float step = (m_colorPickerSlider == 0) ? (1.f / 36.f) : 0.05f;
            *vals[m_colorPickerSlider] = std::clamp(*vals[m_colorPickerSlider] - step, 0.f, 1.f);
            if (item.onChange) item.onChange(item);
            if (m_sliderSfxCb) m_sliderSfxCb(false);
        }
        return;
    }

    if (m_dropdownOpen) {
        m_dropdownOpen = false;
        m_dropdownRawIdx = -1;
        m_dropdownAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
        return;
    }

    if (m_focusArea == FocusArea::Tabs) return;

    auto& items = m_tabs[m_tabIndex].items;
    int rawIdx = rawIndexFromFocusable(m_contentIdx);
    auto& item = items[rawIdx];

    if (item.type == ItemType::Slider) {
        item.floatVal = std::clamp(std::round((item.floatVal - 0.05f) * 20.f) / 20.f, 0.f, 1.f);
        if (item.onChange) item.onChange(item);
        if (m_sliderSfxCb) m_sliderSfxCb(false);
    } else {
        m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
    }
}

void SettingsScreen::onNavRight() {
    if (!m_active || m_animating) return;
    DebugLog::log("[settings] Right (focus=%d)", (int)m_focusArea);

    if (m_colorPickerOpen) {
        auto& items = m_tabs[m_tabIndex].items;
        if (m_colorPickerRawIdx >= 0 && m_colorPickerRawIdx < (int)items.size()) {
            auto& item = items[m_colorPickerRawIdx];
            float* vals[3] = { &item.colorH, &item.colorS, &item.colorL };
            float step = (m_colorPickerSlider == 0) ? (1.f / 36.f) : 0.05f;
            *vals[m_colorPickerSlider] = std::clamp(*vals[m_colorPickerSlider] + step, 0.f, 1.f);
            if (item.onChange) item.onChange(item);
            if (m_sliderSfxCb) m_sliderSfxCb(true);
        }
        return;
    }

    if (m_dropdownOpen) {
        m_dropdownOpen = false;
        m_dropdownRawIdx = -1;
        m_dropdownAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
        return;
    }

    if (m_focusArea == FocusArea::Tabs) {
        if (focusableCount() > 0) {
            m_focusArea = FocusArea::Content;
            m_contentIdx = 0;
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }

    auto& items = m_tabs[m_tabIndex].items;
    int rawIdx = rawIndexFromFocusable(m_contentIdx);
    auto& item = items[rawIdx];

    if (item.type == ItemType::Slider) {
        item.floatVal = std::clamp(std::round((item.floatVal + 0.05f) * 20.f) / 20.f, 0.f, 1.f);
        if (item.onChange) item.onChange(item);
        if (m_sliderSfxCb) m_sliderSfxCb(true);
    } else if (item.type == ItemType::Selector) {
        m_dropdownOpen = true;
        m_dropdownRawIdx = rawIdx;
        m_dropdownHover = std::clamp(item.intVal, 0, std::max(0, (int)item.options.size() - 1));
        m_dropdownAnim.set(1.f, 0.14f, nxui::Easing::outCubic);
        if (m_activateSfxCb) m_activateSfxCb();
    } else if (item.type == ItemType::Action) {
        if (item.onChange) item.onChange(item);
        if (m_activateSfxCb) m_activateSfxCb();
    }
}

void SettingsScreen::scrollToFocused() {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    auto& items = m_tabs[m_tabIndex].items;
    nxui::Rect cr = contentRect();
    float itemY = 0;
    int foc = 0;
    for (auto& it : items) {
        float h = it.type == ItemType::Section ? kSectionHeight : kRowHeight;
        if (it.focusable()) {
            if (foc == m_contentIdx) break;
            ++foc;
        }
        itemY += h;
    }
    if (itemY - m_scrollTarget < 0)
        m_scrollTarget = itemY;
    if (itemY + kRowHeight - m_scrollTarget > cr.height)
        m_scrollTarget = itemY + kRowHeight - cr.height;
}

void SettingsScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_animating) return;

    nxui::Rect panel = panelRect();
    nxui::Rect tr = tabsRect(panel);
    nxui::Rect cr = contentRect(panel);

    if (input.touchDown()) {
        float tx = input.touchX();
        float ty = input.touchY();
        m_touchTarget = TouchTarget::None;
        m_touchHitIndex = -1;
        m_touchOnSelected = false;

        // Check dropdown first if open
        if (m_dropdownOpen && m_dropdownRawIdx >= 0 && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
            auto& items = m_tabs[m_tabIndex].items;
            if (m_dropdownRawIdx < (int)items.size()) {
                auto& item = items[m_dropdownRawIdx];
                int total = (int)item.options.size();
                int visible = std::min(total, 6);
                float optH = 36.f;
                float listH = visible * optH + 10.f;

                float y = cr.y - m_scrollY;
                for (int i = 0; i < m_dropdownRawIdx; ++i)
                    y += (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
                float rowH = kRowHeight;

                float ctrlX = cr.x + cr.width * 0.55f;
                float ctrlW = cr.width * 0.42f;
                float dy = y + rowH + 6.f;
                if (dy + listH > cr.bottom() - 4.f)
                    dy = y - listH - 6.f;

                nxui::Rect dropRect = { ctrlX, dy, ctrlW, listH };
                if (dropRect.contains(tx, ty)) {
                    m_touchTarget = TouchTarget::Dropdown;
                    int start = 0;
                    if (total > visible)
                        start = std::clamp(m_dropdownHover - visible / 2, 0, total - visible);
                    float localY = ty - dy - 5.f;
                    int idx = start + (int)(localY / optH);
                    idx = std::clamp(idx, 0, total - 1);
                    m_touchHitIndex = idx;
                    m_touchOnSelected = (idx == m_dropdownHover);
                    return;
                }
            }
        }

        // Check color picker if open
        if (m_colorPickerOpen) {
            m_touchTarget = TouchTarget::ColorPicker;
            return;
        }

        // Check tabs
        if (tr.contains(tx, ty)) {
            m_touchTarget = TouchTarget::Tab;
            int idx = (int)((ty - tr.y) / kTabRowHeight);
            idx = std::clamp(idx, 0, (int)m_tabs.size() - 1);
            m_touchHitIndex = idx;
            m_touchOnSelected = (idx == m_tabIndex && m_focusArea == FocusArea::Tabs);
            return;
        }

        // Check content items
        if (cr.contains(tx, ty) && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
            auto& items = m_tabs[m_tabIndex].items;
            float y = cr.y - m_scrollY;
            int focIdx = 0;
            for (int i = 0; i < (int)items.size(); ++i) {
                float h = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
                if (items[i].focusable()) {
                    nxui::Rect itemRect = { cr.x, y, cr.width, h };
                    if (itemRect.contains(tx, ty) && y >= cr.y && y + h <= cr.bottom()) {
                        m_touchTarget = TouchTarget::Content;
                        m_touchHitIndex = focIdx;
                        m_touchOnSelected = (focIdx == m_contentIdx && m_focusArea == FocusArea::Content);
                        return;
                    }
                    ++focIdx;
                }
                y += h;
            }
        }
    }

    if (input.touchUp()) {
        float dx = std::abs(input.touchDeltaX());
        float dy = std::abs(input.touchDeltaY());
        constexpr float kTapThreshold = 20.f;

        if (dx < kTapThreshold && dy < kTapThreshold) {
            switch (m_touchTarget) {
            case TouchTarget::Tab:
                if (m_touchHitIndex >= 0 && m_touchHitIndex < (int)m_tabs.size()) {
                    if (m_touchOnSelected) {
                        // Tap on selected tab: enter content
                        if (focusableCount() > 0) {
                            m_focusArea = FocusArea::Content;
                            m_contentIdx = 0;
                            if (m_navSfxCb) m_navSfxCb();
                        }
                    } else {
                        // Tap on different tab: switch to it
                        m_focusArea = FocusArea::Tabs;
                        if (m_tabIndex != m_touchHitIndex) {
                            m_tabSwitchDir = (m_touchHitIndex > m_tabIndex) ? 1 : -1;
                            m_tabIndex = m_touchHitIndex;
                            m_contentIdx = 0;
                            m_scrollY = 0;
                            m_scrollTarget = 0;
                            m_tabReveal.setImmediate(0.f);
                            m_tabReveal.set(1.f, 0.24f, nxui::Easing::outCubic);
                            m_contentSlideAnim.setImmediate(0.f);
                            m_contentSlideAnim.set(1.f, 0.28f, nxui::Easing::outCubic);
                            m_tabAccentW.setImmediate(1.f);
                            m_tabAccentW.set(3.f, 0.32f, nxui::Easing::outExpo);
                            m_dropdownOpen = false;
                            m_dropdownRawIdx = -1;
                            m_dropdownAnim.setImmediate(0.f);
                            m_colorPickerOpen = false;
                            m_colorPickerRawIdx = -1;
                            m_colorPickerAnim.setImmediate(0.f);
                            rebuildContentItems();
                        }
                        if (m_navSfxCb) m_navSfxCb();
                    }
                }
                break;

            case TouchTarget::Content:
                if (m_touchHitIndex >= 0 && m_touchHitIndex < focusableCount()) {
                    if (m_touchOnSelected) {
                        // Tap on selected item: activate
                        onPressA();
                    } else {
                        // Tap on different item: select it
                        m_focusArea = FocusArea::Content;
                        m_contentIdx = m_touchHitIndex;
                        scrollToFocused();
                        if (m_navSfxCb) m_navSfxCb();
                    }
                }
                break;

            case TouchTarget::Dropdown:
                if (m_touchHitIndex >= 0) {
                    if (m_touchOnSelected) {
                        // Tap on selected dropdown option: select it
                        if (m_dropdownRawIdx >= 0 && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
                            auto& items = m_tabs[m_tabIndex].items;
                            if (m_dropdownRawIdx < (int)items.size()) {
                                auto& sel = items[m_dropdownRawIdx];
                                int n = std::max(1, (int)sel.options.size());
                                sel.intVal = std::clamp(m_dropdownHover, 0, n - 1);
                                if (sel.onChange) sel.onChange(sel);
                                if (m_activateSfxCb) m_activateSfxCb();
                            }
                        }
                        m_dropdownOpen = false;
                        m_dropdownRawIdx = -1;
                        m_dropdownAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
                    } else {
                        // Tap on different dropdown option: hover it
                        m_dropdownHover = m_touchHitIndex;
                        if (m_navSfxCb) m_navSfxCb();
                    }
                }
                break;

            case TouchTarget::ColorPicker:
                // Close color picker on tap
                m_colorPickerOpen = false;
                m_colorPickerRawIdx = -1;
                m_colorPickerAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
                break;

            case TouchTarget::None:
                // Tap outside: close dropdowns/pickers or go back
                if (m_dropdownOpen) {
                    m_dropdownOpen = false;
                    m_dropdownRawIdx = -1;
                    m_dropdownAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
                } else if (m_colorPickerOpen) {
                    m_colorPickerOpen = false;
                    m_colorPickerRawIdx = -1;
                    m_colorPickerAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
                } else if (!panel.contains(input.touchX(), input.touchY())) {
                    // Tap outside panel: close settings
                    hide();
                }
                break;
            }
        }

        m_touchTarget = TouchTarget::None;
        m_touchHitIndex = -1;
        m_touchOnSelected = false;
    }
}


void SettingsScreen::update(float dt) {
    if (m_deferredRefresh) {
        m_deferredRefresh = false;
        refreshTranslations();
    }

    if (m_deferBuild) {
        m_deferBuild = false;
        buildTabs();
    }

    if (m_animating) {
        m_animT += dt / kAnimDuration;
        if (m_animT >= 1.f) {
            m_animT = 1.f;
            m_animating = false;
            if (!m_showing) {
                m_active = false;
                if (m_closedCb) m_closedCb();
            }
        }
    }
    m_scrollY += (m_scrollTarget - m_scrollY) * std::min(1.f, dt * 14.f);
    m_uiTime += dt;

    if (m_active && !m_animating && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
        auto& tab = m_tabs[m_tabIndex];
        if (tab.onUpdate) tab.onUpdate(tab, *this);
    }

    m_focusCursor.update(dt);
    m_tabReveal.update(std::min(dt, 0.03f));
    m_dropdownAnim.update(dt);
    m_colorPickerAnim.update(dt);
    m_trackToastAnim.update(dt);
    m_contentSlideAnim.update(std::min(dt, 0.03f));
    m_tabAccentW.update(std::min(dt, 0.03f));

    if (m_trackToastHold > 0.f) {
        m_trackToastHold -= dt;
        if (m_trackToastHold <= 0.f) {
            m_trackToastHold = 0.f;
            if (!m_trackToastFading) {
                m_trackToastAnim.set(0.f, 0.35f, nxui::Easing::outCubic);
                m_trackToastFading = true;
            }
        }
    }

    for (auto& tab : m_tabs) {
        for (auto& item : tab.items) {
            if (item.type == ItemType::Toggle) {
                float target = item.boolVal ? 1.f : 0.f;
                item.anim01 += (target - item.anim01) * std::min(1.f, dt * 14.f);
                if (std::abs(target - item.anim01) < 0.0015f)
                    item.anim01 = target;
            } else if (item.type == ItemType::Slider) {
                float target = std::clamp(item.floatVal, 0.f, 1.f);
                item.anim01 += (target - item.anim01) * std::min(1.f, dt * 18.f);
                if (std::abs(target - item.anim01) < 0.0015f)
                    item.anim01 = target;
            } else if (item.type == ItemType::Action) {
                float target = (m_trackToastAnim.value() > 0.05f) ? 1.f : 0.f;
                item.anim01 += (target - item.anim01) * std::min(1.f, dt * 16.f);
                if (std::abs(target - item.anim01) < 0.0015f)
                    item.anim01 = target;
            }
        }
    }

    nxui::Rect p = panelRect();
    nxui::Rect tr = tabsRect(p);
    nxui::Rect cr = contentRect(p);

    float tabTargetY = tr.y + m_tabIndex * kTabRowHeight;
    m_tabGlowY.set(tabTargetY, 0.18f, nxui::Easing::outCubic);

    if (m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
        auto& items = m_tabs[m_tabIndex].items;
        float y = cr.y - m_scrollY;
        int foc = 0;
        for (auto& it : items) {
            float h = it.type == ItemType::Section ? kSectionHeight : kRowHeight;
            if (it.focusable()) {
                if (foc == m_contentIdx) {
                    m_contentGlowY.set(y, 0.14f, nxui::Easing::outCubic);
                    break;
                }
                ++foc;
            }
            y += h;
        }
    }

    for (auto& c : children()) c->update(dt);
}


nxui::Rect SettingsScreen::panelRect() const {
    return { kPanelMargin, kPanelMargin,
             1280.f - 2 * kPanelMargin, 720.f - 2 * kPanelMargin };
}

nxui::Rect SettingsScreen::panelRect(float scale) const {
    nxui::Rect p = panelRect();
    if (scale < 1.f) {
        float w = p.width  * scale;
        float h = p.height * scale;
        p.x += (p.width  - w) * 0.5f;
        p.y += (p.height - h) * 0.5f;
        p.width = w;
        p.height = h;
    }
    return p;
}

nxui::Rect SettingsScreen::tabsRect() const {
    nxui::Rect p = panelRect();
    return tabsRect(p);
}

nxui::Rect SettingsScreen::tabsRect(const nxui::Rect& p) const {
    return { p.x + kInnerPad, p.y + kInnerPad, kTabWidth, p.height - 2 * kInnerPad };
}

nxui::Rect SettingsScreen::contentRect() const {
    nxui::Rect p = panelRect();
    return contentRect(p);
}

nxui::Rect SettingsScreen::contentRect(const nxui::Rect& p) const {
    float left = p.x + kInnerPad + kTabWidth + kInnerPad;
    return { left, p.y + kInnerPad,
             p.right() - kInnerPad - left, p.height - 2 * kInnerPad };
}

float SettingsScreen::contentTotalHeight() const {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return 0;
    float h = 0;
    for (auto& it : m_tabs[m_tabIndex].items)
        h += (it.type == ItemType::Section ? kSectionHeight : kRowHeight);
    return h;
}


void SettingsScreen::render(nxui::Renderer& ren) {
    if (!m_active && !m_animating) return;

    float t = m_animT;
    float eased = m_showing ? easeOutCubic(t) : 1.f - easeInCubic(t);

    float opacity = eased;
    float scale   = 0.92f + 0.08f * eased;
    nxui::Rect p  = panelRect(scale);

    if (m_theme)
        m_focusCursor.setColor(m_theme->cursorNormal);
    m_focusCursor.setOpacity(opacity);

    if (m_tabBar) m_tabBar->setRect(tabsRect(p));
    if (m_tabContent) m_tabContent->setRect(contentRect(p));

    drawBackground(ren, opacity * 0.55f);
    drawPanel(ren, p, opacity);

    float textOp = m_showing ? opacity : opacity * opacity;

    ren.pushClipRect(p);
    drawTabs(ren, p, textOp);
    drawContent(ren, p, textOp);
    drawDropdown(ren, p, textOp);
    drawColorPicker(ren, p, textOp);
    drawTrackChangedToast(ren, p, textOp);
    ren.popClipRect();

    if (ren.boxWireframeEnabled()) {
        syncDebugWireframeRects(p);
        if (m_tabBar) m_tabBar->render(ren);
        if (m_tabContent) m_tabContent->render(ren);
    }

    m_focusCursor.render(ren);
}

void SettingsScreen::syncDebugWireframeRects(const nxui::Rect& panel) {
    if (!m_tabBar || !m_tabContent) return;

    nxui::Rect tr = tabsRect(panel);
    nxui::Rect cr = contentRect(panel);

    m_tabBar->setRect(tr);
    auto& tabChildren = m_tabBar->children();
    for (int i = 0; i < (int)tabChildren.size(); ++i) {
        tabChildren[i]->setRect({tr.x, tr.y + i * kTabRowHeight, tr.width, kTabRowHeight});
    }

    m_tabContent->setRect(cr);
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    auto& items = m_tabs[m_tabIndex].items;
    auto& itemChildren = m_tabContent->children();

    float slideT = std::clamp(m_contentSlideAnim.value(), 0.f, 1.f);
    float slideOffset = (1.f - slideT) * 24.f * (float)m_tabSwitchDir;

    float y = cr.y - m_scrollY + slideOffset;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
        itemChildren[i]->setRect({cr.x, y, cr.width, h});
        y += h;
    }
}

void SettingsScreen::drawBackground(nxui::Renderer& ren, float opacity) {
    ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, opacity));
}

void SettingsScreen::drawPanel(nxui::Renderer& ren, const nxui::Rect& p, float opacity) {
    if (!m_theme) return;

    setVisible(true);
    setRect(p);
    setScale(1.f);
    setCornerRadius(kPanelRadius);
    setBorderWidth(1.f);
    setBaseColor(m_theme->panelBase);
    setBorderColor(m_theme->panelBorder);
    setHighlightColor(m_theme->panelHighlight);
    setBackingEnabled(true);
    setBackingColor(nxui::Color(
        m_theme->panelBase.r, m_theme->panelBase.g, m_theme->panelBase.b, 1.f));
    setPanelOpacity(opacity);
    setOpacity(1.f);
    nxui::GlassPanel::onRender(ren);

    float sepX = p.x + kInnerPad + kTabWidth + kInnerPad * 0.5f;
    float sepTop = p.y + kInnerPad + 4;
    float sepBot = p.bottom() - kInnerPad - 4;
    ren.drawLine({sepX, sepTop}, {sepX, sepBot},
                 m_theme->panelBorder.withAlpha(0.3f * opacity), 1.f);
}

void SettingsScreen::drawTabs(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_theme) return;
    nxui::Rect tr = tabsRect(panel);

    {
        float pulse = 0.9f + 0.1f * (std::sin(m_uiTime * 4.2f) * 0.5f + 0.5f);
        nxui::Rect selRow = { tr.x, m_tabGlowY.value(), tr.width, kTabRowHeight };
        nxui::Color sel = m_theme->cursorNormal.withAlpha(0.16f * pulse * opacity);
        ren.drawRoundedRect(selRow.shrunk(2.f), sel, 10.f);
    }

    if (m_focusArea == FocusArea::Tabs) {
        nxui::Rect cur = { tr.x, m_tabGlowY.value(), tr.width, kTabRowHeight };
        m_focusCursor.moveTo(cur.shrunk(2.f), 10.f, 0.08f);
    }

    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        float y = tr.y + i * kTabRowHeight;
        nxui::Rect row = { tr.x, y, tr.width, kTabRowHeight };

        bool selected = (i == m_tabIndex);

        nxui::Color textCol = selected ? m_theme->textPrimary : m_theme->textSecondary;
        nxui::Vec2 sz = m_font->measure(m_tabs[i].name);
        float tx = row.x + 14.f;
        float breathe = selected ? (0.5f + 0.5f * std::sin(m_uiTime * 4.6f)) : 0.f;
        float scale = selected ? (0.88f + 0.018f * breathe) : 0.84f;
        tx += selected ? (1.2f * breathe) : 0.f;
        float ty = row.y + (row.height - sz.y * scale) * 0.5f;
        ren.drawText(m_tabs[i].name, {tx, ty}, m_font, textCol.withAlpha(opacity), scale);

        if (selected) {
            float accentW = m_tabAccentW.value();
            float accentH = kTabRowHeight * 0.48f;
            float accentY = row.y + (row.height - accentH) * 0.5f;
            nxui::Rect accent = { row.x + 2.f, accentY, accentW, accentH };
            ren.drawRoundedRect(accent,
                                m_theme->cursorNormal.withAlpha(0.85f * opacity),
                                1.5f);

            nxui::Rect underline = { row.x + 10.f, row.bottom() - 6.f, row.width - 20.f, 2.f };
            ren.drawRoundedRect(underline,
                                m_theme->cursorNormal.withAlpha((0.22f + 0.10f * breathe) * opacity),
                                1.f);
        }
    }
}

void SettingsScreen::drawContent(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_smallFont || !m_theme) return;
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    if (!m_tabContent) return;

    nxui::Rect cr = contentRect(panel);

    if (m_focusArea == FocusArea::Content && focusableCount() > 0) {
        float pulse = 0.86f + 0.14f * (std::sin(m_uiTime * 4.8f) * 0.5f + 0.5f);
        nxui::Rect glowRow = { cr.x, m_contentGlowY.value(), cr.width, kRowHeight };
        nxui::Color glow = m_theme->cursorNormal.withAlpha(0.12f * pulse * opacity);
        ren.drawRoundedRect(glowRow.shrunk(1.f), glow, 8.f);
    }

    if (m_focusArea == FocusArea::Content && focusableCount() > 0) {
        nxui::Rect cur = { cr.x, m_contentGlowY.value(), cr.width, kRowHeight };
        m_focusCursor.moveTo(cur.shrunk(1.f), 8.f, 0.08f);
    }

    auto& items = m_tabs[m_tabIndex].items;
    auto& itemChildren = m_tabContent->children();

    float slideT = std::clamp(m_contentSlideAnim.value(), 0.f, 1.f);
    float slideOffset = (1.f - slideT) * 24.f * (float)m_tabSwitchDir;
    float slideOpacity = opacity * slideT;
    float reveal = std::clamp(m_tabReveal.value(), 0.f, 1.f);

    float y = cr.y - m_scrollY + slideOffset;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
        float delay = std::min(0.60f, i * 0.055f);
        float local = std::clamp((reveal - delay) / 0.28f, 0.f, 1.f);
        float rowOpacity = slideOpacity * local;
        float rowYOffset = (1.f - local) * 10.f;
        itemChildren[i]->setRect({cr.x, y + rowYOffset, cr.width, h});
        itemChildren[i]->setOpacity(rowOpacity);
        y += h;
    }

    ren.pushClipRect(cr);
    m_tabContent->render(ren);
    ren.popClipRect();
}

void SettingsScreen::drawDropdown(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || !m_smallFont || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    float open = m_dropdownAnim.value();
    if (open <= 0.01f) return;

    auto& items = m_tabs[m_tabIndex].items;
    if (m_dropdownRawIdx < 0 || m_dropdownRawIdx >= (int)items.size()) return;
    auto& item = items[m_dropdownRawIdx];
    if (item.type != ItemType::Selector || item.options.empty()) return;

    nxui::Rect cr = contentRect(panel);

    float y = cr.y - m_scrollY;
    for (int i = 0; i < m_dropdownRawIdx; ++i)
        y += (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
    float rowH = (item.type == ItemType::Section) ? kSectionHeight : kRowHeight;

    float ctrlX = cr.x + cr.width * 0.40f;
    float ctrlW = cr.width * 0.60f;

    int total = (int)item.options.size();
    int visible = std::min(total, 6);
    float optH = 36.f;
    float listH = visible * optH + 10.f;

    int start = 0;
    if (total > visible)
        start = std::clamp(m_dropdownHover - visible / 2, 0, total - visible);

    float dy = y + rowH + 6.f;
    if (dy + listH > cr.bottom() - 4.f)
        dy = y - listH - 6.f;

    float scale = 0.94f + 0.06f * open;
    float w = ctrlW * scale;
    float h = listH * scale;
    float dx = ctrlX + (ctrlW - w) * 0.5f;
    float fy = dy + (listH - h) * 0.5f;

    nxui::Rect pop = { dx, fy, w, h };

    nxui::Color bg = (m_theme->mode == nxui::ThemeMode::Dark)
        ? nxui::Color(0.10f, 0.10f, 0.14f, 0.97f * opacity * open)
        : nxui::Color(0.97f, 0.97f, 0.99f, 0.98f * opacity * open);

    ren.drawRoundedRect(pop, bg, 10.f);
    ren.drawRoundedRectOutline(pop,
                               m_theme->panelBorder.withAlpha(0.8f * opacity * open),
                               10.f, 1.2f);

    nxui::Rect listClip = pop.shrunk(4.f);
    ren.pushClipRect(listClip);

    for (int i = 0; i < visible; ++i) {
        int idx = start + i;
        float ry = listClip.y + 2.f + i * optH;
        nxui::Rect rr = { listClip.x + 2.f, ry, listClip.width - 4.f, optH };

        bool hovered = idx == m_dropdownHover;
        if (hovered) {
            float pulse = 0.9f + 0.1f * (std::sin(m_uiTime * 5.2f) * 0.5f + 0.5f);
            nxui::Color hi = m_theme->cursorNormal.withAlpha(0.22f * pulse * opacity * open);
            ren.drawRoundedRect(rr, hi, 7.f);
            m_focusCursor.moveTo(rr.shrunk(0.5f), 7.f, 0.08f);
        }

        nxui::Color tc = (idx == item.intVal) ? m_theme->textPrimary : m_theme->textSecondary;
        std::string displayText = item.options[idx];
        float maxWidth = std::max(0.f, rr.width - 16.f);
        if (!displayText.empty()) {
            nxui::Vec2 tsz = m_smallFont->measure(displayText);
            if (tsz.x > maxWidth) {
                std::string ellipsis = "...";
                while (!displayText.empty()) {
                    displayText.pop_back();
                    tsz = m_smallFont->measure(displayText + ellipsis);
                    if (tsz.x <= maxWidth || displayText.empty())
                        break;
                }
                displayText += ellipsis;
            }
        }
        nxui::Vec2 tsz = m_smallFont->measure(displayText);
        float tx = rr.x + 10.f;
        float ty = rr.y + (rr.height - tsz.y * 0.7f) * 0.5f;
        ren.drawText(displayText, {tx, ty}, m_smallFont, tc.withAlpha(opacity * open), 0.76f);
    }

    ren.popClipRect();
}

void SettingsScreen::drawTrackChangedToast(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_smallFont || !m_theme) return;
    float t = m_trackToastAnim.value();
    if (t <= 0.01f || m_toastText.empty()) return;

    std::string displayText = m_toastText;
    float maxTextWidth = 420.f - 40.f;
    nxui::Vec2 tsz = m_smallFont->measure(displayText);
    if (tsz.x * 0.78f > maxTextWidth) {
        while (!displayText.empty() && m_smallFont->measure(displayText + "...").x * 0.78f > maxTextWidth)
            displayText.pop_back();
        displayText += "...";
        tsz = m_smallFont->measure(displayText);
    }

    float textWidth = std::min(maxTextWidth, tsz.x * 0.78f);
    float scale = 0.96f + 0.04f * t;
    float w = std::clamp(textWidth + 40.f, 220.f, 420.f) * scale;
    float h = 40.f * scale;
    float x = panel.right() - 24.f - w;
    float y = panel.y + 18.f;
    nxui::Rect r = {x, y, w, h};

    nxui::Color bg = (m_theme->mode == nxui::ThemeMode::Dark)
        ? nxui::Color(0.10f, 0.14f, 0.20f, 0.92f * t * opacity)
        : nxui::Color(0.90f, 0.95f, 1.00f, 0.94f * t * opacity);
    nxui::Color bd = m_theme->cursorNormal.withAlpha(0.65f * t * opacity);

    ren.drawRoundedRect(r, bg, 10.f);
    ren.drawRoundedRectOutline(r, bd, 10.f, 1.5f);

    float tx = r.x + 12.f;
    float ty = r.y + (r.height - tsz.y * 0.72f) * 0.5f;
    ren.drawText(displayText, {tx, ty}, m_smallFont, m_theme->textPrimary.withAlpha(t * opacity), 0.78f);
}

void SettingsScreen::drawColorPicker(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || !m_smallFont || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    float open = m_colorPickerAnim.value();
    if (open <= 0.01f) return;

    auto& items = m_tabs[m_tabIndex].items;
    if (m_colorPickerRawIdx < 0 || m_colorPickerRawIdx >= (int)items.size()) return;
    auto& item = items[m_colorPickerRawIdx];
    if (item.type != ItemType::ColorPicker) return;

    nxui::Rect cr = contentRect(panel);
    float H = item.colorH, S = item.colorS, L = item.colorL;

    constexpr float popW = 320.f;
    constexpr float popH = 230.f;
    constexpr float pad = 16.f;
    constexpr float sliderH = 18.f;
    constexpr float sliderGap = 10.f;
    constexpr float labelW = 80.f;
    constexpr float swatchH = 42.f;
    constexpr float knobR = 9.f;

    float px = cr.x + (cr.width - popW) * 0.5f;
    float py = cr.y + (cr.height - popH) * 0.5f;

    float scale = 0.92f + 0.08f * open;
    float w = popW * scale;
    float h = popH * scale;
    float fx = px + (popW - w) * 0.5f;
    float fy = py + (popH - h) * 0.5f;
    nxui::Rect pop = { fx, fy, w, h };

    nxui::Color bg = (m_theme->mode == nxui::ThemeMode::Dark)
        ? nxui::Color(0.08f, 0.08f, 0.12f, 0.97f * opacity * open)
        : nxui::Color(0.96f, 0.96f, 0.98f, 0.98f * opacity * open);
    ren.drawRoundedRect(pop, bg, 14.f);
    ren.drawRoundedRectOutline(pop,
                               m_theme->panelBorder.withAlpha(0.7f * opacity * open),
                               14.f, 1.2f);

    ren.pushClipRect(pop.shrunk(2.f));

    float contentX = pop.x + pad;
    float contentW = pop.width - pad * 2.f;
    float curY = pop.y + pad;

    nxui::Color previewCol = nxui::Color::fromHSL(H, S, L, 1.f);
    nxui::Rect swatchRect = { contentX, curY, contentW, swatchH };
    ren.drawRoundedRect(swatchRect, previewCol.withAlpha(opacity * open), 8.f);
    ren.drawRoundedRectOutline(swatchRect,
                                m_theme->panelBorder.withAlpha(0.5f * opacity * open),
                                8.f, 1.f);
    curY += swatchH + pad;

    const char* labels[3] = { "H", "S", "L" };
    float vals[3] = { H, S, L };

    float sliderX = contentX + labelW;
    float sliderW = contentW - labelW - 40.f;

    for (int si = 0; si < 3; ++si) {
        float rowY = curY + si * (sliderH + sliderGap);
        float t = vals[si];
        bool active = (m_colorPickerOpen && si == m_colorPickerSlider);

        nxui::Color labelCol = active ? m_theme->textPrimary : m_theme->textSecondary;
        ren.drawText(labels[si], {contentX, rowY + 1.f}, m_smallFont,
                     labelCol.withAlpha(opacity * open), 0.72f);

        nxui::Rect track = { sliderX, rowY + (sliderH - 12.f) * 0.5f, sliderW, 12.f };
        if (si == 0) {
            int segs = 12;
            float segW = track.width / segs;
            for (int j = 0; j < segs; ++j) {
                float h0 = (float)j / segs;
                float h1 = (float)(j + 1) / segs;
                nxui::Color c0 = nxui::Color::fromHSL(h0, 0.9f, 0.5f, opacity * open);
                nxui::Color c1 = nxui::Color::fromHSL(h1, 0.9f, 0.5f, opacity * open);
                nxui::Rect seg = { track.x + j * segW, track.y, segW + 1.f, track.height };
                nxui::Color mixed(
                    (c0.r + c1.r) * 0.5f,
                    (c0.g + c1.g) * 0.5f,
                    (c0.b + c1.b) * 0.5f,
                    opacity * open
                );
                if (j == 0) {
                    ren.drawRoundedRect({seg.x, seg.y, seg.width, seg.height}, c0, 4.f);
                } else if (j == segs - 1) {
                    ren.drawRoundedRect({seg.x, seg.y, seg.width, seg.height}, c1, 4.f);
                } else {
                    ren.drawRect(seg, mixed);
                }
            }
        } else if (si == 1) {
            nxui::Color sLeft = nxui::Color::fromHSL(H, 0.f, L, opacity * open);
            nxui::Color sRight = nxui::Color::fromHSL(H, 1.f, L, opacity * open);
            nxui::Color sMid((sLeft.r + sRight.r) * 0.5f,
                             (sLeft.g + sRight.g) * 0.5f,
                             (sLeft.b + sRight.b) * 0.5f, opacity * open);
            ren.drawRoundedRect(track, sMid, 4.f);
            nxui::Rect lHalf = { track.x, track.y, track.width * 0.5f, track.height };
            nxui::Rect rHalf = { track.x + track.width * 0.5f, track.y, track.width * 0.5f, track.height };
            nxui::Color lMid((sLeft.r * 0.7f + sMid.r * 0.3f),
                             (sLeft.g * 0.7f + sMid.g * 0.3f),
                             (sLeft.b * 0.7f + sMid.b * 0.3f), opacity * open);
            nxui::Color rMid((sRight.r * 0.7f + sMid.r * 0.3f),
                             (sRight.g * 0.7f + sMid.g * 0.3f),
                             (sRight.b * 0.7f + sMid.b * 0.3f), opacity * open);
            ren.drawRect(lHalf, lMid);
            ren.drawRoundedRect({lHalf.x, lHalf.y, lHalf.width + 2.f, lHalf.height}, lMid, 4.f);
            ren.drawRoundedRect({rHalf.x - 2.f, rHalf.y, rHalf.width + 2.f, rHalf.height}, rMid, 4.f);
        } else {
            nxui::Color lLeft(0.f, 0.f, 0.f, opacity * open);
            nxui::Color lMid = nxui::Color::fromHSL(H, S, 0.5f, opacity * open);
            nxui::Color lRight(1.f, 1.f, 1.f, opacity * open);
            nxui::Rect leftH = { track.x, track.y, track.width * 0.5f, track.height };
            nxui::Rect rightH = { track.x + track.width * 0.5f, track.y, track.width * 0.5f, track.height };
            nxui::Color darkMix((lLeft.r + lMid.r) * 0.5f,
                                (lLeft.g + lMid.g) * 0.5f,
                                (lLeft.b + lMid.b) * 0.5f, opacity * open);
            nxui::Color lightMix((lMid.r + lRight.r) * 0.5f,
                                 (lMid.g + lRight.g) * 0.5f,
                                 (lMid.b + lRight.b) * 0.5f, opacity * open);
            ren.drawRoundedRect({leftH.x, leftH.y, leftH.width + 2.f, leftH.height}, darkMix, 4.f);
            ren.drawRoundedRect({rightH.x - 2.f, rightH.y, rightH.width + 2.f, rightH.height}, lightMix, 4.f);
        }

        ren.drawRoundedRectOutline(track,
                                    m_theme->panelBorder.withAlpha(0.4f * opacity * open),
                                    4.f, 0.8f);

        float knobX = sliderX + t * (sliderW - knobR * 2.f) + knobR;
        float knobY = rowY + sliderH * 0.5f;
        nxui::Color knobCol(1.f, 1.f, 1.f, opacity * open);
        ren.drawCircle({knobX, knobY}, knobR, knobCol, 24);
        ren.drawCircle({knobX, knobY}, knobR - 2.f,
                       nxui::Color::fromHSL(
                           (si == 0) ? t : H,
                           (si == 1) ? t : S,
                           (si == 2) ? t : L,
                           opacity * open),
                       24);

        if (active) {
            float pulse = 0.8f + 0.2f * (std::sin(m_uiTime * 5.0f) * 0.5f + 0.5f);
            nxui::Rect focRow = { sliderX - 4.f, rowY - 3.f, sliderW + 48.f, sliderH + 6.f };
            m_focusCursor.moveTo(focRow, 6.f, 0.06f);
        }

        int pct = (int)std::round(t * 100.f);
        std::string valStr = std::to_string(pct) + "%";
        float valX = sliderX + sliderW + 6.f;
        ren.drawText(valStr, {valX, rowY + 1.f}, m_smallFont,
                     m_theme->textSecondary.withAlpha(opacity * open), 0.66f);
    }

    ren.popClipRect();
}
