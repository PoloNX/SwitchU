#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>

#ifndef SWITCHU_VERSION
#define SWITCHU_VERSION "unknown"
#endif

SettingsScreen::Tab settings::tabs::AboutTab::build(SettingsScreen& /* screen */) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.about", "About");

    // Application info
    {
        SettingItem it;
        it.label = i18n.tr("settings.about.version", "Version");
        it.type  = ItemType::Info;
        it.infoText = "SwitchU " SWITCHU_VERSION;
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.about.author", "Author");
        it.type  = ItemType::Info;
        it.infoText = "PoloNX";
        t.items.push_back(std::move(it));
    }

    // License
    {
        SettingItem it;
        it.label = i18n.tr("settings.about.license", "License");
        it.type  = ItemType::Info;
        it.infoText = i18n.tr("settings.about.license_value", "GPL-2.0");
        t.items.push_back(std::move(it));
    }

    // Source code
    {
        SettingItem it;
        it.label = i18n.tr("settings.about.source_code", "Source Code");
        it.type  = ItemType::Info;
        it.infoText = i18n.tr("settings.about.source_code_value", "github.com/PoloNX/SwitchU");
        t.items.push_back(std::move(it));
    }

    // Acknowledgements
    {
        SettingItem it;
        it.label = i18n.tr("settings.about.acknowledgements", "Acknowledgements");
        it.type  = ItemType::Section;
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.about.acknowledgements_desc",
                           "Special thanks to everyone who contributed to this project.");
        it.type  = ItemType::Info;
        it.infoText = "";
        t.items.push_back(std::move(it));
    }

    return t;
}
