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

} // namespace themeshop::tabs
