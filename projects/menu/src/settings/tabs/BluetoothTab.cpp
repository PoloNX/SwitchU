#include "TabBuilders.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <cstdio>

namespace {

void rebuildDynamicItems(SettingsScreen::Tab& t, SettingsScreen& screen) {
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    // Keep the first items (General section header + 2 toggles = indices 0,1,2)
    // Everything after index 2 is dynamic and gets rebuilt
    constexpr int kStaticCount = 3;
    if ((int)t.items.size() > kStaticCount)
        t.items.erase(t.items.begin() + kStaticCount, t.items.end());

    // Connected device section
    {
        SettingItem sec;
        sec.label = i18n.tr("settings.bluetooth.connected_device", "Connected Device");
        sec.type = ItemType::Section;
        t.items.push_back(std::move(sec));
    }

    if (bluetooth::IsAvailable()) {
        auto connected = bluetooth::GetConnectedAudioDevice();
        if (bluetooth::IsDeviceValid(connected)) {
            SettingItem it;
            it.label = connected.name;
            it.description = i18n.tr("settings.bluetooth.disconnect", "Disconnect");
            it.type = ItemType::Action;
            auto dev = connected;
            it.onChange = [dev, &screen](SettingItem&) {
                auto& i = nxui::I18n::instance();
                screen.requestDialog(
                    i.tr("settings.bluetooth.device_actions", "Device Actions"),
                    std::string(dev.name),
                    {{i.tr("settings.bluetooth.disconnect", "Disconnect"), [dev]() {
                        bluetooth::DisconnectAudioDevice(dev);
                    }},
                    {i.tr("button.cancel", "Cancel"), []() {}}}
                );
            };
            t.items.push_back(std::move(it));
        } else {
            SettingItem it;
            it.label = i18n.tr("settings.bluetooth.no_device_connected", "No device connected");
            it.type = ItemType::Info;
            t.items.push_back(std::move(it));
        }
    } else {
        SettingItem it;
        it.label = i18n.tr("settings.bluetooth.no_device_connected", "No device connected");
        it.type = ItemType::Info;
        t.items.push_back(std::move(it));
    }

    // Paired devices section
    {
        SettingItem sec;
        sec.label = i18n.tr("settings.bluetooth.paired_devices", "Paired Devices");
        sec.type = ItemType::Section;
        t.items.push_back(std::move(sec));
    }

    if (bluetooth::IsAvailable()) {
        auto paired = bluetooth::ListPairedAudioDevices();
        if (paired.empty()) {
            SettingItem it;
            it.label = i18n.tr("settings.bluetooth.no_paired_devices", "No paired devices");
            it.type = ItemType::Info;
            t.items.push_back(std::move(it));
        } else {
            for (auto& device : paired) {
                SettingItem it;
                it.label = device.name;
                it.type = ItemType::Action;
                auto dev = device;
                it.onChange = [dev, &screen](SettingItem&) {
                    auto& i = nxui::I18n::instance();
                    screen.requestDialog(
                        i.tr("settings.bluetooth.device_actions", "Device Actions"),
                        std::string(dev.name),
                        {{i.tr("settings.bluetooth.connect", "Connect"), [dev]() {
                            bluetooth::ConnectAudioDevice(dev);
                        }},
                        {i.tr("settings.bluetooth.unpair", "Unpair"), [dev]() {
                            bluetooth::UnpairAudioDevice(dev);
                        }},
                        {i.tr("button.cancel", "Cancel"), []() {}}}
                    );
                };
                t.items.push_back(std::move(it));
            }
        }
    }

    // Discover devices section
    {
        SettingItem sec;
        sec.label = i18n.tr("settings.bluetooth.discover_devices", "Discover Devices");
        sec.type = ItemType::Section;
        t.items.push_back(std::move(sec));
    }

    if (bluetooth::IsAvailable()) {
        {
            SettingItem it;
            bool searching = bluetooth::IsDiscovering();
            it.label = searching
                ? i18n.tr("settings.bluetooth.stop_search", "Stop searching")
                : i18n.tr("settings.bluetooth.start_search", "Search for devices...");
            it.type = ItemType::Action;
            it.onChange = [](SettingItem&) {
                if (bluetooth::IsDiscovering())
                    bluetooth::StopDiscovery();
                else
                    bluetooth::StartDiscovery();
            };
            t.items.push_back(std::move(it));
        }

        if (bluetooth::IsDiscovering()) {
            auto discovered = bluetooth::ListDiscoveredAudioDevices();
            for (auto& device : discovered) {
                SettingItem it;
                it.label = device.name;
                it.type = ItemType::Action;
                auto dev = device;
                it.onChange = [dev, &screen](SettingItem&) {
                    auto& i = nxui::I18n::instance();
                    screen.requestDialog(
                        i.tr("settings.bluetooth.device_actions", "Device Actions"),
                        std::string(dev.name),
                        {{i.tr("settings.bluetooth.pair_connect", "Pair & Connect"), [dev]() {
                            bluetooth::ConnectAudioDevice(dev);
                        }},
                        {i.tr("button.cancel", "Cancel"), []() {}}}
                    );
                };
                t.items.push_back(std::move(it));
            }
        }
    }
}

} // anonymous namespace

SettingsScreen::Tab settings::tabs::BluetoothTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.bluetooth", "Bluetooth");

    // General section
    {
        SettingItem sec;
        sec.label = i18n.tr("settings.bluetooth.general", "General");
        sec.type = ItemType::Section;
        t.items.push_back(std::move(sec));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.bluetooth.bluetooth", "Bluetooth");
        it.type = ItemType::Toggle;
        bool val = false;
        if (R_FAILED(setsysGetBluetoothEnableFlag(&val))) {
            DebugLog::log("[bt] setsysGetBluetoothEnableFlag failed");
            val = false;
        }
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            bool desired = self.boolVal;
            Result rc = setsysSetBluetoothEnableFlag(desired);
            if (R_FAILED(rc)) {
                DebugLog::log("[bt] setsysSetBluetoothEnableFlag failed: 0x%x", rc);
                self.boolVal = !desired;
                self.anim01 = self.boolVal ? 1.f : 0.f;
            } else {
                bool actual = false;
                if (R_FAILED(setsysGetBluetoothEnableFlag(&actual))) {
                    DebugLog::log("[bt] setsysGetBluetoothEnableFlag failed after set");
                    actual = desired;
                }
                if (actual != desired) {
                    DebugLog::log("[bt] setsysSetBluetoothEnableFlag mismatch: desired=%d actual=%d", desired, actual);
                    self.boolVal = actual;
                    self.anim01 = actual ? 1.f : 0.f;
                }
            }
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.bluetooth.afh", "Bluetooth AFH");
        it.type = ItemType::Toggle;
        bool val = false;
        if (R_FAILED(setsysGetBluetoothAfhEnableFlag(&val))) {
            DebugLog::log("[bt] setsysGetBluetoothAfhEnableFlag failed");
            val = false;
        }
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            bool desired = self.boolVal;
            Result rc = setsysSetBluetoothAfhEnableFlag(desired);
            if (R_FAILED(rc)) {
                DebugLog::log("[bt] setsysSetBluetoothAfhEnableFlag failed: 0x%x", rc);
                self.boolVal = !desired;
                self.anim01 = self.boolVal ? 1.f : 0.f;
            }
        };
        t.items.push_back(std::move(it));
    }

    // Build dynamic items (connected, paired, discovered)
    rebuildDynamicItems(t, screen);

    // Poll for BT changes every frame while this tab is active
    t.onUpdate = [](Tab& self, SettingsScreen& scr) {
        bool changed = false;
        if (bluetooth::IsAvailable()) {
            changed |= bluetooth::HasPairedChanges();
            changed |= bluetooth::HasConnectedChanges();
            changed |= bluetooth::HasDiscoveredChanges();
        }
        if (changed) {
            rebuildDynamicItems(self, scr);
            scr.rebuildCurrentTab();
        }
    };

    return t;
}
