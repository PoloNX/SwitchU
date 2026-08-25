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
        "Show heroes, covers and logos behind the application menu.");
    enabled.boolVal = screen.m_steamGridDbEnabled;
    enabled.anim01 = enabled.boolVal ? 1.f : 0.f;
    enabled.onChange = [&screen](SettingItem& item) {
        screen.m_steamGridDbEnabled = item.boolVal;
        if (screen.m_steamGridDbEnabledCb)
            screen.m_steamGridDbEnabledCb(item.boolVal);
    };
    tab.items.push_back(std::move(enabled));

    SettingItem apiKey;
    apiKey.type = ItemType::Action;
    apiKey.label = i18n.tr("settings.steamgriddb.api_key", "API key");
    apiKey.description = screen.m_steamGridDbHasApiKey
        ? i18n.tr("settings.steamgriddb.api_key_set", "Configured (hidden)")
        : i18n.tr("settings.steamgriddb.api_key_missing", "Not configured");
    apiKey.onChange = [&screen](SettingItem&) {
        if (screen.m_steamGridDbApiKeyCb) screen.m_steamGridDbApiKeyCb();
    };
    tab.items.push_back(std::move(apiKey));

    SettingItem scan;
    scan.type = ItemType::Action;
    scan.label = i18n.tr("settings.steamgriddb.scan", "Search artwork for all applications");
    scan.description = i18n.tr("settings.steamgriddb.scan_desc",
        "Downloads one cover, hero and logo for every installed application.");
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

    SettingItem progress;
    progress.type = ItemType::Progress;
    progress.label = i18n.tr("settings.steamgriddb.progress", "Artwork scan");
    progress.description = i18n.tr("settings.steamgriddb.idle", "Ready to scan installed applications.");
    progress.floatVal = 0.f;
    tab.items.push_back(std::move(progress));

    SettingItem result;
    result.type = ItemType::Info;
    result.label = i18n.tr("settings.steamgriddb.results", "Results");
    result.infoText = i18n.tr("settings.steamgriddb.no_results", "No scan completed yet");
    tab.items.push_back(std::move(result));

    tab.onUpdate = [](SettingsScreen::Tab& current, TabbedOverlayScreen& base) {
        auto& owner = static_cast<SettingsScreen&>(base);
        if (current.items.size() < 6) return;

        auto& key = current.items[2];
        key.description = owner.m_steamGridDbHasApiKey
            ? nxui::I18n::instance().tr("settings.steamgriddb.api_key_set", "Configured (hidden)")
            : nxui::I18n::instance().tr("settings.steamgriddb.api_key_missing", "Not configured");

        auto& progress = current.items[4];
        if (owner.m_steamGridDbRunning && owner.m_steamGridDbTotal > 0) {
            progress.floatVal = std::clamp(
                static_cast<float>(owner.m_steamGridDbCompleted)
                    / static_cast<float>(owner.m_steamGridDbTotal), 0.f, 1.f);
            progress.description = owner.m_steamGridDbCurrent.empty()
                ? owner.m_steamGridDbMessage : owner.m_steamGridDbCurrent;
        } else if (owner.m_steamGridDbFinished) {
            progress.floatVal = 1.f;
            progress.description = owner.m_steamGridDbMessage;
        } else {
            progress.floatVal = 0.f;
        }

        auto& result = current.items[5];
        if (owner.m_steamGridDbTotal > 0) {
            result.infoText = std::to_string(owner.m_steamGridDbMatched) + " found, "
                            + std::to_string(owner.m_steamGridDbFailed) + " missing";
        }
    };

    return tab;
}
