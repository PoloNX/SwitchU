#include "TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include "smi_commands.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string toLowerCopy(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool containsInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty())
        return true;
    return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

std::vector<std::string> loadAllTimezoneNames() {
    std::vector<std::string> out;
    s32 total = 0;
    if (R_FAILED(timeGetTotalLocationNameCount(&total)) || total <= 0)
        return out;

    out.reserve(static_cast<size_t>(total));
    constexpr s32 kChunk = 64;
    TimeLocationName buf[kChunk];
    for (s32 index = 0; index < total; ) {
        s32 got = 0;
        if (R_FAILED(timeLoadLocationNameList(index, buf, kChunk, &got)) || got <= 0)
            break;
        for (s32 i = 0; i < got; ++i) {
            if (buf[i].name[0] != '\0')
                out.emplace_back(buf[i].name);
        }
        index += got;
    }
    return out;
}

std::string nintendoSyncLabel() {
    return nxui::I18n::instance().tr(
        "settings.system.internet_time",
        "Synchronize Clock via Nintendo Servers");
}

std::string publicSyncLabel() {
    return nxui::I18n::instance().tr(
        "settings.system.web_time",
        "Synchronize Clock via Public Servers");
}

std::string timezoneLabel() {
    return nxui::I18n::instance().tr("settings.system.timezone", "Timezone");
}

} // namespace

namespace settings::tabs {

void SystemTab::setToggleByLabel(SettingsScreen& screen, const std::string& label, bool enabled) {
    for (auto& tab : screen.m_tabs) {
        for (auto& item : tab.items) {
            if (item.type == SettingsScreen::ItemType::Toggle && item.label == label) {
                item.boolVal = enabled;
                item.anim01 = enabled ? 1.f : 0.f;
            }
        }
    }
}

void SystemTab::disableAllClockSync(SettingsScreen& screen) {
    bool automatic = false;
    if (R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&automatic))
        && automatic) {
        const Result rc = switchu::menu::smi_cmd::setInternetTimeSync(false);
        DebugLog::log("[settings-time] disable nintendo for manual rc=0x%X", rc);
    }
    setToggleByLabel(screen, nintendoSyncLabel(), false);

    if (screen.m_webClockSyncEnabled) {
        screen.m_webClockSyncEnabled = false;
        setToggleByLabel(screen, publicSyncLabel(), false);
        if (screen.m_webClockSyncCb)
            screen.m_webClockSyncCb(false);
    }
}

bool SystemTab::anyClockSyncEnabled(const SettingsScreen& screen) {
    if (screen.m_webClockSyncEnabled)
        return true;
    bool automatic = false;
    if (R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&automatic)))
        return automatic;
    return false;
}

bool currentDateTimeValue(TabbedOverlayScreen::DateTimeEditorValue& value) {
    u64 timestamp = 0;
    TimeCalendarTime calendar{};
    TimeCalendarAdditionalInfo additional{};
    if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &timestamp)) ||
        R_FAILED(timeToCalendarTimeWithMyRule(timestamp, &calendar, &additional)))
        return false;
    value.year = calendar.year;
    value.month = calendar.month;
    value.day = calendar.day;
    value.hour = calendar.hour;
    value.minute = calendar.minute;
    return true;
}

bool setManualDateTime(
    SettingsScreen& screen,
    const TabbedOverlayScreen::DateTimeEditorValue& value) {
    auto& i18n = nxui::I18n::instance();
    switchu::smi::ManualDateTimeArgs daemonArgs{};
    daemonArgs.year = static_cast<uint32_t>(value.year);
    daemonArgs.month = static_cast<uint32_t>(value.month);
    daemonArgs.day = static_cast<uint32_t>(value.day);
    daemonArgs.hour = static_cast<uint32_t>(value.hour);
    daemonArgs.minute = static_cast<uint32_t>(value.minute);
    Result daemonRc = switchu::menu::smi_cmd::setManualDateTime(daemonArgs);
    DebugLog::log("[settings-time] daemon SetManualDateTime rc=0x%X", daemonRc);
    if (R_SUCCEEDED(daemonRc)) {
        screen.requestToast(i18n.tr(
            "settings.system.manual_time_saved", "Date and time updated."));
        return true;
    }
    const std::string failureText = i18n.tr(
        "settings.system.time_change_failed",
        "The date and time setting could not be changed.");
    char fallback[128];
    std::snprintf(fallback, sizeof(fallback),
                  "%s (0x%X)", failureText.c_str(), daemonRc);
    screen.requestToast(fallback);
    return false;
}

void SystemTab::openManualDateTimeEditor(SettingsScreen& screen) {
    auto& i18n = nxui::I18n::instance();
    TabbedOverlayScreen::DateTimeEditorValue initial;
    if (!currentDateTimeValue(initial)) {
        screen.requestToast(i18n.tr(
            "settings.system.time_change_failed",
            "The date and time setting could not be changed."));
        return;
    }
    screen.requestDateTimeEditor(
        initial, [&screen](const auto& value) {
            return setManualDateTime(screen, value);
        });
}

void SystemTab::refreshTimezoneRowDescription(SettingsScreen& screen, const std::string& zoneName) {
    if (screen.m_tabIndex < 0 || screen.m_tabIndex >= (int)screen.m_tabs.size())
        return;
    auto& items = screen.m_tabs[screen.m_tabIndex].items;
    for (auto& item : items) {
        if (item.label != timezoneLabel())
            continue;
        item.description = zoneName.empty()
            ? nxui::I18n::instance().tr("common.na", "N/A")
            : zoneName;
        break;
    }
}

void SystemTab::beginTimezoneChange(SettingsScreen& screen, const std::string& filter) {
    auto& i18n = nxui::I18n::instance();
    TimeLocationName current{};
    std::string currentName;
    if (R_SUCCEEDED(timeGetDeviceLocationName(&current)))
        currentName = current.name;

    std::vector<std::string> zones;
    {
        auto all = loadAllTimezoneNames();
        zones.reserve(all.size());
        for (const auto& zone : all) {
            if (containsInsensitive(zone, filter))
                zones.push_back(zone);
        }
    }
    if (zones.empty()) {
        screen.requestToast(i18n.tr(
            "settings.system.timezone_none",
            "No matching timezones."));
        return;
    }

    int selected = 0;
    for (int i = 0; i < (int)zones.size(); ++i) {
        if (zones[i] == currentName) {
            selected = i;
            break;
        }
    }

    screen.requestTimezonePicker(
        zones,
        selected,
        [&screen](const std::string& chosen) {
            auto& i18nInner = nxui::I18n::instance();
            TimeLocationName name{};
            std::snprintf(name.name, sizeof(name.name), "%s", chosen.c_str());
            const Result rc = timeSetDeviceLocationName(&name);
            DebugLog::log("[settings-time] set timezone '%s' rc=0x%X",
                          chosen.c_str(), rc);
            if (R_FAILED(rc)) {
                screen.requestToast(i18nInner.tr(
                    "settings.system.timezone_failed",
                    "Could not change timezone."));
                return;
            }
            SystemTab::refreshTimezoneRowDescription(screen, chosen);
            screen.requestToast(i18nInner.tr(
                "settings.system.timezone_saved",
                "Timezone updated."));
            if (screen.m_webClockSyncEnabled)
                screen.startWebClockSync(false);
        },
        [&screen]() {
            auto& i18nInner = nxui::I18n::instance();
            screen.requestTextEntry(
                i18nInner.tr("settings.system.timezone_filter_title", "Filter timezones"),
                i18nInner.tr(
                    "settings.system.timezone_filter_guide",
                    "Type to filter (e.g. New_York). Leave empty for all."),
                std::string(),
                36,
                [&screen](const std::string& nextFilter) {
                    SystemTab::beginTimezoneChange(screen, nextFilter);
                });
        });
}


} // namespace settings::tabs


SettingsScreen::Tab settings::tabs::SystemTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.system", "System");

    {
        SetSysFirmwareVersion fw{};
        SettingItem it; it.label = i18n.tr("settings.system.firmware", "Firmware Version"); it.type = ItemType::Info;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            it.infoText = fw.display_version;
        else
            it.infoText = i18n.tr("common.na", "N/A");
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.system.custom_firmware", "Custom Firmware"); it.type = ItemType::Info;
        u64 cfg = 0;
        bool isAtmos = R_SUCCEEDED(splGetConfig((SplConfigItem)65000, &cfg));
        if (isAtmos) {
            unsigned major = (cfg >> 56) & 0xFF;
            unsigned minor = (cfg >> 48) & 0xFF;
            unsigned micro = (cfg >> 40) & 0xFF;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Atmosphere %u.%u.%u", major, minor, micro);
            it.infoText = buf;
        } else {
            it.infoText = i18n.tr("settings.system.not_detected", "Not detected");
        }
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.system.emunand", "EmuNAND"); it.type = ItemType::Info;
        u64 cfg = 0;
        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65007, &cfg)) && cfg != 0)
            it.infoText = i18n.tr("common.active", "Active");
        else
            it.infoText = i18n.tr("settings.system.inactive_sysnand", "Inactive / SysNAND");
        t.items.push_back(std::move(it));
    }

    {
        SetSysDeviceNickName nick{};
        SettingItem it; it.label = i18n.tr("settings.system.console_nickname", "Console Nickname"); it.type = ItemType::Info;
        if (R_SUCCEEDED(setsysGetDeviceNickname(&nick)))
            it.infoText = nick.nickname;
        else
            it.infoText = i18n.tr("common.na", "N/A");
        t.items.push_back(std::move(it));
    }

    {
        SetSysSerialNumber sn{};
        SettingItem it; it.label = i18n.tr("settings.system.serial_number", "Serial Number"); it.type = ItemType::Info;
        if (R_SUCCEEDED(setsysGetSerialNumber(&sn)))
            it.infoText = sn.number;
        else
            it.infoText = i18n.tr("common.na", "N/A");
        t.items.push_back(std::move(it));
    }

    {
        TimeLocationName tz{};
        SettingItem it;
        it.label = timezoneLabel();
        it.type = ItemType::Action;
        it.buttonLabel = i18n.tr("settings.system.timezone_button", "Change");
        if (R_SUCCEEDED(timeGetDeviceLocationName(&tz)))
            it.description = tz.name;
        else
            it.description = i18n.tr("common.na", "N/A");
        it.onChange = [&screen](SettingItem&) {
            SystemTab::beginTimezoneChange(screen);
        };
        t.items.push_back(std::move(it));
    }

    {
        bool automatic = true;
        const Result stateResult =
            setsysIsUserSystemClockAutomaticCorrectionEnabled(&automatic);
        if (screen.m_webClockSyncEnabled && R_SUCCEEDED(stateResult) && automatic) {
            const Result rc = switchu::menu::smi_cmd::setInternetTimeSync(false);
            DebugLog::log(
                "[settings-time] public preferred; disable nintendo on build rc=0x%X",
                rc);
            automatic = false;
        }
        SettingItem it;
        it.label = nintendoSyncLabel();
        it.description = i18n.tr(
            "settings.system.internet_time_desc",
            "Set clock via Nintendo (fails with DNS blocking)");
        it.type = ItemType::Toggle;
        it.boolVal = R_SUCCEEDED(stateResult) ? automatic : false;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        DebugLog::log(
            "[settings-time] initial automaticCorrection rc=0x%X enabled=%d shown=%d",
            stateResult,
            automatic ? 1 : 0,
            it.boolVal ? 1 : 0);
        it.onChange = [&screen](SettingItem& self) {
            const Result rc =
                switchu::menu::smi_cmd::setInternetTimeSync(self.boolVal);
            DebugLog::log(
                "[settings-time] daemon SetInternetTimeSync enabled=%d rc=0x%X",
                self.boolVal ? 1 : 0,
                rc);
            if (R_FAILED(rc)) {
                self.boolVal = !self.boolVal;
                screen.requestToast(nxui::I18n::instance().tr(
                    "settings.system.time_change_failed",
                    "The date and time setting could not be changed."));
                return;
            }
            if (self.boolVal && screen.m_webClockSyncEnabled) {
                screen.m_webClockSyncEnabled = false;
                SystemTab::setToggleByLabel(screen, publicSyncLabel(), false);
                if (screen.m_webClockSyncCb)
                    screen.m_webClockSyncCb(false);
            }
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = publicSyncLabel();
        it.description = i18n.tr(
            "settings.system.web_time_desc",
            "Set clock via public HTTPS (works with DNS Blocking)");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_webClockSyncEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_webClockSyncEnabled = self.boolVal;
            if (screen.m_webClockSyncCb)
                screen.m_webClockSyncCb(self.boolVal);

            if (!self.boolVal)
                return;

            bool automatic = false;
            if (R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&automatic))
                && automatic) {
                const Result rc = switchu::menu::smi_cmd::setInternetTimeSync(false);
                DebugLog::log(
                    "[settings-time] public sync disabled nintendo rc=0x%X", rc);
                if (R_FAILED(rc)) {
                    self.boolVal = false;
                    screen.m_webClockSyncEnabled = false;
                    if (screen.m_webClockSyncCb)
                        screen.m_webClockSyncCb(false);
                    screen.requestToast(nxui::I18n::instance().tr(
                        "settings.system.time_change_failed",
                        "The date and time setting could not be changed."));
                    return;
                }
                SystemTab::setToggleByLabel(screen, nintendoSyncLabel(), false);
            }
            screen.startWebClockSync(false);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.system.manual_time", "Set Date and Time");
        it.description = i18n.tr(
            "settings.system.manual_time_desc",
            "Set the clock yourself. Disables sync if it is on.");
        it.type = ItemType::Action;
        it.buttonLabel = i18n.tr("settings.system.manual_time_button", "Change");
        it.onChange = [&screen](SettingItem&) {
            auto& i18nInner = nxui::I18n::instance();
            if (!SystemTab::anyClockSyncEnabled(screen)) {
                SystemTab::openManualDateTimeEditor(screen);
                return;
            }
            screen.requestDialog(
                i18nInner.tr(
                    "settings.system.manual_time_disable_sync_title",
                    "Disable clock sync?"),
                i18nInner.tr(
                    "settings.system.manual_time_disable_sync_msg",
                    "This will disable Sync. Continue?"),
                {
                    { i18nInner.tr("button.ok", "OK"), [&screen]() {
                        SystemTab::disableAllClockSync(screen);
                        SystemTab::openManualDateTimeEditor(screen);
                    } },
                    { i18nInner.tr("button.cancel", "Cancel"), []() {} }
                });
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.system.clock_12h", "12-Hour Clock");
        it.description = i18n.tr("settings.system.clock_12h_desc", "Show the home clock with AM and PM.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_clockUse12Hour;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_clockUse12Hour = self.boolVal;
            if (screen.m_clockUse12HourCb)
                screen.m_clockUse12HourCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.system.usb30", "USB 3.0"); it.type = ItemType::Toggle;
        it.description = i18n.tr(
            "settings.system.usb30_desc",
            "Faster USB when the PC mounts the SD over MTP (plug in while HOME is open). "
            "Takes effect after a restart.");
        bool val = false;
        setsysGetUsb30EnableFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [&screen, &i18n](SettingItem& self) {
            setsysSetUsb30EnableFlag(self.boolVal);
            screen.requestToast(i18n.tr("settings.system.usb30_restart",
                                        "USB 3.0 changes take effect after a restart."),
                                3.0f);
        };
        t.items.push_back(std::move(it));
    }

    {
        SetBatteryLot lot{};
        SettingItem it; it.label = i18n.tr("settings.system.battery_lot", "Battery Lot"); it.type = ItemType::Info;
        if (R_SUCCEEDED(setsysGetBatteryLot(&lot)))
            it.infoText = lot.lot;
        else
            it.infoText = i18n.tr("common.na", "N/A");
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.system.ui_language", "UI Language"); it.type = ItemType::Selector;

        std::vector<std::string> tags = nxui::I18n::supportedLanguageTags();
        it.options.reserve(tags.size());
        for (const auto& tag : tags) {
            if (tag == "auto") it.options.push_back(i18n.tr("common.auto", "Auto"));
            else it.options.push_back(i18n.tr(std::string("languages.") + tag, tag));
        }

        auto itTag = std::find(tags.begin(), tags.end(), screen.m_uiLanguageOverride);
        it.intVal = (itTag != tags.end()) ? (int)std::distance(tags.begin(), itTag) : 0;

        it.onChange = [&screen, tags = std::move(tags)](SettingItem& self) {
            int idx = std::clamp(self.intVal, 0, std::max(0, (int)tags.size() - 1));
            const std::string& selected = tags[idx];
            screen.m_uiLanguageOverride = selected;
            if (screen.m_uiLanguageCb) screen.m_uiLanguageCb(selected);
        };

        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.system.console_language", "Console Language"); it.type = ItemType::Info;
        u64 langCode = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&langCode))) {
            SetLanguage lang = SetLanguage_ENUS;
            if (R_SUCCEEDED(setMakeLanguage(langCode, &lang))) {
                std::string tag = nxui::I18n::detectSystemLanguageTag();
                it.infoText = i18n.tr(std::string("languages.") + tag, tag);
            }
        }
        if (it.infoText.empty()) it.infoText = i18n.tr("common.na", "N/A");
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.system.region", "Region"); it.type = ItemType::Selector;
        it.options = {
            i18n.tr("settings.system.region_japan", "Japan"),
            i18n.tr("settings.system.region_usa", "USA"),
            i18n.tr("settings.system.region_europe", "Europe"),
            i18n.tr("settings.system.region_australia", "Australia"),
            i18n.tr("settings.system.region_hong_kong", "Hong Kong"),
            i18n.tr("settings.system.region_taiwan", "Taiwan"),
            i18n.tr("settings.system.region_south_korea", "South Korea")
        };
        SetRegion reg = SetRegion_JPN;
        if (R_SUCCEEDED(setGetRegionCode(&reg)))
            it.intVal = (int)reg;
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.system.auto_update", "Auto Update"); it.type = ItemType::Toggle;
        bool val = true;
        setsysGetAutoUpdateEnableFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetAutoUpdateEnableFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.system.error_reporting", "Error Reporting"); it.type = ItemType::Toggle;
        bool val = false;
        setsysGetConsoleInformationUploadFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetConsoleInformationUploadFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
