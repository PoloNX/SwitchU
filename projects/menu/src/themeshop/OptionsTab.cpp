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

    // Theme Shop Options stays theme/icon-adjacent only. General music volume,
    // HOME grid, cursor, and page-transition prefs live in Settings.

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
        it.label = i18n.tr("themeshop.options.theme_music_override",
                           "Themes replace my playlist");
        it.description = i18n.tr(
            "themeshop.options.theme_music_override_desc",
            "On: applying a theme with music swaps your playlist for the theme's tracks. Off: keep your library and use the options below.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_themeMusicOverride;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_themeMusicOverride = self.boolVal;
            if (screen.m_themeMusicOverrideCb)
                screen.m_themeMusicOverrideCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.theme_music_add",
                           "Add theme music to my playlist");
        it.description = i18n.tr(
            "themeshop.options.theme_music_add_desc",
            "When replace is off, append the theme's tracks to your current playlist instead of ignoring them.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_themeMusicAddToPlaylist;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_themeMusicAddToPlaylist = self.boolVal;
            if (screen.m_themeMusicAddToPlaylistCb)
                screen.m_themeMusicAddToPlaylistCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.theme_music_play",
                           "Play theme music on apply");
        it.description = i18n.tr(
            "themeshop.options.theme_music_play_desc",
            "When replace is off and theme music was added, start that track after Apply. (Replace mode always starts the theme playlist when music is enabled.)");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_themeMusicPlayOnApply;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_themeMusicPlayOnApply = self.boolVal;
            if (screen.m_themeMusicPlayOnApplyCb)
                screen.m_themeMusicPlayOnApplyCb(self.boolVal);
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
