#include "ThemeShopScreen.hpp"
#include "ThemeShopTabBuilders.hpp"
#include "core/DebugLog.hpp"

void ThemeShopScreen::buildTabs() {
    DebugLog::log("[themeshop] buildTabs() start");
    // Replace, don't append — refreshTranslations / rebuild can call this again.
    // Also size the settings-row widget cache: Themes/Icons are custom-drawn, but
    // Sounds/Interface use m_cachedTabContentWidgets. setTheme() often runs while
    // m_tabs is still empty (cache resized to 0); Icons → Icon Shop → Down into
    // Sounds then indexed a null cache (Data Abort at null+0x30).
    m_tabs.clear();
    DebugLog::log("[themeshop]   ThemesTab...");
    m_tabs.push_back(themeshop::tabs::InstalledTab::build(*this));
    DebugLog::log("[themeshop]   IconsTab...");
    m_tabs.push_back(themeshop::tabs::IconsTab::build(*this));
    DebugLog::log("[themeshop]   SoundsTab...");
    m_tabs.push_back(themeshop::tabs::SoundsTab::build(*this));
    DebugLog::log("[themeshop]   InterfaceTab...");
    m_tabs.push_back(themeshop::tabs::InterfaceTab::build(*this));
    DebugLog::log("[themeshop]   LockScreenTab...");
    m_tabs.push_back(themeshop::tabs::LockScreenTab::build(*this));
    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());
    DebugLog::log("[themeshop] buildTabs() done (%d tabs)", (int)m_tabs.size());
}
