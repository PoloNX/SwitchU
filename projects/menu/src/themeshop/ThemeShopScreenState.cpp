#include "ThemeShopScreen.hpp"
#include "ThemeShopTabBuilders.hpp"
#include "core/DebugLog.hpp"

void ThemeShopScreen::buildTabs() {
    DebugLog::log("[themeshop] buildTabs() start");
    DebugLog::log("[themeshop]   ThemesTab...");
    m_tabs.push_back(themeshop::tabs::InstalledTab::build(*this));
    DebugLog::log("[themeshop]   IconsTab...");
    m_tabs.push_back(themeshop::tabs::IconsTab::build(*this));
    DebugLog::log("[themeshop]   SoundsTab...");
    m_tabs.push_back(themeshop::tabs::SoundsTab::build(*this));
    DebugLog::log("[themeshop]   InterfaceTab...");
    m_tabs.push_back(themeshop::tabs::InterfaceTab::build(*this));
    DebugLog::log("[themeshop] buildTabs() done (%d tabs)", (int)m_tabs.size());
}
