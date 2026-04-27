#include "SettingsScreen.hpp"
#include "core/DebugLog.hpp"

#include <algorithm>
#include <cmath>

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
            int count = std::max(1, (int)sel.options.size());
            sel.intVal = std::clamp(m_dropdownHover, 0, count - 1);
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
            int count = std::max(1, (int)items[m_dropdownRawIdx].options.size());
            m_dropdownHover = (m_dropdownHover + count - 1) % count;
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
            int count = std::max(1, (int)items[m_dropdownRawIdx].options.size());
            m_dropdownHover = (m_dropdownHover + 1) % count;
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
    int focusIndex = 0;
    for (auto& item : items) {
        float height = item.type == ItemType::Section ? kSectionHeight : kRowHeight;
        if (item.focusable()) {
            if (focusIndex == m_contentIdx) break;
            ++focusIndex;
        }
        itemY += height;
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
    constexpr float kCardInsetX = 18.f;
    constexpr float kCardInsetY = 8.f;
    constexpr float kCardInnerInsetX = 14.f;
    constexpr float kCardInnerInsetY = 8.f;

    auto clampScrollTarget = [this, &cr](float value) {
        float maxScroll = std::max(0.f, contentTotalHeight() - cr.height + 20.f);
        return std::clamp(value, 0.f, maxScroll);
    };

    auto findContentHit = [this, &cr](float tx, float ty) {
        if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
            return -1;

        auto& items = m_tabs[m_tabIndex].items;
        float y = cr.y - m_scrollY;
        int focusIdx = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            float height = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
            nxui::Rect itemRect = { cr.x, y, cr.width, height };
            bool visible = y < cr.bottom() && y + height > cr.y;
            if (items[i].focusable()) {
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
        float y = cr.y - m_scrollY;
        int currentFocus = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            float height = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
            if (items[i].focusable()) {
                if (currentFocus == focusIdx) {
                    nxui::Rect cardRect = {
                        cr.x + kCardInsetX,
                        y + kCardInsetY,
                        std::max(0.f, cr.width - kCardInsetX * 2.f),
                        std::max(0.f, height - 6.f)
                    };
                    return nxui::Rect{
                        cardRect.x + kCardInnerInsetX,
                        cardRect.y + kCardInnerInsetY,
                        std::max(0.f, cardRect.width - kCardInnerInsetX * 2.f),
                        std::max(0.f, cardRect.height - kCardInnerInsetY * 2.f)
                    };
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
        newValue = std::clamp(std::round(newValue * 20.f) / 20.f, 0.f, 1.f);
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
        float listH = visible * 42.f + 12.f;

        float y = cr.y - m_scrollY;
        for (int i = 0; i < m_dropdownRawIdx; ++i)
            y += (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;

        float ctrlX = cr.x + cr.width * 0.40f;
        float ctrlW = cr.width * 0.60f;
        float dy = y + kRowHeight + 6.f;
        if (dy + listH > cr.bottom() - 4.f)
            dy = y - listH - 6.f;

        return nxui::Rect{ctrlX, dy, ctrlW, listH};
    };

    auto colorPickerRect = [&cr]() {
        constexpr float popW = 320.f;
        constexpr float popH = 230.f;
        return nxui::Rect{
            cr.x + (cr.width - popW) * 0.5f,
            cr.y + (cr.height - popH) * 0.5f,
            popW,
            popH
        };
    };

    auto colorPickerTrackRect = [&colorPickerRect](int sliderIndex) {
        constexpr float pad = 16.f;
        constexpr float sliderH = 18.f;
        constexpr float sliderGap = 10.f;
        constexpr float labelW = 80.f;
        constexpr float swatchH = 42.f;

        nxui::Rect pop = colorPickerRect();
        float contentX = pop.x + pad;
        float contentW = pop.width - pad * 2.f;
        float sliderX = contentX + labelW;
        float sliderW = contentW - labelW - 40.f;
        float rowY = pop.y + pad + swatchH + pad + sliderIndex * (sliderH + sliderGap);
        return nxui::Rect{sliderX, rowY + (sliderH - 12.f) * 0.5f, sliderW, 12.f};
    };

    auto applyColorPickerDrag = [this, &colorPickerTrackRect](float tx, float ty) {
        if (!m_colorPickerOpen || m_colorPickerRawIdx < 0 || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
            return false;

        auto& item = m_tabs[m_tabIndex].items[m_colorPickerRawIdx];
        if (item.type != ItemType::ColorPicker)
            return false;

        int hitSlider = -1;
        for (int i = 0; i < 3; ++i) {
            if (colorPickerTrackRect(i).expanded(10.f).contains(tx, ty)) {
                hitSlider = i;
                break;
            }
        }
        if (hitSlider < 0 && !m_touchDraggingColor)
            return false;
        if (hitSlider < 0)
            hitSlider = std::clamp(m_colorPickerSlider, 0, 2);

        nxui::Rect track = colorPickerTrackRect(hitSlider);
        if (track.width <= 0.f)
            return false;

        float newValue = (tx - track.x) / std::max(1.f, track.width);
        if (hitSlider == 0)
            newValue = std::round(std::clamp(newValue, 0.f, 1.f) * 36.f) / 36.f;
        else
            newValue = std::round(std::clamp(newValue, 0.f, 1.f) * 20.f) / 20.f;

        float* values[3] = { &item.colorH, &item.colorS, &item.colorL };
        if (std::abs(newValue - *values[hitSlider]) < 0.0001f) {
            m_colorPickerSlider = hitSlider;
            return true;
        }

        bool increasing = newValue > *values[hitSlider];
        *values[hitSlider] = newValue;
        m_colorPickerSlider = hitSlider;
        if (item.onChange) item.onChange(item);
        if (m_sliderSfxCb) m_sliderSfxCb(increasing);
        return true;
    };

    constexpr float kTapThreshold = 20.f;
    constexpr float kDragThreshold = 12.f;

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
        m_touchStartScroll = m_scrollTarget;
        m_touchScrolling = false;
        m_touchDraggingSlider = false;
        m_touchDraggingColor = false;

        if (m_dropdownOpen && m_dropdownRawIdx >= 0 && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
            auto& items = m_tabs[m_tabIndex].items;
            if (m_dropdownRawIdx < (int)items.size()) {
                auto& item = items[m_dropdownRawIdx];
                int total = (int)item.options.size();
                int visible = std::min(total, 6);
                float optH = 42.f;
                nxui::Rect dropRect = dropdownRect();
                if (dropRect.contains(tx, ty)) {
                    m_touchTarget = TouchTarget::Dropdown;
                    int start = 0;
                    if (total > visible)
                        start = std::clamp(m_dropdownHover - visible / 2, 0, total - visible);
                    float localY = ty - dropRect.y - 5.f;
                    int idx = start + (int)(localY / optH);
                    idx = std::clamp(idx, 0, total - 1);
                    m_touchHitIndex = idx;
                    m_touchOnSelected = (idx == m_dropdownHover);
                    return;
                }
            }
        }

        if (m_colorPickerOpen) {
            m_touchTarget = TouchTarget::ColorPicker;
            m_touchOnSelected = colorPickerRect().contains(tx, ty);
            if (m_touchOnSelected)
                m_touchDraggingColor = applyColorPickerDrag(tx, ty);
            return;
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
                    m_touchDirectControl = true;
                    m_focusArea = FocusArea::Content;
                    m_contentIdx = m_touchHitIndex;
                    scrollToFocused();
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
        float dx = std::abs(input.touchDeltaX());
        float dy = std::abs(input.touchDeltaY());

        if (m_touchTarget == TouchTarget::Content) {
            if (m_touchDraggingSlider && m_touchHitIndex >= 0) {
                applySliderDrag(m_touchHitIndex, input.touchX());
            } else if (!m_touchDirectControl && dy > kDragThreshold && dy > dx) {
                m_touchScrolling = true;
                m_scrollTarget = clampScrollTarget(m_touchStartScroll - (input.touchY() - m_touchStartY));
            }
        } else if (m_touchTarget == TouchTarget::ColorPicker && m_touchOnSelected) {
            if (m_touchDraggingColor || dx > 4.f || dy > 4.f)
                m_touchDraggingColor = applyColorPickerDrag(input.touchX(), input.touchY()) || m_touchDraggingColor;
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
            m_touchDraggingColor = false;
            return;
        }

        float dx = std::abs(input.touchDeltaX());
        float dy = std::abs(input.touchDeltaY());
        bool dragged = m_touchScrolling || m_touchDraggingSlider || m_touchDraggingColor
            || dx >= kTapThreshold || dy >= kTapThreshold;

        if (!dragged) {
            switch (m_touchTarget) {
            case TouchTarget::Tab:
                if (m_touchHitIndex >= 0 && m_touchHitIndex < (int)m_tabs.size()) {
                    if (m_touchOnSelected) {
                        if (focusableCount() > 0) {
                            m_focusArea = FocusArea::Content;
                            m_contentIdx = 0;
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
                            if (m_toggleSfxCb) m_toggleSfxCb(item.boolVal);
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
                            int picked = std::clamp(m_touchHitIndex, 0, count - 1);
                            m_dropdownHover = picked;
                            sel.intVal = picked;
                            if (sel.onChange) sel.onChange(sel);
                            if (m_activateSfxCb) m_activateSfxCb();
                        }
                    }
                    m_dropdownOpen = false;
                    m_dropdownRawIdx = -1;
                    m_dropdownAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
                }
                break;

            case TouchTarget::ColorPicker:
                if (!m_touchOnSelected) {
                    m_colorPickerOpen = false;
                    m_colorPickerRawIdx = -1;
                    m_colorPickerAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
                }
                break;

            case TouchTarget::None:
                if (m_dropdownOpen) {
                    m_dropdownOpen = false;
                    m_dropdownRawIdx = -1;
                    m_dropdownAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
                } else if (m_colorPickerOpen) {
                    m_colorPickerOpen = false;
                    m_colorPickerRawIdx = -1;
                    m_colorPickerAnim.set(0.f, 0.10f, nxui::Easing::outCubic);
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
        m_touchDraggingColor = false;
    }
}

void SettingsScreen::onContentUpdate(float dt) {
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

    nxui::Rect panel = panelRect();
    nxui::Rect tr = tabsRect(panel);
    nxui::Rect cr = contentRect(panel);

    float tabTargetY = tr.y + m_tabIndex * kTabRowHeight;
    m_tabGlowY.set(tabTargetY, 0.18f, nxui::Easing::outCubic);

    if (m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
        auto& items = m_tabs[m_tabIndex].items;
        float y = cr.y - m_scrollY;
        int focusIndex = 0;
        for (auto& item : items) {
            float height = item.type == ItemType::Section ? kSectionHeight : kRowHeight;
            if (item.focusable()) {
                if (focusIndex == m_contentIdx) {
                    m_contentGlowY.set(y, 0.14f, nxui::Easing::outCubic);
                    break;
                }
                ++focusIndex;
            }
            y += height;
        }
    }
}