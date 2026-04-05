#pragma once

#include "../SettingsScreen.hpp"

namespace settings::tabs {

class SystemTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class AudioTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class DisplayTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class InternetTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class ControllersTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class BluetoothTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class SleepTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class MusicTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class ThemeTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class StorageTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

class AboutTab {
public:
    static SettingsScreen::Tab build(SettingsScreen& screen);
};

}
