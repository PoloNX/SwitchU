#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>
#include <algorithm>

ThemeShopScreen::Tab themeshop::tabs::SoundsTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.sounds", "Sounds");

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.menu_music", "Menu Music");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_musicEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_musicEnabled = self.boolVal;
            if (screen.m_musicEnabledCb) screen.m_musicEnabledCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.music_volume", "Music Volume");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_musicVolume, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_musicVolume = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_musicVolumeCb) screen.m_musicVolumeCb(screen.m_musicVolume);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.sfx_volume", "SFX Volume");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_sfxVolume, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_sfxVolume = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_sfxVolumeCb) screen.m_sfxVolumeCb(screen.m_sfxVolume);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.theme_music_override",
                           "Allow themes to replace my playlist");
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

    return t;
}
