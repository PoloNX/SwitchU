#include "ThemeShopTabBuilders.hpp"

#include "widgets/LockClockFonts.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>

const std::vector<switchu::lockfonts::Entry>&
themeshop::tabs::LockScreenTab::fontEntries(ThemeShopScreen& screen) {
    if (!screen.m_lockClockFontEntries.empty())
        return screen.m_lockClockFontEntries;
    static const std::vector<switchu::lockfonts::Entry> kFallback =
        switchu::lockfonts::builtinEntries();
    return kFallback;
}

void themeshop::tabs::LockScreenTab::syncClockFontSelector(
    ThemeShopScreen& screen, ThemeShopScreen::SettingItem& item) {
    const auto& entries = fontEntries(screen);
    item.options.clear();
    item.options.reserve(entries.size());
    for (const auto& e : entries)
        item.options.push_back(e.label);

    item.intVal = switchu::lockfonts::indexForId(entries, screen.m_lockScreenClockFont);
    item.previewText = "12:34";
    item.extraHeight = 0.f;
    if (!entries.empty()) {
        const auto& selected = entries[std::clamp(
            item.intVal, 0, std::max(0, static_cast<int>(entries.size()) - 1))];
        item.previewFont = screen.ensureLockClockPreviewFont(selected.id);
    } else {
        item.previewFont = nullptr;
    }
}

void themeshop::tabs::LockScreenTab::syncTransitionCompanion(
    ThemeShopScreen& screen, ThemeShopScreen::SettingItem& item) {
    auto& i18n = nxui::I18n::instance();
    const int mode = std::clamp(screen.m_lockScreenTransition, 0, 3);
    item.indentLevel = 1;
    item.type = ThemeShopScreen::ItemType::Selector;

    if (mode == 2) { // Zoom
        item.hidden = false;
        item.enabled = true;
        item.label = i18n.tr("themeshop.options.lock_zoom_dir", "Zoom Direction");
        item.description = i18n.tr("themeshop.options.lock_zoom_dir_desc",
                                   "Out grows away; In shrinks in.");
        item.options = {
            i18n.tr("themeshop.options.lock_zoom_out", "Out"),
            i18n.tr("themeshop.options.lock_zoom_in", "In"),
        };
        item.intVal = std::clamp(screen.m_lockScreenZoomDir, 0, 1);
        item.onChange = [&screen](ThemeShopScreen::SettingItem& self) {
            screen.m_lockScreenZoomDir = std::clamp(self.intVal, 0, 1);
            if (screen.m_lockScreenZoomDirCb)
                screen.m_lockScreenZoomDirCb(screen.m_lockScreenZoomDir);
        };
    } else if (mode == 3) { // Wipe
        item.hidden = false;
        item.enabled = true;
        item.label = i18n.tr("themeshop.options.lock_wipe_dir", "Wipe Direction");
        item.description = i18n.tr("themeshop.options.lock_wipe_dir_desc",
                                   "Which way Lock exits.");
        item.options = {
            i18n.tr("themeshop.options.lock_wipe_up", "Up"),
            i18n.tr("themeshop.options.lock_wipe_right", "Right"),
            i18n.tr("themeshop.options.lock_wipe_down", "Down"),
            i18n.tr("themeshop.options.lock_wipe_left", "Left"),
        };
        item.intVal = std::clamp(screen.m_lockScreenWipeDir, 0, 3);
        item.onChange = [&screen](ThemeShopScreen::SettingItem& self) {
            screen.m_lockScreenWipeDir = std::clamp(self.intVal, 0, 3);
            if (screen.m_lockScreenWipeDirCb)
                screen.m_lockScreenWipeDirCb(screen.m_lockScreenWipeDir);
        };
    } else {
        // Curtain / Dissolve — no companion; hide the row entirely.
        item.hidden = true;
        item.enabled = false;
        item.label.clear();
        item.description.clear();
        item.options = { "—" };
        item.intVal = 0;
        item.onChange = {};
    }
}

ThemeShopScreen::Tab themeshop::tabs::LockScreenTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.lock_screen", "Lock Screen");

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.lock_screen", "Lock Screen");
        it.description = i18n.tr(
            "themeshop.options.lock_screen_desc",
            "Lock on boot, sleep, and wake.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_lockScreenEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_lockScreenEnabled = self.boolVal;
            if (screen.m_lockScreenCb) screen.m_lockScreenCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.lock_clock_font", "Clock Font");
        it.description = i18n.tr(
            "themeshop.options.lock_clock_font_desc",
            "Typeface for the lock clock.");
        it.type = ItemType::Selector;
        syncClockFontSelector(screen, it);
        it.onChange = [&screen](SettingItem& self) {
            const auto& entries = fontEntries(screen);
            if (entries.empty())
                return;
            const int idx = std::clamp(self.intVal, 0, static_cast<int>(entries.size()) - 1);
            screen.m_lockScreenClockFont = entries[idx].id;
            self.previewFont = screen.ensureLockClockPreviewFont(entries[idx].id);
            if (screen.m_lockScreenClockFontCb)
                screen.m_lockScreenClockFontCb(screen.m_lockScreenClockFont);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.theme_lock_font_override",
                           "Allow themes to override clock font");
        it.description = i18n.tr(
            "themeshop.options.theme_lock_font_override_desc",
            "Themes may update your clock font.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_themeLockClockFontOverride;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_themeLockClockFontOverride = self.boolVal;
            if (screen.m_themeLockClockFontOverrideCb)
                screen.m_themeLockClockFontOverrideCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.lock_transition", "Lock Transition");
        it.description = i18n.tr(
            "themeshop.options.lock_transition_desc",
            "How Lock enters and leaves.");
        it.type = ItemType::Selector;
        it.options = {
            i18n.tr("themeshop.options.lock_transition_curtain", "Curtain"),
            i18n.tr("themeshop.options.lock_transition_dissolve", "Dissolve"),
            i18n.tr("themeshop.options.lock_transition_zoom", "Zoom"),
            i18n.tr("themeshop.options.lock_transition_wipe", "Wipe"),
        };
        it.intVal = std::clamp(screen.m_lockScreenTransition, 0, 3);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_lockScreenTransition = std::clamp(self.intVal, 0, 3);
            if (screen.m_lockScreenTransitionCb)
                screen.m_lockScreenTransitionCb(screen.m_lockScreenTransition);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        syncTransitionCompanion(screen, it);
        t.items.push_back(std::move(it));
    }

    t.onUpdate = [&screen](Tab& tab, TabbedOverlayScreen&) {
        if (tab.items.size() < 5)
            return;
        tab.items[0].boolVal = screen.m_lockScreenEnabled;
        tab.items[0].anim01 = screen.m_lockScreenEnabled ? 1.f : 0.f;
        syncClockFontSelector(screen, tab.items[1]);
        tab.items[2].boolVal = screen.m_themeLockClockFontOverride;
        tab.items[2].anim01 = screen.m_themeLockClockFontOverride ? 1.f : 0.f;
        tab.items[3].intVal = std::clamp(screen.m_lockScreenTransition, 0, 3);
        LockScreenTab::syncTransitionCompanion(screen, tab.items[4]);
    };

    return t;
}
