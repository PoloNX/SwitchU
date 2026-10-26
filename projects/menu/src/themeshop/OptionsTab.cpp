#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>
#include <algorithm>

ThemeShopScreen::Tab themeshop::tabs::OptionsTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.options", "Options");

    // Theme Shop Options stays theme/icon-adjacent only. Music, HOME grid,
    // cursor, and page-transition prefs live in Settings → Audio / Display.

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.icon_contrast", "Adaptive icon contrast");
        it.description = i18n.tr(
            "themeshop.options.icon_contrast_desc",
            "In single-row with a hero, colour HOME icons follow the interface light/dark chrome. Without a hero they keep authored colours (or your custom tint).");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_iconContrastAdapt;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_iconContrastAdapt = self.boolVal;
            if (screen.m_iconContrastAdaptCb)
                screen.m_iconContrastAdaptCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.auto_theme", "Automatic Theme");
        it.description = screen.m_autoThemeSummary.empty()
            ? i18n.tr("themeshop.options.auto_theme_desc",
                      "Switch between a day theme and a night theme automatically.")
            : screen.m_autoThemeSummary;
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_autoThemeOpenCb)
                screen.m_autoThemeOpenCb();
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
