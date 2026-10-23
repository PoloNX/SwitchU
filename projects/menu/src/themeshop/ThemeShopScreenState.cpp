#include "ThemeShopScreen.hpp"
#include "ThemeShopTabBuilders.hpp"
#include "core/DebugLog.hpp"

void ThemeShopScreen::buildTabs() {
    DebugLog::log("[themeshop] buildTabs() start");
    m_tabs.clear();
    DebugLog::log("[themeshop]   InstalledTab...");
    m_tabs.push_back(themeshop::tabs::InstalledTab::build(*this));
    DebugLog::log("[themeshop]   CommunityTab...");
    m_tabs.push_back(themeshop::tabs::CommunityTab::build(*this));
    DebugLog::log("[themeshop]   IconsTab...");
    m_tabs.push_back(themeshop::tabs::IconsTab::build(*this));
    DebugLog::log("[themeshop]   OptionsTab...");
    m_tabs.push_back(themeshop::tabs::OptionsTab::build(*this));
    DebugLog::log("[themeshop] buildTabs() done (%d tabs)", (int)m_tabs.size());

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());

    if (m_tabBar) rebuildTabBar();
    if (m_tabContent) rebuildContentItems();
}
