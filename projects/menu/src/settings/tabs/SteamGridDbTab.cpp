#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>

SettingsScreen::Tab settings::tabs::SteamGridDbTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.steamgriddb", "SteamGridDB");

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.steamgriddb.section", "Game artwork");
    tab.items.push_back(std::move(section));

    SettingItem enabled;
    enabled.type = ItemType::Toggle;
    enabled.label = i18n.tr("settings.steamgriddb.enabled", "Display SteamGridDB artwork");
    enabled.description = i18n.tr("settings.steamgriddb.enabled_desc",
        "Where artwork appears.");
    enabled.boolVal = screen.m_steamGridDbEnabled;
    enabled.anim01 = enabled.boolVal ? 1.f : 0.f;
    enabled.onChange = [&screen](SettingItem& item) {
        screen.m_steamGridDbEnabled = item.boolVal;
        if (screen.m_steamGridDbEnabledCb)
            screen.m_steamGridDbEnabledCb(item.boolVal);
    };
    tab.items.push_back(std::move(enabled));

    auto addViewToggle = [&](const char* labelKey, const char* label,
                             const char* descKey, const char* desc,
                             bool& flag) {
        SettingItem item;
        item.type = ItemType::Toggle;
        item.label = i18n.tr(labelKey, label);
        item.description = i18n.tr(descKey, desc);
        item.boolVal = flag;
        item.anim01 = item.boolVal ? 1.f : 0.f;
        item.indentLevel = 1;
        item.enabled = screen.m_steamGridDbEnabled;
        item.onChange = [&screen, &flag](SettingItem& self) {
            if (!screen.m_steamGridDbEnabled)
                return;
            flag = self.boolVal;
            if (screen.m_steamGridDbViewFlagsCb) {
                screen.m_steamGridDbViewFlagsCb(screen.m_steamGridDbShowInGrid,
                                                screen.m_steamGridDbShowInDynamicLine,
                                                screen.m_steamGridDbShowInFolders);
            }
        };
        tab.items.push_back(std::move(item));
    };

    addViewToggle("settings.steamgriddb.show_grid", "Show on grid menu",
                  "settings.steamgriddb.show_grid_desc",
                  "Behind the grid.",
                  screen.m_steamGridDbShowInGrid);
    addViewToggle("settings.steamgriddb.show_line", "Show on single-row menu",
                  "settings.steamgriddb.show_line_desc",
                  "Behind the carousel.",
                  screen.m_steamGridDbShowInDynamicLine);
    addViewToggle("settings.steamgriddb.show_folders", "Show inside folders",
                  "settings.steamgriddb.show_folders_desc",
                  "Inside open folders.",
                  screen.m_steamGridDbShowInFolders);

    SettingItem poster;
    poster.type = ItemType::Toggle;
    poster.label = i18n.tr("settings.steamgriddb.show_poster_line",
                           "Show Poster artwork in single-row view");
    poster.description = i18n.tr("settings.steamgriddb.show_poster_line_desc",
        "Tall covers on the carousel.");
    poster.boolVal = screen.m_steamGridDbShowPosterInDynamicLine;
    poster.anim01 = poster.boolVal ? 1.f : 0.f;
    poster.indentLevel = 1;
    poster.enabled = screen.m_steamGridDbEnabled;
    poster.onChange = [&screen](SettingItem& self) {
        if (!screen.m_steamGridDbEnabled)
            return;
        screen.m_steamGridDbShowPosterInDynamicLine = self.boolVal;
        if (screen.m_steamGridDbPosterInLineCb)
            screen.m_steamGridDbPosterInLineCb(self.boolVal);
    };
    tab.items.push_back(std::move(poster));

    SettingItem logo;
    logo.type = ItemType::Toggle;
    logo.label = i18n.tr("settings.steamgriddb.show_logo_line",
                         "Show logo in single-row view");
    logo.description = i18n.tr("settings.steamgriddb.show_logo_line_desc",
        "Logo above the carousel.");
    logo.boolVal = screen.m_steamGridDbShowLogoInDynamicLine;
    logo.anim01 = logo.boolVal ? 1.f : 0.f;
    logo.indentLevel = 1;
    logo.enabled = screen.m_steamGridDbEnabled;
    logo.onChange = [&screen](SettingItem& self) {
        if (!screen.m_steamGridDbEnabled)
            return;
        screen.m_steamGridDbShowLogoInDynamicLine = self.boolVal;
        if (screen.m_steamGridDbLogoInLineCb)
            screen.m_steamGridDbLogoInLineCb(self.boolVal);
    };
    tab.items.push_back(std::move(logo));

    SettingItem quality;
    quality.type = ItemType::Selector;
    quality.label = i18n.tr("settings.steamgriddb.hero_quality", "Hero backdrop quality");
    quality.description = i18n.tr("settings.steamgriddb.hero_quality_desc",
        "Adjust hero backdrop for quality/performance.");
    quality.options = {
        i18n.tr("settings.steamgriddb.hero_quality_off", "Off (don't store files)"),
        i18n.tr("settings.steamgriddb.hero_quality_low", "Low (320, 4×)"),
        i18n.tr("settings.steamgriddb.hero_quality_medium", "Medium (640, 2×)"),
        i18n.tr("settings.steamgriddb.hero_quality_high", "High (full 1280)")
    };
    quality.intVal = std::clamp(screen.m_steamGridDbHeroQuality, 0, 3);
    quality.indentLevel = 1;
    quality.enabled = screen.m_steamGridDbEnabled;
    quality.onChange = [&screen](SettingItem& self) {
        if (!screen.m_steamGridDbEnabled)
            return;
        screen.m_steamGridDbHeroQuality = std::clamp(self.intVal, 0, 3);
        if (screen.m_steamGridDbHeroQualityCb)
            screen.m_steamGridDbHeroQualityCb(screen.m_steamGridDbHeroQuality);
    };
    tab.items.push_back(std::move(quality));

    SettingItem blur;
    blur.type = ItemType::Slider;
    blur.label = i18n.tr("settings.steamgriddb.hero_blur", "Hero backdrop blur");
    blur.description = i18n.tr("settings.steamgriddb.hero_blur_desc",
        "Adjust Hero blur (hides low quality well).");
    blur.sliderSteps = 10;
    blur.floatVal = std::clamp(screen.m_steamGridDbHeroBlur, 0, 10) / 10.f;
    blur.anim01 = blur.floatVal;
    blur.indentLevel = 1;
    blur.peekBackground = true;
    blur.enabled = screen.m_steamGridDbEnabled && screen.m_steamGridDbHeroQuality != 0;
    blur.onChange = [&screen](SettingItem& self) {
        if (!screen.m_steamGridDbEnabled)
            return;
        screen.m_steamGridDbHeroBlur = std::clamp(
            static_cast<int>(std::lround(self.floatVal * 10.f)), 0, 10);
        if (screen.m_steamGridDbHeroBlurCb)
            screen.m_steamGridDbHeroBlurCb(screen.m_steamGridDbHeroBlur);
    };
    tab.items.push_back(std::move(blur));

    SettingItem apiKey;
    apiKey.type = ItemType::Action;
    apiKey.label = i18n.tr("settings.steamgriddb.api_key", "API key");
    apiKey.buttonLabel = i18n.tr("button.configure", "Configure");
    apiKey.description = screen.m_steamGridDbHasApiKey
        ? i18n.tr("settings.steamgriddb.api_key_set", "Configured (hidden)")
        : i18n.tr("settings.steamgriddb.api_key_missing", "Not configured");
    apiKey.onChange = [&screen](SettingItem&) {
        if (screen.m_steamGridDbApiKeyCb) screen.m_steamGridDbApiKeyCb();
    };
    tab.items.push_back(std::move(apiKey));

    SettingItem scan;
    scan.type = ItemType::Action;
    scan.label = i18n.tr("settings.steamgriddb.scan", "Search artwork for missing assets");
    scan.buttonLabel = i18n.tr("button.search", "Search");
    scan.description = i18n.tr("settings.steamgriddb.scan_desc",
        "Fill missing hero, logo, and poster.");
    scan.onChange = [&screen](SettingItem&) {
        if (screen.m_steamGridDbRunning) {
            screen.requestToast(nxui::I18n::instance().tr(
                "settings.steamgriddb.already_running", "A scan is already running."));
            return;
        }
        if (!screen.m_steamGridDbHasApiKey) {
            screen.requestToast(nxui::I18n::instance().tr(
                "settings.steamgriddb.need_key", "Configure an API key first."));
            return;
        }
        if (screen.m_steamGridDbScrapeCb) screen.m_steamGridDbScrapeCb();
    };
    tab.items.push_back(std::move(scan));

    tab.onUpdate = [](SettingsScreen::Tab& current, TabbedOverlayScreen& base) {
        auto& owner = static_cast<SettingsScreen&>(base);
        // Section, master, 3 view toggles, poster, logo, quality, blur, API key, scan
        if (current.items.size() < 11) return;

        for (int i = 2; i <= 8; ++i) {
            current.items[static_cast<std::size_t>(i)].enabled = owner.m_steamGridDbEnabled;
            current.items[static_cast<std::size_t>(i)].indentLevel = 1;
        }
        // Blur only applies when a hero canvas is actually decoded.
        current.items[8].enabled = owner.m_steamGridDbEnabled
            && owner.m_steamGridDbHeroQuality != 0;

        auto& key = current.items[9];
        key.description = owner.m_steamGridDbHasApiKey
            ? nxui::I18n::instance().tr("settings.steamgriddb.api_key_set", "Configured (hidden)")
            : nxui::I18n::instance().tr("settings.steamgriddb.api_key_missing", "Not configured");
        (void)base;
    };

    return tab;
}
