#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>

namespace {

bool readUsbFullKeyEnabled() {
    bool val = false;
    if (R_SUCCEEDED(hidsysIsUsbFullKeyControllerEnabled(&val)))
        return val;
    if (R_SUCCEEDED(setsysGetUsbFullKeyEnableFlag(&val)))
        return val;
    return false;
}

void writeUsbFullKeyEnabled(bool enabled) {
    // Persist (stock Controllers setting) and apply live for USB / 2.4 GHz dongles.
    setsysSetUsbFullKeyEnableFlag(enabled);
    hidsysEnableUsbFullKeyController(enabled);
}

} // namespace

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
        // Stock HOME Controllers page name — needed for Pro / 8BitDo 2.4 GHz dongles (#215).
        SettingItem it;
        it.label = i18n.tr(
            "settings.controllers.usb_wired",
            "Pro Controller Wired Communication");
        it.description = i18n.tr(
            "settings.controllers.usb_wired_desc",
            "Allow Pro Controllers over USB or a 2.4 GHz dongle.");
        it.type = ItemType::Toggle;
        const bool val = readUsbFullKeyEnabled();
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            writeUsbFullKeyEnabled(self.boolVal);
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
