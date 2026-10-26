#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>

ThemeShopScreen::Tab themeshop::tabs::OptionsTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.options", "Options");

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
        it.label = i18n.tr("settings.display.grid_columns", "Home Grid Columns");
        it.type = ItemType::Slider;
        it.sliderSteps = 7;
        int cols = std::clamp(screen.m_gridColumns, 1, 8);
        it.floatVal = (float)(cols - 1) / 7.f;
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(cols);
        it.onChange = [&screen](SettingItem& self) {
            int cols = std::clamp(1 + (int)std::round(std::clamp(self.floatVal, 0.f, 1.f) * 7.f), 1, 8);
            self.floatVal = (float)(cols - 1) / 7.f;
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(cols);
            screen.m_gridColumns = cols;
            if (screen.m_gridColumnsCb) screen.m_gridColumnsCb(cols);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.grid_rows", "Home Grid Rows");
        it.type = ItemType::Slider;
        it.sliderSteps = 4;
        int rows = std::clamp(screen.m_gridRows, 1, 5);
        it.floatVal = (float)(rows - 1) / 4.f;
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(rows);
        it.onChange = [&screen](SettingItem& self) {
            int rows = std::clamp(1 + (int)std::round(std::clamp(self.floatVal, 0.f, 1.f) * 4.f), 1, 5);
            self.floatVal = (float)(rows - 1) / 4.f;
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(rows);
            screen.m_gridRows = rows;
            if (screen.m_gridRowsCb) screen.m_gridRowsCb(rows);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.sort_large_tiles", "Sort Large Tiles Too");
        it.description = i18n.tr("themeshop.options.sort_large_tiles_desc",
                                 "A-Z and Recent also move applications that use a larger tile size.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_sortLargeTiles;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_sortLargeTiles = self.boolVal;
            if (screen.m_sortLargeTilesCb) screen.m_sortLargeTilesCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

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
        it.label = i18n.tr("themeshop.options.action_hints", "Action Hints");
        it.description = i18n.tr("themeshop.options.action_hints_desc",
                                 "Choose how contextual controls appear in the bottom-right corner.");
        it.type = ItemType::Selector;
        it.options = {
            i18n.tr("themeshop.options.hint_panel", "Panel"),
            i18n.tr("themeshop.options.hint_capsules", "Capsules")
        };
        it.intVal = std::clamp(screen.m_actionHintStyle, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_actionHintStyle = std::clamp(self.intVal, 0, 1);
            if (screen.m_actionHintStyleCb)
                screen.m_actionHintStyleCb(screen.m_actionHintStyle);
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

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.cursor_motion", "Cursor Movement");
        it.type = ItemType::Selector;
        it.description = i18n.tr(
            "settings.display.cursor_motion_desc",
            "Choose whether the selection cursor slides or teleports with a visual pulse.");
        it.options = {
            i18n.tr("settings.display.cursor_translate", "Translate"),
            i18n.tr("settings.display.cursor_teleport", "Teleport")
        };
        it.intVal = std::clamp(screen.m_cursorMotionMode, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_cursorMotionMode = std::clamp(self.intVal, 0, 1);
            if (screen.m_cursorMotionModeCb)
                screen.m_cursorMotionModeCb(screen.m_cursorMotionMode);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.page_transition", "Page Transition");
        it.type = ItemType::Selector;
        it.description = i18n.tr(
            "settings.display.page_transition_desc",
            "Choose whether switching pages slides the grid or swaps it instantly.");
        it.options = {
            i18n.tr("settings.display.page_translate", "Translate"),
            i18n.tr("settings.display.page_teleport", "Teleport")
        };
        it.intVal = std::clamp(screen.m_pageTransitionMode, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_pageTransitionMode = std::clamp(self.intVal, 0, 1);
            if (screen.m_pageTransitionModeCb)
                screen.m_pageTransitionModeCb(screen.m_pageTransitionMode);
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
