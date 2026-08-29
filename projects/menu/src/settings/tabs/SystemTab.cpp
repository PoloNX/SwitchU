#include "TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include "smi_commands.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string uidToHex(const AccountUid& uid) {
    char buf[33] = {};
    std::snprintf(buf, sizeof(buf), "%016llX%016llX",
                  (unsigned long long)uid.uid[0],
                  (unsigned long long)uid.uid[1]);
    return std::string(buf);
}

struct ProfileOption {
    AccountUid uid{};
    std::string uidHex;
    std::string name;
};

std::vector<ProfileOption> listProfileOptions() {
    std::vector<ProfileOption> out;
    AccountUid uids[8] = {};
    s32 count = 0;
    if (R_FAILED(accountListAllUsers(uids, 8, &count)) || count <= 0)
        return out;

    out.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        AccountProfile profile{};
        if (R_FAILED(accountGetProfile(&profile, uids[i])))
            continue;

        AccountProfileBase base{};
        AccountUserData userData{};
        std::string name;
        if (R_SUCCEEDED(accountProfileGet(&profile, &userData, &base)))
            name = base.nickname;
        accountProfileClose(&profile);

        if (name.empty()) {
            char fallback[32] = {};
            std::snprintf(fallback, sizeof(fallback), "User %d", i + 1);
            name = fallback;
        }

        ProfileOption option;
        option.uid = uids[i];
        option.uidHex = uidToHex(uids[i]);
        option.name = std::move(name);
        out.push_back(std::move(option));
    }
    return out;
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

} // namespace

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
        SettingItem it; it.label = i18n.tr("settings.system.timezone", "Timezone"); it.type = ItemType::Info;
        if (R_SUCCEEDED(timeGetDeviceLocationName(&tz)))
            it.infoText = tz.name;
        else
            it.infoText = i18n.tr("common.na", "N/A");
        t.items.push_back(std::move(it));
    }

    {
        bool automatic = true;
        const Result stateResult =
            setsysIsUserSystemClockAutomaticCorrectionEnabled(&automatic);
        SettingItem it;
        it.label = i18n.tr("settings.system.internet_time", "Synchronize Clock via Internet");
        it.description = i18n.tr(
            "settings.system.internet_time_desc",
            "Automatically correct the console clock using network time.");
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
            }
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.system.manual_time", "Set Date and Time");
        it.description = i18n.tr(
            "settings.system.manual_time_desc",
            "Available when Internet clock synchronization is disabled.");
        it.type = ItemType::Action;
        it.buttonLabel = i18n.tr("settings.system.manual_time_button", "Change");
        it.onChange = [&screen](SettingItem&) {
            bool automatic = true;
            const Result stateRc =
                setsysIsUserSystemClockAutomaticCorrectionEnabled(&automatic);
            if (R_FAILED(stateRc) || automatic) {
                screen.requestToast(nxui::I18n::instance().tr(
                    "settings.system.disable_internet_time_first",
                    "Disable Internet clock synchronization first."));
                return;
            }
            TabbedOverlayScreen::DateTimeEditorValue initial;
            if (!currentDateTimeValue(initial)) {
                screen.requestToast(nxui::I18n::instance().tr(
                    "settings.system.time_change_failed",
                    "The date and time setting could not be changed."));
                return;
            }
            screen.requestDateTimeEditor(
                initial, [&screen](const auto& value) {
                    return setManualDateTime(screen, value);
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
        it.description = i18n.tr("settings.system.usb30_desc", "Enable USB 3.0 for faster transfer speeds.");
        bool val = false;
        setsysGetUsb30EnableFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetUsb30EnableFlag(self.boolVal);
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
        auto profiles = listProfileOptions();
        SettingItem it;
        it.label = i18n.tr("settings.system.default_profile", "Default Profile");
        it.description = i18n.tr("settings.system.default_profile_desc",
                                 "Launch games with this profile when possible.");
        it.type = ItemType::Selector;
        it.options.push_back(i18n.tr("settings.system.default_profile_ask", "Ask each time"));
        for (const auto& profile : profiles)
            it.options.push_back(profile.name);

        it.intVal = 0;
        if (!screen.m_defaultProfileUid.empty()) {
            for (int i = 0; i < (int)profiles.size(); ++i) {
                if (profiles[(size_t)i].uidHex == screen.m_defaultProfileUid) {
                    it.intVal = i + 1;
                    break;
                }
            }
        }

        it.onChange = [&screen, profiles = std::move(profiles)](SettingItem& self) {
            int idx = std::clamp(self.intVal, 0, (int)profiles.size());
            screen.m_defaultProfileUid = idx > 0 ? profiles[(size_t)(idx - 1)].uidHex : std::string();
            if (screen.m_defaultProfileCb)
                screen.m_defaultProfileCb(screen.m_defaultProfileUid);
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
