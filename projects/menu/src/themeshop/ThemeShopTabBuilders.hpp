#pragma once

#include "ThemeShopScreen.hpp"

namespace themeshop::tabs {

class InstalledTab {
public:
    static ThemeShopScreen::Tab build(ThemeShopScreen& screen);
};

class CommunityTab {
public:
    static ThemeShopScreen::Tab build(ThemeShopScreen& screen);
};

class IconsTab {
public:
    static ThemeShopScreen::Tab build(ThemeShopScreen& screen);
};

class SoundsTab {
public:
    static ThemeShopScreen::Tab build(ThemeShopScreen& screen);
};

class InterfaceTab {
public:
    static ThemeShopScreen::Tab build(ThemeShopScreen& screen);
};

class LockScreenTab {
public:
    static ThemeShopScreen::Tab build(ThemeShopScreen& screen);

private:
    static const std::vector<switchu::lockfonts::Entry>& fontEntries(ThemeShopScreen& screen);
    static void syncClockFontSelector(ThemeShopScreen& screen,
                                      ThemeShopScreen::SettingItem& item);
};

} // namespace themeshop::tabs
