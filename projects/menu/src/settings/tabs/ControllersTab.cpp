#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>

SettingsScreen::Tab settings::tabs::ControllersTab::build(SettingsScreen& screen) {
    (void)screen;
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.controllers", "Controllers");

    {
        SettingItem it;
        it.label = i18n.tr("settings.controllers.test", "Test Input Devices");
        it.buttonLabel = i18n.tr("button.test", "Test");
        it.description = i18n.tr(
            "settings.controllers.test_desc",
            "Test joysticks, buttons, and the touch screen.");
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_controllerTestCb)
                screen.m_controllerTestCb();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.controllers.pairing", "Change Grip/Order");
        it.buttonLabel = i18n.tr("button.open", "Open");
        it.description = i18n.tr(
            "settings.controllers.pairing_desc",
            "Pair controllers and change their player order.");
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_controllerPairingCb)
                screen.m_controllerPairingCb();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.controllers.remapping", "Change Button Mapping");
        it.buttonLabel = i18n.tr("button.open", "Open");
        it.description = i18n.tr(
            "settings.controllers.remapping_desc",
            "Open the system controller button mapping tool.");
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_controllerRemappingCb)
                screen.m_controllerRemappingCb();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.controllers.vibration", "Vibration"); it.type = ItemType::Slider;
        float vol = 1.f;
        setsysGetVibrationMasterVolume(&vol);
        it.floatVal = vol;
        it.anim01 = std::clamp(vol, 0.f, 1.f);
        it.onChange = [](SettingItem& self) {
            setsysSetVibrationMasterVolume(self.floatVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.controllers.usb_wired", "USB Wired Communication"); it.type = ItemType::Toggle;
        it.description = i18n.tr("settings.controllers.usb_wired_desc", "Use USB wired mode to reduce latency on compatible controllers.");
        bool val = false;
        setsysGetUsbFullKeyEnableFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetUsbFullKeyEnableFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.controllers.nfc", "NFC (amiibo)"); it.type = ItemType::Toggle;
        bool val = true;
        setsysGetNfcEnableFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetNfcEnableFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
