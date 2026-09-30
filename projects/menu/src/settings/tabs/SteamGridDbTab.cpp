#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>

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
        "Master switch. Nested options below choose where artwork appears.");
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
                  "Hero backdrop behind the standard HOME grid.",
                  screen.m_steamGridDbShowInGrid);
    addViewToggle("settings.steamgriddb.show_line", "Show on single-row menu",
                  "settings.steamgriddb.show_line_desc",
                  "Hero and logo behind the single-row carousel.",
                  screen.m_steamGridDbShowInDynamicLine);
    addViewToggle("settings.steamgriddb.show_folders", "Show inside folders",
                  "settings.steamgriddb.show_folders_desc",
                  "Artwork while browsing games inside an open folder.",
                  screen.m_steamGridDbShowInFolders);

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
        "Downloads a hero and logo only for applications that do not already have artwork. Cancel or press B to stop.");
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
        // Section, master, 3 nested view toggles, API key, scan
        if (current.items.size() < 7) return;

        for (int i = 2; i <= 4; ++i) {
            current.items[static_cast<std::size_t>(i)].enabled = owner.m_steamGridDbEnabled;
            current.items[static_cast<std::size_t>(i)].indentLevel = 1;
        }

        auto& key = current.items[5];
        key.description = owner.m_steamGridDbHasApiKey
            ? nxui::I18n::instance().tr("settings.steamgriddb.api_key_set", "Configured (hidden)")
            : nxui::I18n::instance().tr("settings.steamgriddb.api_key_missing", "Not configured");
        (void)base;
    };

    return tab;
}
