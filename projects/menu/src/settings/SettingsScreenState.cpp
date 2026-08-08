#include "SettingsScreen.hpp"
#include "tabs/TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include <nxui/core/I18n.hpp>
#include <chrono>

void SettingsScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    m_tabs = {
        {.name = i18n.tr("settings.tabs.system", "System")},
        {.name = i18n.tr("settings.tabs.accessibility", "Accessibility")},
        {.name = i18n.tr("settings.tabs.storage", "Storage")},
        {.name = i18n.tr("settings.tabs.audio", "Audio")},
        {.name = i18n.tr("settings.tabs.display", "Display")},
        {.name = i18n.tr("settings.tabs.internet", "Internet")},
        {.name = i18n.tr("settings.tabs.controllers", "Controllers")},
        {.name = i18n.tr("settings.tabs.bluetooth", "Bluetooth")},
        {.name = i18n.tr("settings.tabs.sleep", "Sleep")},
        {.name = i18n.tr("settings.tabs.about", "About")},
    };
    m_loadedTabs.assign(m_tabs.size(), false);
    m_loadingTabs.assign(m_tabs.size(), false);
    m_tabTasks.clear();
    m_tabTasks.resize(m_tabs.size());
    m_nextPrefetchTab = 0;

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());

    DebugLog::log("[settings] registered %d lazy tabs", (int)m_tabs.size());
}

void SettingsScreen::ensureTabLoaded(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(m_tabs.size()))
        return;
    if (m_loadedTabs.size() != m_tabs.size())
        m_loadedTabs.assign(m_tabs.size(), false);
    if (m_loadingTabs.size() != m_tabs.size())
        m_loadingTabs.assign(m_tabs.size(), false);
    if (m_tabTasks.size() != m_tabs.size())
        m_tabTasks.resize(m_tabs.size());
    if (m_loadedTabs[static_cast<std::size_t>(tabIndex)])
        return;
    if (m_loadingTabs[static_cast<std::size_t>(tabIndex)])
        return;

    if (tabIndex == 7 && !bluetooth::IsAvailable()) {
        bluetooth::Initialize();
        DebugLog::log("[settings] Bluetooth audio manager initialized on demand");
    }

    m_tabs[static_cast<std::size_t>(tabIndex)] = makeLoadingTab(tabIndex);
    startAsyncTabLoad(tabIndex);
}

SettingsScreen::Tab SettingsScreen::buildTabNow(int tabIndex) {
    switch (tabIndex) {
        case 0: return settings::tabs::SystemTab::build(*this);
        case 1: return settings::tabs::AccessibilityTab::build(*this);
        case 2: return settings::tabs::StorageTab::build(*this);
        case 3: return settings::tabs::AudioTab::build(*this);
        case 4: return settings::tabs::DisplayTab::build(*this);
        case 5: return settings::tabs::InternetTab::build(*this);
        case 6: return settings::tabs::ControllersTab::build(*this);
        case 7: return settings::tabs::BluetoothTab::build(*this);
        case 8: return settings::tabs::SleepTab::build(*this);
        case 9: return settings::tabs::AboutTab::build(*this);
        default: break;
    }

    return makeLoadingTab(tabIndex);
}

SettingsScreen::Tab SettingsScreen::makeLoadingTab(int tabIndex) const {
    Tab tab;
    if (tabIndex >= 0 && tabIndex < static_cast<int>(m_tabs.size()))
        tab.name = m_tabs[static_cast<std::size_t>(tabIndex)].name;
    else
        tab.name = nxui::I18n::instance().tr("common.loading", "Loading");

    SettingItem item;
    item.label = nxui::I18n::instance().tr("common.loading", "Loading...");
    item.type = ItemType::Info;
    item.infoText = nxui::I18n::instance().tr("settings.loading_tab", "Loading this section in the background.");
    tab.items.push_back(std::move(item));
    return tab;
}

void SettingsScreen::startAsyncTabLoad(int tabIndex) {
    const std::size_t idx = static_cast<std::size_t>(tabIndex);
    if (idx >= m_tabs.size())
        return;

    DebugLog::log("[settings] starting async tab load %d", tabIndex);
    m_loadingTabs[idx] = true;
    m_tabTasks[idx] = std::async(std::launch::async, [this, tabIndex]() {
        return buildTabNow(tabIndex);
    });
}

void SettingsScreen::pollTabLoaders() {
    bool rebuiltCurrent = false;
    for (std::size_t i = 0; i < m_tabTasks.size(); ++i) {
        if (i >= m_loadingTabs.size() || !m_loadingTabs[i] || !m_tabTasks[i].valid())
            continue;

        if (m_tabTasks[i].wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            continue;

        Tab loaded = m_tabTasks[i].get();
        m_tabs[i] = std::move(loaded);
        if (i < m_loadedTabs.size())
            m_loadedTabs[i] = true;
        m_loadingTabs[i] = false;
        if (i < m_cachedTabContentWidgets.size())
            m_cachedTabContentWidgets[i].clear();

        DebugLog::log("[settings] async tab loaded %d items=%d",
                      (int)i, (int)m_tabs[i].items.size());

        if ((int)i == m_tabIndex && !rebuiltCurrent) {
            clampContentIdx();
            m_contentIdx = 0;
            m_scrollY = 0.f;
            m_scrollTarget = 0.f;
            rebuildContentItems();
            rebuiltCurrent = true;
        }
    }
}

void SettingsScreen::prefetchOneTab() {
    if (m_tabs.empty())
        return;

    for (std::size_t n = 0; n < m_tabs.size(); ++n) {
        std::size_t idx = static_cast<std::size_t>(m_nextPrefetchTab);
        m_nextPrefetchTab = (m_nextPrefetchTab + 1) % static_cast<int>(m_tabs.size());

        if (idx >= m_loadedTabs.size() || idx >= m_loadingTabs.size())
            continue;
        if (m_loadedTabs[idx] || m_loadingTabs[idx])
            continue;

        // Keep the optional btmsys client out of the normal menu/application
        // handoff. Initialize it only when the user explicitly visits the tab.
        if (idx == 7 && idx != static_cast<std::size_t>(m_tabIndex))
            continue;

        if (idx != static_cast<std::size_t>(m_tabIndex))
            m_tabs[idx] = makeLoadingTab(static_cast<int>(idx));
        startAsyncTabLoad(static_cast<int>(idx));
        return;
    }
}

void SettingsScreen::onContentUpdate(float dt) {
    pollTabLoaders();
    if (isActive())
        prefetchOneTab();
    TabbedOverlayScreen::onContentUpdate(dt);
    pollTabLoaders();
}
