#include "TabbedOverlayScreen.hpp"

#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>

namespace {
float sliderStepValue(const TabbedOverlayScreen::SettingItem& item) {
    return 1.f / (float)std::max(1, item.sliderSteps);
}

float quantizeSliderValue(const TabbedOverlayScreen::SettingItem& item, float value) {
    int steps = std::max(1, item.sliderSteps);
    return std::clamp(std::round(std::clamp(value, 0.f, 1.f) * (float)steps) / (float)steps, 0.f, 1.f);
}

std::string itemTypeName(TabbedOverlayScreen::ItemType type) {
    auto& i18n = nxui::I18n::instance();
    switch (type) {
        case TabbedOverlayScreen::ItemType::Toggle: return i18n.tr("accessibility.roles.toggle", "toggle");
        case TabbedOverlayScreen::ItemType::Slider: return i18n.tr("accessibility.roles.slider", "slider");
        case TabbedOverlayScreen::ItemType::Selector: return i18n.tr("accessibility.roles.selector", "selector");
        case TabbedOverlayScreen::ItemType::Action: return i18n.tr("accessibility.roles.button", "button");
        case TabbedOverlayScreen::ItemType::Progress: return i18n.tr("accessibility.roles.progress", "progress");
        case TabbedOverlayScreen::ItemType::Info: return i18n.tr("accessibility.roles.info", "information");
        case TabbedOverlayScreen::ItemType::Section: return i18n.tr("accessibility.roles.section", "section");
    }
    return {};
}

std::string itemValueText(const TabbedOverlayScreen::SettingItem& item) {
    auto& i18n = nxui::I18n::instance();
    switch (item.type) {
        case TabbedOverlayScreen::ItemType::Toggle:
            return item.boolVal
                ? i18n.tr("common.active", "Active")
                : i18n.tr("common.disabled", "Disabled");
        case TabbedOverlayScreen::ItemType::Slider: {
            if (!item.infoText.empty())
                return item.infoText;
            int pct = (int)std::round(std::clamp(item.floatVal, 0.f, 1.f) * 100.f);
            return std::to_string(pct) + " " + i18n.tr("accessibility.value.percent", "percent");
        }
        case TabbedOverlayScreen::ItemType::Selector: {
            if (item.options.empty())
                return {};
            int idx = std::clamp(item.intVal, 0, (int)item.options.size() - 1);
            return item.options[(size_t)idx];
        }
        case TabbedOverlayScreen::ItemType::Info:
        case TabbedOverlayScreen::ItemType::Progress:
            return item.infoText;
        default:
            return {};
    }
}

std::string itemActionText(const TabbedOverlayScreen::SettingItem& item) {
    auto& i18n = nxui::I18n::instance();
    switch (item.type) {
        case TabbedOverlayScreen::ItemType::Toggle:
            return i18n.tr("accessibility.settings.toggle_actions", "A to change. Left to return to tabs.");
        case TabbedOverlayScreen::ItemType::Slider:
            return i18n.tr("accessibility.settings.slider_actions", "Left or right to adjust. B to return to tabs.");
        case TabbedOverlayScreen::ItemType::Selector:
            return i18n.tr("accessibility.settings.selector_actions", "A or right to open the list. B to return to tabs.");
        case TabbedOverlayScreen::ItemType::Action:
            return i18n.tr("accessibility.settings.action_actions", "A to run the action. B to return to tabs.");
        default:
            return {};
    }
}
}

std::string TabbedOverlayScreen::currentAccessibilitySummary() const {
    std::string context;
    std::string position;
    std::string summary;
    bool forceRepeat = false;
    currentAccessibilityParts(context, position, summary, forceRepeat);
    if (context.empty() && position.empty())
        return summary;
    std::string out;
    if (!context.empty())
        out = context;
    if (!position.empty())
        out += (out.empty() ? "" : ". ") + position;
    if (!summary.empty())
        out += (out.empty() ? "" : ". ") + summary;
    return out;
}

void TabbedOverlayScreen::currentAccessibilityParts(std::string& context,
                                                    std::string& position,
                                                    std::string& summary,
                                                    bool& forceRepeat) const {
    context.clear();
    position.clear();
    summary.clear();
    forceRepeat = false;

    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
        return;

    const auto& tab = m_tabs[(size_t)m_tabIndex];
    auto& i18n = nxui::I18n::instance();
    if (m_focusArea == FocusArea::Tabs) {
        context = m_mode == ScreenMode::ThemeShop
            ? i18n.tr("accessibility.context.themes", "Themes")
            : m_mode == ScreenMode::GameOptions
                ? i18n.tr("game.options", "Software options")
                : m_mode == ScreenMode::FolderOptions
                    ? i18n.tr("folder.options", "Folder options")
                : i18n.tr("accessibility.context.settings", "Settings");
        if (m_accessibilitySpeakPosition) {
            position = std::to_string(m_tabIndex + 1) + " "
                     + i18n.tr("accessibility.context.of", "of") + " "
                     + std::to_string((int)m_tabs.size());
        }
        summary = i18n.tr("accessibility.settings.tab_prefix", "Tab") + " " + tab.name
                + (m_accessibilitySpeakHints
                    ? ". " + i18n.tr("accessibility.settings.tab_actions", "A or right to enter. Up and down to change tab. B to close.")
                    : "");
        return;
    }

    if (m_dropdownOpen && m_dropdownRawIdx >= 0 && m_dropdownRawIdx < (int)tab.items.size()) {
        const auto& item = tab.items[(size_t)m_dropdownRawIdx];
        if (!item.options.empty()) {
            int idx = std::clamp(m_dropdownHover, 0, (int)item.options.size() - 1);
            context = tab.name
                    + ". " + i18n.tr("accessibility.settings.list_prefix", "List")
                    + " " + item.label;
            if (m_accessibilitySpeakPosition) {
                position = std::to_string(idx + 1) + " "
                         + i18n.tr("accessibility.context.of", "of") + " "
                         + std::to_string((int)item.options.size());
            }
            summary = item.options[(size_t)idx];
            if (m_accessibilitySpeakHints)
                summary += ". " + i18n.tr("accessibility.settings.list_actions", "Up and down to choose. A to confirm. B to close the list.");
            return;
        }
    }

    int rawIdx = rawIndexFromFocusable(m_contentIdx);
    if (rawIdx < 0 || rawIdx >= (int)tab.items.size())
        return;

    const auto& item = tab.items[(size_t)rawIdx];
    context = tab.name;
    const int count = focusableCount();
    if (m_accessibilitySpeakPosition && count > 0) {
        position = std::to_string(m_contentIdx + 1) + " "
                 + i18n.tr("accessibility.context.of", "of") + " "
                 + std::to_string(count);
    }

    std::string out = item.label;
    std::string role = itemTypeName(item.type);
    if (!role.empty())
        out += ", " + role;
    std::string value = itemValueText(item);
    if (!value.empty())
        out += ". " + i18n.tr("accessibility.value.label", "Value") + ": " + value;
    if (!item.description.empty())
        out += ". " + item.description;
    std::string actions = m_accessibilitySpeakHints ? itemActionText(item) : std::string();
    if (!actions.empty())
        out += ". " + actions;
    summary = out;
}

void TabbedOverlayScreen::announceCurrentFocus() {
    if (m_accessibilityStructuredCb) {
        std::string context;
        std::string position;
        std::string summary;
        bool forceRepeat = false;
        currentAccessibilityParts(context, position, summary, forceRepeat);
        m_accessibilityStructuredCb(context, position, summary, forceRepeat, false);
        return;
    }
    if (m_accessibilityCb)
        m_accessibilityCb(currentAccessibilitySummary());
}

void TabbedOverlayScreen::announceCurrentValue() {
    if (!m_accessibilityCb || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
        return;
    const auto& tab = m_tabs[(size_t)m_tabIndex];
    int rawIdx = rawIndexFromFocusable(m_contentIdx);
    if (rawIdx < 0 || rawIdx >= (int)tab.items.size())
        return;
    const auto& item = tab.items[(size_t)rawIdx];
    std::string value = itemValueText(item);
    if (!value.empty())
        m_accessibilityCb(value);
}

void TabbedOverlayScreen::setupActions() {
    clearActions();
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() { onPressB(); });
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() { onPressA(); });
    addAction(static_cast<uint64_t>(nxui::Button::X), [this]() { onPressX(); });
    addDirectionAction(nxui::FocusDirection::UP,    [this]() { onNavUp(); });
    addDirectionAction(nxui::FocusDirection::DOWN,  [this]() { onNavDown(); });
    addDirectionAction(nxui::FocusDirection::LEFT,  [this]() { onNavLeft(); });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() { onNavRight(); });
}

void TabbedOverlayScreen::onPressB() {
    if (!m_active || m_animating) return;
    if (usesCustomContentLayout() && handleCustomPressB()) {
        announceCurrentFocus();
        return;
    }

    if (m_dropdownOpen) {
        closeDropdown(true);
        announceCurrentFocus();
        return;
    }
    if (m_focusArea == FocusArea::Content) {
        m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentFocus();
        return;
    }
    hide();
}

void TabbedOverlayScreen::onPressA() {
    if (!m_active || m_animating) return;
    if (usesCustomContentLayout() && handleCustomPressA()) {
        announceCurrentFocus();
        return;
    }
    if (usesCustomContentLayout() && m_focusArea == FocusArea::Content)
        return;

    if (m_focusArea == FocusArea::Tabs) {
        if (focusableCount() > 0) {
            m_focusArea = FocusArea::Content;
            m_contentIdx = 0;
            scrollToFocused();
            if (m_navSfxCb) m_navSfxCb();
            announceCurrentFocus();
        }
        return;
    }

    auto& items = m_tabs[m_tabIndex].items;
    int rawIdx = rawIndexFromFocusable(m_contentIdx);
    auto& item = items[rawIdx];

    if (m_dropdownOpen) {
        if (m_dropdownRawIdx >= 0 && m_dropdownRawIdx < (int)items.size()
            && items[m_dropdownRawIdx].type == ItemType::Selector) {
            auto& sel = items[m_dropdownRawIdx];
            int count = std::max(1, (int)sel.options.size());
            sel.intVal = std::clamp(m_dropdownHover, 0, count - 1);
            if (sel.onChange) sel.onChange(sel);
            if (m_activateSfxCb) m_activateSfxCb();
            announceCurrentFocus();
        }
        closeDropdown(true);
        return;
    }

    if (item.type == ItemType::Toggle) {
        item.boolVal = !item.boolVal;
        if (item.onChange) item.onChange(item);
        if (m_toggleSfxCb && !item.suppressToggleSfx) m_toggleSfxCb(item.boolVal);
        announceCurrentFocus();
    } else if (item.type == ItemType::Selector) {
        openDropdown(rawIdx);
        if (m_activateSfxCb) m_activateSfxCb();
        announceCurrentFocus();
    } else if (item.type == ItemType::Action) {
        if (item.onChange) item.onChange(item);
        if (m_activateSfxCb) m_activateSfxCb();
        announceCurrentFocus();
    }
}

void TabbedOverlayScreen::onPressX() {
    if (!m_active || m_animating)
        return;
    if (usesCustomContentLayout() && handleCustomPressX()) {
        announceCurrentFocus();
        return;
    }
}

void TabbedOverlayScreen::onNavUp() {
    if (!m_active || m_animating) return;
    if (usesCustomContentLayout() && handleCustomNavUp()) {
        announceCurrentFocus();
        return;
    }
    if (usesCustomContentLayout() && m_focusArea == FocusArea::Content)
        return;

    if (m_focusArea == FocusArea::Tabs) {
        if (m_tabIndex > 0) {
            m_tabSwitchDir = -1;
            --m_tabIndex;
            m_contentIdx = 0;
            m_scrollY = 0;
            m_scrollTarget = 0;
            m_contentSlideAnim.setImmediate(0.f);
            m_contentSlideAnim.set(1.f, 0.28f, nxui::Easing::outCubic);
            m_tabAccentW.setImmediate(1.f);
            m_tabAccentW.set(3.f, 0.32f, nxui::Easing::outExpo);
            closeDropdown(false);
            rebuildContentItems();
            if (m_navSfxCb) m_navSfxCb();
            announceCurrentFocus();
        }
        return;
    }

    if (m_dropdownOpen) {
        auto& items = m_tabs[m_tabIndex].items;
        if (m_dropdownRawIdx >= 0 && m_dropdownRawIdx < (int)items.size()
            && items[m_dropdownRawIdx].type == ItemType::Selector) {
            int count = std::max(1, (int)items[m_dropdownRawIdx].options.size());
            m_dropdownHover = (m_dropdownHover + count - 1) % count;
            if (m_navSfxCb) m_navSfxCb();
            announceCurrentFocus();
        }
        return;
    }

    if (m_contentIdx > 0) {
        --m_contentIdx;
        if (m_navSfxCb) m_navSfxCb();
        scrollToFocused();
        announceCurrentFocus();
    } else if (m_scrollTarget > 0.f) {
        // There can be non-focusable information/section rows before the
        // first control. At the focus boundary, keep D-pad repeat useful by
        // scrolling the remaining content instead of swallowing the input.
        m_scrollTarget = std::max(0.f, m_scrollTarget - kRowHeight);
    }
}

void TabbedOverlayScreen::onNavDown() {
    if (!m_active || m_animating) return;
    if (usesCustomContentLayout() && handleCustomNavDown()) {
        announceCurrentFocus();
        return;
    }
    if (usesCustomContentLayout() && m_focusArea == FocusArea::Content)
        return;

    if (m_focusArea == FocusArea::Tabs) {
        if (m_tabIndex < (int)m_tabs.size() - 1) {
            m_tabSwitchDir = 1;
            ++m_tabIndex;
            m_contentIdx = 0;
            m_scrollY = 0;
            m_scrollTarget = 0;
            m_contentSlideAnim.setImmediate(0.f);
            m_contentSlideAnim.set(1.f, 0.28f, nxui::Easing::outCubic);
            m_tabAccentW.setImmediate(1.f);
            m_tabAccentW.set(3.f, 0.32f, nxui::Easing::outExpo);
            closeDropdown(false);
            rebuildContentItems();
            if (m_navSfxCb) m_navSfxCb();
            announceCurrentFocus();
        }
        return;
    }

    if (m_dropdownOpen) {
        auto& items = m_tabs[m_tabIndex].items;
        if (m_dropdownRawIdx >= 0 && m_dropdownRawIdx < (int)items.size()
            && items[m_dropdownRawIdx].type == ItemType::Selector) {
            int count = std::max(1, (int)items[m_dropdownRawIdx].options.size());
            m_dropdownHover = (m_dropdownHover + 1) % count;
            if (m_navSfxCb) m_navSfxCb();
            announceCurrentFocus();
        }
        return;
    }

    if (m_contentIdx < focusableCount() - 1) {
        ++m_contentIdx;
        if (m_navSfxCb) m_navSfxCb();
        scrollToFocused();
        announceCurrentFocus();
    } else {
        constexpr float kContentTopPad = 16.f;
        const nxui::Rect cr = contentRect();
        const float maxScroll = std::max(
            0.f, contentTotalHeight() + kContentTopPad - cr.height + 20.f);
        if (m_scrollTarget < maxScroll)
            m_scrollTarget = std::min(maxScroll, m_scrollTarget + kRowHeight);
    }
}

void TabbedOverlayScreen::onNavLeft() {
    if (!m_active || m_animating) return;
    if (usesCustomContentLayout() && handleCustomNavLeft()) {
        announceCurrentFocus();
        return;
    }
    if (usesCustomContentLayout() && m_focusArea == FocusArea::Content)
        return;

    if (m_dropdownOpen) {
        closeDropdown(true);
        announceCurrentFocus();
        return;
    }

    if (m_focusArea == FocusArea::Tabs) return;

    auto& items = m_tabs[m_tabIndex].items;
    int rawIdx = rawIndexFromFocusable(m_contentIdx);
    auto& item = items[rawIdx];

    if (item.type == ItemType::Slider) {
        item.floatVal = quantizeSliderValue(item, item.floatVal - sliderStepValue(item));
        item.anim01 = item.floatVal;
        if (item.onChange) item.onChange(item);
        if (m_sliderSfxCb) m_sliderSfxCb(false);
        announceCurrentValue();
    } else {
        m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentFocus();
    }
}

void TabbedOverlayScreen::onNavRight() {
    if (!m_active || m_animating) return;
    if (usesCustomContentLayout() && handleCustomNavRight()) {
        announceCurrentFocus();
        return;
    }
    if (usesCustomContentLayout() && m_focusArea == FocusArea::Content)
        return;

    if (m_dropdownOpen) {
        closeDropdown(true);
        announceCurrentFocus();
        return;
    }

    if (m_focusArea == FocusArea::Tabs) {
        if (focusableCount() > 0) {
            m_focusArea = FocusArea::Content;
            m_contentIdx = 0;
            scrollToFocused();
            if (m_navSfxCb) m_navSfxCb();
            announceCurrentFocus();
        }
        return;
    }

    auto& items = m_tabs[m_tabIndex].items;
    int rawIdx = rawIndexFromFocusable(m_contentIdx);
    auto& item = items[rawIdx];

    if (item.type == ItemType::Slider) {
        item.floatVal = quantizeSliderValue(item, item.floatVal + sliderStepValue(item));
        item.anim01 = item.floatVal;
        if (item.onChange) item.onChange(item);
        if (m_sliderSfxCb) m_sliderSfxCb(true);
        announceCurrentValue();
    } else if (item.type == ItemType::Selector) {
        openDropdown(rawIdx);
        if (m_activateSfxCb) m_activateSfxCb();
        announceCurrentFocus();
    } else if (item.type == ItemType::Action) {
        if (item.onChange) item.onChange(item);
        if (m_activateSfxCb) m_activateSfxCb();
        announceCurrentFocus();
    }
}

void TabbedOverlayScreen::scrollToFocused() {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    auto& items = m_tabs[m_tabIndex].items;
    nxui::Rect cr = contentRect();
    constexpr float kContentTopPad = 16.f;
    float itemY = kContentTopPad;
    float focusedHeight = kRowHeight;
    int focusIndex = 0;
    for (auto& item : items) {
        float height = itemHeight(item, cr.width);
        if (itemFocusable(item)) {
            if (focusIndex == m_contentIdx) {
                focusedHeight = height;
                break;
            }
            ++focusIndex;
        }
        itemY += height;
    }
    if (itemY - m_scrollTarget < 0)
        m_scrollTarget = itemY;
    if (itemY + focusedHeight - m_scrollTarget > cr.height)
        m_scrollTarget = itemY + focusedHeight - cr.height;
    float maxScroll = std::max(0.f, contentTotalHeight() + kContentTopPad - cr.height + 20.f);
    m_scrollTarget = std::clamp(m_scrollTarget, 0.f, maxScroll);
}

void TabbedOverlayScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_animating) return;

    nxui::Rect panel = panelRect();
    nxui::Rect tr = tabsRect(panel);
    nxui::Rect cr = contentRect(panel);
    if (usesCustomContentLayout() && handleCustomTouch(input, panel, tr, cr))
        return;

    constexpr float kContentTopPad = 16.f;
    constexpr float kCardInsetX = 18.f;
    constexpr float kCardInsetY = 8.f;

    auto clampScrollTarget = [this, &cr](float value) {
        float maxScroll = std::max(0.f, contentTotalHeight() + kContentTopPad - cr.height + 20.f);
        return std::clamp(value, 0.f, maxScroll);
    };

    auto findContentHit = [this, &cr](float tx, float ty) {
        if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
            return -1;

        auto& items = m_tabs[m_tabIndex].items;
        float y = cr.y + kContentTopPad - m_scrollY;
        int focusIdx = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            float height = itemHeight(items[i], cr.width);
            float insetY = (items[i].type == ItemType::Section) ? 1.f : kCardInsetY;
            float cardH = std::max(0.f, height - (items[i].type == ItemType::Section ? 2.f : 6.f));
            nxui::Rect itemRect = {
                cr.x + kCardInsetX,
                y + insetY,
                std::max(0.f, cr.width - kCardInsetX * 2.f),
                cardH
            };
            bool visible = y < cr.bottom() && y + height > cr.y;
            if (itemFocusable(items[i])) {
                if (visible && itemRect.contains(tx, ty))
                    return focusIdx;
                ++focusIdx;
            }
            y += height;
        }
        return -1;
    };

    auto contentRowRect = [this, &cr](int focusIdx) {
        if (focusIdx < 0 || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
            return nxui::Rect{0.f, 0.f, 0.f, 0.f};

        auto& items = m_tabs[m_tabIndex].items;
        float y = cr.y + kContentTopPad - m_scrollY;
        int currentFocus = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            float height = itemHeight(items[i], cr.width);
            if (itemFocusable(items[i])) {
                if (currentFocus == focusIdx) {
                    float insetY = (items[i].type == ItemType::Section) ? 1.f : kCardInsetY;
                    float cardH = std::max(0.f, height - (items[i].type == ItemType::Section ? 2.f : 6.f));
                    nxui::Rect cardRect = {
                        cr.x + kCardInsetX,
                        y + insetY,
                        std::max(0.f, cr.width - kCardInsetX * 2.f),
                        cardH
                    };
                    return cardRect;
                }
                ++currentFocus;
            }
            y += height;
        }

        return nxui::Rect{0.f, 0.f, 0.f, 0.f};
    };

    auto sliderTrackRect = [this, &contentRowRect](int focusIdx) {
        if (focusIdx < 0)
            return nxui::Rect{0.f, 0.f, 0.f, 0.f};

        int rawIdx = rawIndexFromFocusable(focusIdx);
        auto& item = m_tabs[m_tabIndex].items[rawIdx];
        if (item.type != ItemType::Slider)
            return nxui::Rect{0.f, 0.f, 0.f, 0.f};

        nxui::Rect rowRect = contentRowRect(focusIdx);
        float rightW = std::max(170.f, rowRect.width * 0.42f);
        float trackW = std::clamp(rightW - 44.f - 10.f, 110.f, 260.f);
        float trackX = rowRect.right() - rightW;
        float trackY = rowRect.y + (rowRect.height - 12.f) * 0.5f;
        return nxui::Rect{trackX, trackY, trackW, 12.f};
    };

    auto contentControlRect = [this, &contentRowRect, &sliderTrackRect](int focusIdx) {
        if (focusIdx < 0 || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
            return nxui::Rect{0.f, 0.f, 0.f, 0.f};

        int rawIdx = rawIndexFromFocusable(focusIdx);
        auto& item = m_tabs[m_tabIndex].items[rawIdx];
        nxui::Rect rowRect = contentRowRect(focusIdx);
        switch (item.type) {
            case ItemType::Toggle: {
                constexpr float kTrackW = 64.f;
                constexpr float kTrackH = 32.f;
                return nxui::Rect{
                    rowRect.right() - kTrackW,
                    rowRect.y + (rowRect.height - kTrackH) * 0.5f,
                    kTrackW,
                    kTrackH
                }.expanded(10.f);
            }
            case ItemType::Slider:
                return sliderTrackRect(focusIdx).expanded(18.f);
            case ItemType::Selector: {
                float w = std::max(210.f, rowRect.width * 0.48f);
                float h = std::max(38.f, rowRect.height - 14.f);
                return nxui::Rect{
                    rowRect.right() - w,
                    rowRect.y + (rowRect.height - h) * 0.5f,
                    w,
                    h
                }.expanded(10.f);
            }
            default:
                return nxui::Rect{0.f, 0.f, 0.f, 0.f};
        }
    };

    auto applySliderDrag = [this, &sliderTrackRect](int focusIdx, float tx) {
        if (focusIdx < 0)
            return false;

        nxui::Rect track = sliderTrackRect(focusIdx);
        if (track.width <= 0.f)
            return false;

        int rawIdx = rawIndexFromFocusable(focusIdx);
        auto& item = m_tabs[m_tabIndex].items[rawIdx];

        constexpr float kKnobW = 18.f;
        float newValue = (tx - track.x - kKnobW * 0.5f) / std::max(1.f, track.width - kKnobW);
        newValue = quantizeSliderValue(item, newValue);
        if (std::abs(newValue - item.floatVal) < 0.0001f)
            return true;

        bool increasing = newValue > item.floatVal;
        item.floatVal = newValue;
        item.anim01 = newValue;
        if (item.onChange) item.onChange(item);
        if (m_sliderSfxCb) m_sliderSfxCb(increasing);
        return true;
    };

    auto dropdownRect = [this, &cr]() {
        if (!m_dropdownOpen || m_dropdownRawIdx < 0 || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
            return nxui::Rect{0.f, 0.f, 0.f, 0.f};

        auto& items = m_tabs[m_tabIndex].items;
        if (m_dropdownRawIdx >= (int)items.size())
            return nxui::Rect{0.f, 0.f, 0.f, 0.f};

        auto& item = items[m_dropdownRawIdx];
        int total = (int)item.options.size();
        int visible = std::min(total, 6);
        float listH = visible * 46.f + 16.f;

        float y = cr.y + kContentTopPad - m_scrollY;
        for (int i = 0; i < m_dropdownRawIdx; ++i)
            y += itemHeight(items[i], cr.width);

        float ctrlX = cr.x + cr.width * 0.40f;
        float ctrlW = cr.width * 0.60f;
        float dy = y + itemHeight(items[m_dropdownRawIdx], cr.width) + 6.f;
        if (dy + listH > cr.bottom() - 4.f)
            dy = y - listH - 6.f;

        return nxui::Rect{ctrlX, dy, ctrlW, listH};
    };

    auto dropdownIndexAt = [this, &dropdownRect](float tx, float ty) {
        if (!m_dropdownOpen || m_dropdownRawIdx < 0 || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
            return -1;

        auto& items = m_tabs[m_tabIndex].items;
        if (m_dropdownRawIdx >= (int)items.size())
            return -1;

        auto& item = items[m_dropdownRawIdx];
        int total = (int)item.options.size();
        int visible = std::min(total, 6);
        if (total <= 0 || visible <= 0)
            return -1;

        nxui::Rect dropRect = dropdownRect();
        if (!dropRect.expanded(18.f).contains(tx, ty))
            return -1;

        float visualStart = 0.f;
        if (total > visible)
            visualStart = std::clamp(m_dropdownVisualStart, 0.f, (float)(total - visible));
        int start = std::clamp((int)std::floor(visualStart), 0, std::max(0, total - visible));
        float rowOffset = (float)start - visualStart;

        constexpr float optH = 46.f;
        float localY = std::clamp(ty - dropRect.y - 9.f, 0.f, optH * (float)visible - 0.001f);
        int idx = start + (int)std::floor(localY / optH - rowOffset);
        return std::clamp(idx, 0, total - 1);
    };

    constexpr float kTapThreshold = 20.f;
    constexpr float kPanThreshold = 6.f;

    if (input.touchDown()) {
        if (m_ignoreInitialTouchRelease)
            m_ignoreInitialTouchRelease = false;

        float tx = input.touchX();
        float ty = input.touchY();
        m_touchTarget = TouchTarget::None;
        m_touchHitIndex = -1;
        m_touchOnSelected = false;
        m_touchDirectControl = false;
        m_touchStartX = tx;
        m_touchStartY = ty;
        m_touchStartScroll = m_scrollY;
        m_touchStartDropdownVisualStart = m_dropdownVisualStart;
        m_touchScrolling = false;
        m_touchDraggingSlider = false;

        if (m_dropdownOpen && m_dropdownRawIdx >= 0 && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
            auto& items = m_tabs[m_tabIndex].items;
            if (m_dropdownRawIdx < (int)items.size()) {
                nxui::Rect dropRect = dropdownRect();
                if (dropRect.contains(tx, ty)) {
                    m_touchTarget = TouchTarget::Dropdown;
                    int idx = dropdownIndexAt(tx, ty);
                    m_touchHitIndex = idx;
                    m_touchOnSelected = (idx == m_dropdownHover);
                    return;
                }
            }
        }

        if (tr.contains(tx, ty)) {
            m_touchTarget = TouchTarget::Tab;
            int idx = (int)((ty - tr.y) / kTabRowHeight);
            idx = std::clamp(idx, 0, (int)m_tabs.size() - 1);
            m_touchHitIndex = idx;
            m_touchOnSelected = (idx == m_tabIndex && m_focusArea == FocusArea::Tabs);
            return;
        }

        if (cr.contains(tx, ty) && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
            m_touchTarget = TouchTarget::Content;
            m_touchHitIndex = findContentHit(tx, ty);
            m_touchOnSelected = (m_touchHitIndex >= 0
                && m_touchHitIndex == m_contentIdx
                && m_focusArea == FocusArea::Content);

            if (m_touchHitIndex >= 0) {
                nxui::Rect controlRect = contentControlRect(m_touchHitIndex);
                if (controlRect.width > 0.f && controlRect.contains(tx, ty)) {
                    m_touchDirectControl = m_touchOnSelected;
                }

                if (m_touchDirectControl) {
                    int rawIdx = rawIndexFromFocusable(m_touchHitIndex);
                    auto& item = m_tabs[m_tabIndex].items[rawIdx];
                    if (item.type == ItemType::Slider) {
                        m_touchDraggingSlider = true;
                        applySliderDrag(m_touchHitIndex, tx);
                    }
                } else if (m_touchOnSelected) {
                    nxui::Rect track = sliderTrackRect(m_touchHitIndex);
                    if (track.width > 0.f && track.expanded(12.f).contains(tx, ty)) {
                        m_touchDraggingSlider = true;
                        applySliderDrag(m_touchHitIndex, tx);
                    }
                }
            }
            return;
        }
    }

    if (input.isTouching()) {
        float tx = input.touchX();
        float ty = input.touchY();
        float dx = std::abs(tx - m_touchStartX);
        float dy = std::abs(ty - m_touchStartY);

        if (m_touchTarget == TouchTarget::None && cr.contains(tx, ty)) {
            m_touchTarget = TouchTarget::Content;
            m_touchHitIndex = -1;
            m_touchOnSelected = false;
            m_touchDirectControl = false;
            m_touchStartX = tx;
            m_touchStartY = ty;
            m_touchStartScroll = m_scrollY;
            m_touchScrolling = true;
        }

        if (m_touchTarget == TouchTarget::Content) {
            if (m_touchDraggingSlider && m_touchHitIndex >= 0) {
                applySliderDrag(m_touchHitIndex, tx);
            } else {
                if (!m_touchScrolling
                    && (dx > kPanThreshold || dy > kPanThreshold)
                    && dy > dx) {
                    m_touchDirectControl = false;
                    m_touchScrolling = true;
                    m_touchStartX = tx;
                    m_touchStartY = ty;
                    m_touchStartScroll = m_scrollY;
                }

                if (m_touchScrolling) {
                    float nextScroll = clampScrollTarget(m_touchStartScroll - (ty - m_touchStartY));
                    m_scrollTarget = nextScroll;
                    m_scrollY = nextScroll;
                }
            }
        } else if (m_touchTarget == TouchTarget::Dropdown) {
            auto& items = m_tabs[m_tabIndex].items;
            int count = (m_dropdownRawIdx >= 0 && m_dropdownRawIdx < (int)items.size())
                ? (int)items[m_dropdownRawIdx].options.size()
                : 0;
            if (count > 0) {
                if (!m_touchScrolling
                    && (dx > kPanThreshold || dy > kPanThreshold)
                    && dy > dx) {
                    m_touchScrolling = true;
                    m_touchStartX = tx;
                    m_touchStartY = ty;
                    m_touchStartDropdownVisualStart = m_dropdownVisualStart;
                }

                if (m_touchScrolling) {
                    int visible = std::min(count, 6);
                    float maxStart = (float)std::max(0, count - visible);
                    m_dropdownVisualStart = std::clamp(
                        m_touchStartDropdownVisualStart - (ty - m_touchStartY) / 46.f,
                        0.f,
                        maxStart);
                    int idx = dropdownIndexAt(tx, ty);
                    if (idx >= 0 && idx != m_dropdownHover) {
                        m_dropdownHover = idx;
                        if (m_navSfxCb) m_navSfxCb();
                    }
                    return;
                }
            }

            if (!m_touchScrolling) {
                int idx = dropdownIndexAt(tx, ty);
                if (idx >= 0 && idx != m_touchHitIndex) {
                    if (idx != m_dropdownHover) {
                        m_dropdownHover = idx;
                        if (m_navSfxCb) m_navSfxCb();
                    }
                }
            }
        }
    }

    if (input.touchUp()) {
        if (m_ignoreInitialTouchRelease) {
            m_ignoreInitialTouchRelease = false;
            m_touchTarget = TouchTarget::None;
            m_touchHitIndex = -1;
            m_touchOnSelected = false;
            m_touchDirectControl = false;
            m_touchScrolling = false;
            m_touchDraggingSlider = false;
            return;
        }

        float dx = std::abs(input.touchDeltaX());
        float dy = std::abs(input.touchDeltaY());
        bool dragged = m_touchScrolling || m_touchDraggingSlider
            || dx >= kTapThreshold || dy >= kTapThreshold;

        if (!dragged) {
            switch (m_touchTarget) {
            case TouchTarget::Tab:
                if (m_touchHitIndex >= 0 && m_touchHitIndex < (int)m_tabs.size()) {
                    if (m_touchOnSelected) {
                        if (focusableCount() > 0) {
                            m_focusArea = FocusArea::Content;
                            m_contentIdx = 0;
                            scrollToFocused();
                            if (m_navSfxCb) m_navSfxCb();
                        }
                    } else {
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
                            closeDropdown(false);
                            rebuildContentItems();
                        }
                        if (m_navSfxCb) m_navSfxCb();
                    }
                }
                break;

            case TouchTarget::Content:
                if (m_touchHitIndex >= 0 && m_touchHitIndex < focusableCount()) {
                    if (m_touchDirectControl) {
                        m_focusArea = FocusArea::Content;
                        m_contentIdx = m_touchHitIndex;
                        scrollToFocused();

                        int rawIdx = rawIndexFromFocusable(m_touchHitIndex);
                        auto& item = m_tabs[m_tabIndex].items[rawIdx];
                        if (item.type == ItemType::Toggle) {
                            item.boolVal = !item.boolVal;
                            item.anim01 = item.boolVal ? 1.f : 0.f;
                            if (item.onChange) item.onChange(item);
                            if (m_toggleSfxCb && !item.suppressToggleSfx) m_toggleSfxCb(item.boolVal);
                        } else if (item.type == ItemType::Selector) {
                            onPressA();
                        }
                    } else if (m_touchOnSelected) {
                        onPressA();
                    } else {
                        m_focusArea = FocusArea::Content;
                        m_contentIdx = m_touchHitIndex;
                        scrollToFocused();
                        if (m_navSfxCb) m_navSfxCb();
                    }
                }
                break;

            case TouchTarget::Dropdown:
                if (m_touchHitIndex >= 0) {
                    if (m_dropdownRawIdx >= 0 && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
                        auto& items = m_tabs[m_tabIndex].items;
                        if (m_dropdownRawIdx < (int)items.size()) {
                            auto& sel = items[m_dropdownRawIdx];
                            int count = std::max(1, (int)sel.options.size());
                            int releasedIdx = dropdownIndexAt(input.touchX(), input.touchY());
                            if (releasedIdx < 0)
                                break;
                            int picked = std::clamp(releasedIdx, 0, count - 1);
                            if (m_touchOnSelected && picked == m_dropdownHover) {
                                sel.intVal = picked;
                                if (sel.onChange) sel.onChange(sel);
                                if (m_activateSfxCb) m_activateSfxCb();
                                closeDropdown(true);
                            } else {
                                m_dropdownHover = picked;
                                if (m_navSfxCb) m_navSfxCb();
                            }
                        }
                    }
                }
                break;

            case TouchTarget::None:
                if (m_dropdownOpen) {
                    closeDropdown(true);
                } else if (!panel.contains(input.touchX(), input.touchY())) {
                    hide();
                }
                break;
            }
        }

        m_touchTarget = TouchTarget::None;
        m_touchHitIndex = -1;
        m_touchOnSelected = false;
        m_touchDirectControl = false;
        m_touchScrolling = false;
        m_touchDraggingSlider = false;
    }
}

void TabbedOverlayScreen::onContentUpdate(float dt) {
    if (m_deferredRefresh) {
        m_deferredRefresh = false;
        refreshTranslations();
    }

    if (m_animating) {
        m_animT += dt / kAnimDuration;
        if (m_animT >= 1.f) {
            m_animT = 1.f;
            m_animating = false;
            if (!m_showing) {
                m_active = false;
                syncPanelState(0.f);
                setVisible(false);
                if (m_closedCb) m_closedCb();
                return;
            }
        }
    }

    syncPanelState(visibilityProgress());

    m_scrollY += (m_scrollTarget - m_scrollY) * std::min(1.f, dt * 14.f);
    m_uiTime += dt;

    if (m_active && !m_animating && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
        auto& tab = m_tabs[m_tabIndex];
        if (tab.onUpdate) tab.onUpdate(tab, *this);
    }

    updateCustomContent(dt);

    m_focusCursor.update(dt);
    m_tabReveal.update(std::min(dt, 0.03f));
    m_dropdownAnim.update(dt);
    if ((m_dropdownOpen || m_dropdownClosing)
        && !(m_touchTarget == TouchTarget::Dropdown && m_touchScrolling)
        && m_dropdownRawIdx >= 0
        && m_tabIndex >= 0
        && m_tabIndex < (int)m_tabs.size()
        && m_dropdownRawIdx < (int)m_tabs[m_tabIndex].items.size()) {
        auto& item = m_tabs[m_tabIndex].items[m_dropdownRawIdx];
        int total = (int)item.options.size();
        int visible = std::min(total, 6);
        float targetStart = 0.f;
        if (total > visible)
            targetStart = (float)std::clamp(m_dropdownHover - visible / 2, 0, total - visible);
        m_dropdownVisualStart += (targetStart - m_dropdownVisualStart) * std::min(1.f, dt * 18.f);
        if (std::abs(targetStart - m_dropdownVisualStart) < 0.001f)
            m_dropdownVisualStart = targetStart;
    }
    if (m_dropdownClosing && m_dropdownAnim.value() <= 0.01f) {
        m_dropdownClosing = false;
        m_dropdownRawIdx = -1;
        m_dropdownAnim.setImmediate(0.f);
    }
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

    if (m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
        auto& items = m_tabs[m_tabIndex].items;
        for (auto& item : items) {
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
}
