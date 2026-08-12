#include "TabBuilders.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <cstdio>
#include <vector>

namespace {

void rebuildDynamicItems(SettingsScreen::Tab& t, TabbedOverlayScreen& screen) {
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    // Keep the first items (General section header + 2 toggles = indices 0,1,2)
    // Everything after index 2 is dynamic and gets rebuilt
    constexpr int kStaticCount = 3;
    if ((int)t.items.size() > kStaticCount)
        t.items.erase(t.items.begin() + kStaticCount, t.items.end());

    // Paired devices section
    {
        SettingItem sec;
        sec.label = i18n.tr("settings.bluetooth.paired_devices", "Paired Devices");
        sec.type = ItemType::Section;
        t.items.push_back(std::move(sec));
    }

    if (bluetooth::IsAvailable()) {
        auto connected = bluetooth::GetConnectedAudioDevice();
        auto paired = bluetooth::ListPairedAudioDevices();
        if (paired.empty()) {
            SettingItem it;
            it.label = i18n.tr("settings.bluetooth.no_paired_devices", "No paired devices");
            it.type = ItemType::Info;
            t.items.push_back(std::move(it));
        } else {
            for (auto& device : paired) {
                SettingItem it;
                const bool isConnected = bluetooth::IsDeviceValid(connected)
                    && bluetooth::AddressesEqual(connected.addr, device.addr);
                it.label = bluetooth::DeviceName(device);
                it.description = isConnected
                    ? i18n.tr("settings.bluetooth.connected", "Connected")
                    : i18n.tr("settings.bluetooth.not_connected", "Not connected");
                it.type = ItemType::Action;
                auto dev = device;
                it.onChange = [dev, isConnected, &screen](SettingItem&) {
                    auto& i = nxui::I18n::instance();
                    std::vector<TabbedOverlayScreen::DialogButtonDef> buttons;
                    if (isConnected) {
                        buttons.push_back({i.tr("settings.bluetooth.disconnect", "Disconnect"), [dev]() {
                            bluetooth::DisconnectAudioDevice(dev);
                        }});
                    } else {
                        buttons.push_back({i.tr("settings.bluetooth.connect", "Connect"), [dev]() {
                            bluetooth::ConnectAudioDevice(dev);
                        }});
                    }
                    buttons.push_back({i.tr("settings.bluetooth.unpair", "Unpair"), [dev]() {
                        bluetooth::UnpairAudioDevice(dev);
                    }});
                    buttons.push_back({i.tr("button.cancel", "Cancel"), []() {}});
                    screen.requestDialog(
                        i.tr("settings.bluetooth.device_actions", "Device Actions"),
                        bluetooth::DeviceName(dev),
                        std::move(buttons)
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
                it.label = bluetooth::DeviceName(device);
                it.type = ItemType::Action;
                auto dev = device;
                it.onChange = [dev, &screen](SettingItem&) {
                    auto& i = nxui::I18n::instance();
                    screen.requestDialog(
                        i.tr("settings.bluetooth.device_actions", "Device Actions"),
                        bluetooth::DeviceName(dev),
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
        bool val = true;
        setsysGetBluetoothEnableFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetBluetoothEnableFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.bluetooth.afh", "Bluetooth AFH");
        it.type = ItemType::Toggle;
        bool val = true;
        setsysGetBluetoothAfhEnableFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetBluetoothAfhEnableFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    // Build dynamic items (connected, paired, discovered)
    rebuildDynamicItems(t, screen);

    // Poll for BT changes every frame while this tab is active
    t.onUpdate = [](Tab& self, TabbedOverlayScreen& scr) {
        bool changed = false;
        if (bluetooth::IsAvailable()) {
            changed |= bluetooth::HasPairedChanges();
            changed |= bluetooth::HasConnectedChanges();
            changed |= bluetooth::HasDiscoveredChanges();
        }
        if (changed) {
            rebuildDynamicItems(self, scr);
            scr.refreshCurrentTabWidgets();
        }
    };

    return t;
}
