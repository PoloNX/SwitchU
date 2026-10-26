#include "TabBuilders.hpp"
#include "screen_fit.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>
#include <cmath>
#include <string>

SettingsScreen::Tab settings::tabs::DisplayTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.display", "Display");

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.grid_columns", "Home Grid Columns");
        it.type = ItemType::Slider;
        it.sliderSteps = 7;
        int cols = std::clamp(screen.m_gridColumns, 1, 8);
        it.floatVal = (float)(cols - 1) / 7.f;
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(cols);
        it.onChange = [&screen](SettingItem& self) {
            int cols = std::clamp(1 + (int)std::round(std::clamp(self.floatVal, 0.f, 1.f) * 7.f), 1, 8);
            self.floatVal = (float)(cols - 1) / 7.f;
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(cols);
            screen.m_gridColumns = cols;
            if (screen.m_gridColumnsCb) screen.m_gridColumnsCb(cols);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.grid_rows", "Home Grid Rows");
        it.type = ItemType::Slider;
        it.sliderSteps = 4;
        int rows = std::clamp(screen.m_gridRows, 1, 5);
        it.floatVal = (float)(rows - 1) / 4.f;
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(rows);
        it.onChange = [&screen](SettingItem& self) {
            int rows = std::clamp(1 + (int)std::round(std::clamp(self.floatVal, 0.f, 1.f) * 4.f), 1, 5);
            self.floatVal = (float)(rows - 1) / 4.f;
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(rows);
            screen.m_gridRows = rows;
            if (screen.m_gridRowsCb) screen.m_gridRowsCb(rows);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.sort_large_tiles", "Sort Large Tiles Too");
        it.description = i18n.tr("themeshop.options.sort_large_tiles_desc",
                                 "A-Z and Recent also move applications that use a larger tile size.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_sortLargeTiles;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_sortLargeTiles = self.boolVal;
            if (screen.m_sortLargeTilesCb) screen.m_sortLargeTilesCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.action_hints", "Action Hints");
        it.description = i18n.tr("themeshop.options.action_hints_desc",
                                 "Choose how contextual controls appear in the bottom-right corner.");
        it.type = ItemType::Selector;
        it.options = {
            i18n.tr("themeshop.options.hint_panel", "Panel"),
            i18n.tr("themeshop.options.hint_capsules", "Capsules")
        };
        it.intVal = std::clamp(screen.m_actionHintStyle, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_actionHintStyle = std::clamp(self.intVal, 0, 1);
            if (screen.m_actionHintStyleCb)
                screen.m_actionHintStyleCb(screen.m_actionHintStyle);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.cursor_motion", "Cursor Movement");
        it.type = ItemType::Selector;
        it.description = i18n.tr(
            "settings.display.cursor_motion_desc",
            "Choose whether the selection cursor slides or teleports with a visual pulse.");
        it.options = {
            i18n.tr("settings.display.cursor_translate", "Translate"),
            i18n.tr("settings.display.cursor_teleport", "Teleport")
        };
        it.intVal = std::clamp(screen.m_cursorMotionMode, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_cursorMotionMode = std::clamp(self.intVal, 0, 1);
            if (screen.m_cursorMotionModeCb)
                screen.m_cursorMotionModeCb(screen.m_cursorMotionMode);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.page_transition", "Page Transition");
        it.type = ItemType::Selector;
        it.description = i18n.tr(
            "settings.display.page_transition_desc",
            "Choose whether switching pages slides the grid or swaps it instantly.");
        it.options = {
            i18n.tr("settings.display.page_translate", "Translate"),
            i18n.tr("settings.display.page_teleport", "Teleport")
        };
        it.intVal = std::clamp(screen.m_pageTransitionMode, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_pageTransitionMode = std::clamp(self.intVal, 0, 1);
            if (screen.m_pageTransitionModeCb)
                screen.m_pageTransitionModeCb(screen.m_pageTransitionMode);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.display.brightness", "Brightness"); it.type = ItemType::Slider;
        float val = 0.5f;
        lblGetCurrentBrightnessSetting(&val);
        it.floatVal = val;
        it.anim01 = std::clamp(val, 0.f, 1.f);
        it.onChange = [](SettingItem& self) {
            lblSetCurrentBrightnessSetting(self.floatVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.display.auto_brightness", "Auto-Brightness"); it.type = ItemType::Toggle;
        it.description = i18n.tr("settings.display.auto_brightness_desc", "Adjust brightness automatically based on ambient light.");
        bool val = false;
        lblIsAutoBrightnessControlEnabled(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            if (self.boolVal)
                lblEnableAutoBrightnessControl();
            else
                lblDisableAutoBrightnessControl();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.display.burn_in", "Screen Burn-In Reduction"); it.type = ItemType::Toggle;
        it.description = i18n.tr("settings.display.burn_in_desc", "Reduce screen burn-in during long usage.");
        bool val = false;
        SetSysBacklightSettings bl{};
        if (R_SUCCEEDED(setsysGetBacklightSettings(&bl)))
            val = (bl.auto_brightness_flags & 1) != 0;
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            SetSysBacklightSettings bl{};
            if (R_SUCCEEDED(setsysGetBacklightSettings(&bl))) {
                if (self.boolVal)
                    bl.auto_brightness_flags |= 1;
                else
                    bl.auto_brightness_flags &= ~1u;
                setsysSetBacklightSettings(&bl);
            }
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.audio.tv_resolution", "TV Resolution"); it.type = ItemType::Selector;
        it.description = i18n.tr("settings.audio.tv_resolution_desc", "Adjust HDMI resolution. Auto uses recommended value.");
        it.options = {
            i18n.tr("common.auto", "Auto"),
            i18n.tr("settings.audio.res_720p", "720p"),
            i18n.tr("settings.audio.res_1080p", "1080p")
        };
        SetSysTvSettings tv{};
        if (R_SUCCEEDED(setsysGetTvSettings(&tv)))
            it.intVal = std::clamp((int)tv.tv_resolution, 0, 2);
        it.onChange = [](SettingItem& self) {
            SetSysTvSettings tv{};
            if (R_SUCCEEDED(setsysGetTvSettings(&tv))) {
                tv.tv_resolution = (s32)self.intVal;
                setsysSetTvSettings(&tv);
            }
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.audio.hdmi_rgb_range", "HDMI RGB Range"); it.type = ItemType::Selector;
        it.description = i18n.tr("settings.audio.hdmi_rgb_range_desc", "Full for PC monitors, Limited for most TVs.");
        it.options = {
            i18n.tr("common.auto", "Auto"),
            i18n.tr("settings.audio.full", "Full"),
            i18n.tr("settings.audio.limited", "Limited")
        };
        SetSysTvSettings tv{};
        if (R_SUCCEEDED(setsysGetTvSettings(&tv)))
            it.intVal = std::clamp((int)tv.rgb_range, 0, 2);
        it.onChange = [](SettingItem& self) {
            SetSysTvSettings tv{};
            if (R_SUCCEEDED(setsysGetTvSettings(&tv))) {
                tv.rgb_range = (s32)self.intVal;
                setsysSetTvSettings(&tv);
            }
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.display.tv_screen_size", "TV Screen Size"); it.type = ItemType::Slider;
        it.description = i18n.tr("settings.display.tv_screen_size_desc", "Adjust overscan so the full image is visible.");
        // The slider spans 80%..100% in 1% steps: 21 positions, 20 steps.
        constexpr u32 kMinUnderscan = 80;
        constexpr u32 kMaxUnderscan = 100;
        constexpr u32 kUnderscanRange = kMaxUnderscan - kMinUnderscan;
        u32 underscan = kMaxUnderscan;
        SetSysTvSettings tv{};
        if (R_SUCCEEDED(setsysGetTvSettings(&tv)))
            underscan = std::clamp(tv.underscan, kMinUnderscan, kMaxUnderscan);
        it.floatVal = static_cast<float>(underscan - kMinUnderscan) / static_cast<float>(kUnderscanRange);
        it.anim01 = std::clamp(it.floatVal, 0.f, 1.f);
        it.sliderSteps = static_cast<int>(kUnderscanRange);
        it.onChange = [=](SettingItem& self) {
            const u32 percent = kMinUnderscan
                + std::clamp(static_cast<u32>(self.floatVal * static_cast<float>(kUnderscanRange) + 0.5f),
                             0u, kUnderscanRange);
            SetSysTvSettings tv{};
            if (R_SUCCEEDED(setsysGetTvSettings(&tv))) {
                tv.underscan = percent;
                setsysSetTvSettings(&tv);
            }
            // Resize the managed layer now so the result is visible immediately,
            // without waiting for a re-dock (like qlaunch's live preview).
            switchu::menu::applyScreenUnderscan();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.display.album_storage", "Primary Album Storage"); it.type = ItemType::Selector;
        it.description = i18n.tr("settings.display.album_storage_desc", "Where screenshots and videos are saved.");
        it.options = {
            i18n.tr("settings.display.album_sd", "SD Card"),
            i18n.tr("settings.display.album_nand", "NAND")
        };
        SetSysPrimaryAlbumStorage storage = SetSysPrimaryAlbumStorage_SdCard;
        setsysGetPrimaryAlbumStorage(&storage);
        it.intVal = (storage == SetSysPrimaryAlbumStorage_SdCard) ? 0 : 1;
        it.onChange = [](SettingItem& self) {
            SetSysPrimaryAlbumStorage s = (self.intVal == 0)
                ? SetSysPrimaryAlbumStorage_SdCard
                : SetSysPrimaryAlbumStorage_Nand;
            setsysSetPrimaryAlbumStorage(s);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.display.ui_wireframe", "UI Wireframe"); it.type = ItemType::Toggle;
        it.description = i18n.tr("settings.display.ui_wireframe_desc", "Show outlines of all Box widgets for layout debugging.");
        it.boolVal = screen.m_wireframeEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_wireframeEnabled = self.boolVal;
            if (screen.m_wireframeCb) screen.m_wireframeCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
