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
            "Show Lock on boot, before sleep, and after wake. "
            "Unlock returns to your last HOME layout. "
            "Also unlocks Power → Lock in Quick Settings and the HOME power menu. "
            "The console Power button still sleeps (Horizon); Lock covers the handoff.");
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
            "Typeface for the Lock Screen clock. Theme follows the active theme's regular font. "
            "Fonts bundled with installed themes appear as Name (Theme).");
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
            "On: applying a theme that includes fonts updates the Lock clock face. "
            "Off: keep your Clock Font choice.");
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

    t.onUpdate = [&screen](Tab& tab, TabbedOverlayScreen&) {
        if (tab.items.size() < 2)
            return;
        tab.items[0].boolVal = screen.m_lockScreenEnabled;
        tab.items[0].anim01 = screen.m_lockScreenEnabled ? 1.f : 0.f;
        syncClockFontSelector(screen, tab.items[1]);
        if (tab.items.size() >= 3) {
            tab.items[2].boolVal = screen.m_themeLockClockFontOverride;
            tab.items[2].anim01 = screen.m_themeLockClockFontOverride ? 1.f : 0.f;
        }
    };

    return t;
}
