#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>

SettingsScreen::Tab settings::tabs::DisplayTab::build(SettingsScreen& screen) {
    (void)screen;
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
        SetSysTvSettings tv{};
        if (R_SUCCEEDED(setsysGetTvSettings(&tv)))
            it.floatVal = tv.underscan / 100.f;
        else
            it.floatVal = 1.f;
        it.anim01 = std::clamp(it.floatVal, 0.f, 1.f);
        it.onChange = [](SettingItem& self) {
            SetSysTvSettings tv{};
            if (R_SUCCEEDED(setsysGetTvSettings(&tv))) {
                tv.underscan = (u32)(self.floatVal * 100.f);
                setsysSetTvSettings(&tv);
            }
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