#include "SettingsScreen.hpp"
#include "tabs/TabBuilders.hpp"
#include "core/DebugLog.hpp"

void SettingsScreen::buildTabs() {
    DebugLog::log("[settings] buildTabs() start");
    m_tabs.clear();
    DebugLog::log("[settings]   SystemTab...");
    m_tabs.push_back(settings::tabs::SystemTab::build(*this));
    DebugLog::log("[settings]   AccessibilityTab...");
    m_tabs.push_back(settings::tabs::AccessibilityTab::build(*this));
    DebugLog::log("[settings]   StorageTab...");
    m_tabs.push_back(settings::tabs::StorageTab::build(*this));
    DebugLog::log("[settings]   AudioTab...");
    m_tabs.push_back(settings::tabs::AudioTab::build(*this));
    DebugLog::log("[settings]   DisplayTab...");
    m_tabs.push_back(settings::tabs::DisplayTab::build(*this));
    DebugLog::log("[settings]   InternetTab...");
    m_tabs.push_back(settings::tabs::InternetTab::build(*this));
    DebugLog::log("[settings]   ControllersTab...");
    m_tabs.push_back(settings::tabs::ControllersTab::build(*this));
    DebugLog::log("[settings]   BluetoothTab...");
    m_tabs.push_back(settings::tabs::BluetoothTab::build(*this));
    DebugLog::log("[settings]   SleepTab...");
    m_tabs.push_back(settings::tabs::SleepTab::build(*this));
    DebugLog::log("[settings]   AboutTab...");
    m_tabs.push_back(settings::tabs::AboutTab::build(*this));
    DebugLog::log("[settings] buildTabs() done (%d tabs)", (int)m_tabs.size());

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());

    if (m_tabBar) rebuildTabBar();
    if (m_tabContent) rebuildContentItems();
}
