#include "TabBuilders.hpp"
#include "screen_fit.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>

SettingsScreen::Tab settings::tabs::DisplayTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.display", "Display");

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
