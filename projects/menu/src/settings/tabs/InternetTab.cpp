#include "TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <cstdio>
#include <cstring>

static std::string settings_ipToString(u32 addr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                  addr & 0xFF, (addr >> 8) & 0xFF,
                  (addr >> 16) & 0xFF, (addr >> 24) & 0xFF);
    return buf;
}

static std::string settings_macToString(const u8* mac) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

SettingsScreen::Tab settings::tabs::InternetTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.internet", "Internet");

    u32 ip = 0;
    std::string ssid;
    bool nifmOk = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    DebugLog::log("[internet] nifmInit: %s", nifmOk ? "OK" : "FAIL");
    if (nifmOk) {
        nifmGetCurrentIpAddress(&ip);

        NifmNetworkProfileData prof{};
        if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&prof))) {
            char ssidBuf[33]{};
            std::memcpy(ssidBuf, prof.wireless_setting_data.ssid, 32);
            ssid = ssidBuf;
        }

        nifmExit();
    }

    {
        SettingItem it; it.label = i18n.tr("settings.internet.wifi_ssid", "WiFi Network"); it.type = ItemType::Action;
        it.buttonLabel = i18n.tr("button.connect", "Connect");
        it.description = !ssid.empty() ? ssid : i18n.tr("settings.internet.not_connected", "Not connected");
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_netConnectCb) screen.m_netConnectCb();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.internet.ip_address", "IP Address"); it.type = ItemType::Info;
        if (nifmOk && ip != 0)
            it.infoText = settings_ipToString(ip);
        else
            it.infoText = i18n.tr("settings.internet.not_connected", "Not connected");
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.internet.wifi", "WiFi"); it.type = ItemType::Toggle;
        it.description = i18n.tr("settings.internet.wifi_desc", "Enable or disable the wireless LAN radio.");
        bool val = true;
        setsysGetWirelessLanEnableFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetWirelessLanEnableFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.internet.auto_app_download", "Auto App Download"); it.type = ItemType::Toggle;
        it.description = i18n.tr("settings.internet.auto_app_download_desc", "Automatically download updates for installed games.");
        bool val = true;
        setsysGetAutomaticApplicationDownloadFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetAutomaticApplicationDownloadFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.internet.mac_address", "MAC Address"); it.type = ItemType::Info;
        SetCalMacAddress mac{};
        if (R_SUCCEEDED(setcalInitialize())) {
            if (R_SUCCEEDED(setcalGetWirelessLanMacAddress(&mac)))
                it.infoText = settings_macToString(mac.addr);
            setcalExit();
        }
        if (it.infoText.empty())
            it.infoText = i18n.tr("common.na", "N/A");
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.internet.dns", "DNS"); it.type = ItemType::Info;
        it.infoText = i18n.tr("settings.internet.dns_auto", "Auto (DHCP)");
        t.items.push_back(std::move(it));
    }

    return t;
}
