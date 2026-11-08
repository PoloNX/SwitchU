#include "SettingsScreen.hpp"
#include "tabs/TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include "smi_commands.hpp"
#include "services/ClockService.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "services/SystemTime.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <chrono>
#include <cstdio>

namespace {

bool isRetriableClockSyncError(const std::string& error) {
    return error.find("nifmGetInternetConnectionStatus") != std::string::npos
        || error.find("Internet connection is not ready") != std::string::npos
        || error.find("nifmInitialize") != std::string::npos;
}

} // namespace

void SettingsScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    m_tabs = {
        {.name = i18n.tr("settings.tabs.system", "System")},
        {.name = i18n.tr("settings.tabs.accessibility", "Accessibility")},
        {.name = i18n.tr("settings.tabs.storage", "Storage")},
        {.name = i18n.tr("settings.tabs.audio", "Audio")},
        {.name = i18n.tr("settings.tabs.display", "Display")},
        {.name = i18n.tr("settings.tabs.internet", "Internet")},
        {.name = i18n.tr("settings.tabs.steamgriddb", "SteamGridDB")},
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

    if (tabIndex == 8 && !bluetooth::IsAvailable()) {
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
        case 6: return settings::tabs::SteamGridDbTab::build(*this);
        case 7: return settings::tabs::ControllersTab::build(*this);
        case 8: return settings::tabs::BluetoothTab::build(*this);
        case 9: return settings::tabs::SleepTab::build(*this);
        case 10: return settings::tabs::AboutTab::build(*this);
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

    // DebugLog::log("[settings] starting async tab load %d", tabIndex);
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
        if (idx == 8 && idx != static_cast<std::size_t>(m_tabIndex))
            continue;

        if (idx != static_cast<std::size_t>(m_tabIndex))
            m_tabs[idx] = makeLoadingTab(static_cast<int>(idx));
        startAsyncTabLoad(static_cast<int>(idx));
        return;
    }
}

void SettingsScreen::onContentUpdate(float dt) {
    pollTabLoaders();
    pollClockSync();
    pollClockVerify();
    if (isActive())
        prefetchOneTab();
    TabbedOverlayScreen::onContentUpdate(dt);
    pollTabLoaders();
    pollClockSync();
    pollClockVerify();
}

void SettingsScreen::emitClockSyncToast(const std::string& msg, float holdSeconds) {
    if (msg.empty())
        return;
    if (isFullyVisible()) {
        requestToast(msg, holdSeconds);
        return;
    }
    if (m_externalToastCb) {
        m_externalToastCb(msg, holdSeconds);
        return;
    }
    requestToast(msg, holdSeconds);
}

void SettingsScreen::startWebClockSync(bool toastFailureOnly) {
    auto& i18n = nxui::I18n::instance();
    if (m_clockSyncFuture.valid()) {
        if (!toastFailureOnly) {
            emitClockSyncToast(i18n.tr(
                "settings.system.web_time_busy",
                "Clock sync is already running."));
        }
        return;
    }

    m_clockSyncToastFailureOnly = toastFailureOnly;
    if (!toastFailureOnly) {
        emitClockSyncToast(i18n.tr(
            "settings.system.web_time_working",
            "Syncing clock from the web..."), 1.8f);
    }

    m_clockSyncFuture = std::async(std::launch::async, []() -> ClockSyncFetch {
        ClockSyncFetch out;
        try {
            out.posixUtc = themeshop::http::fetchUtcUnixTime();
            out.ok = true;
        } catch (const std::exception& ex) {
            out.ok = false;
            out.error = ex.what();
        } catch (...) {
            out.ok = false;
            out.error = "Unknown clock sync error";
        }
        // Don't keep nifm/sockets pinned after a one-shot clock fetch — other
        // sysmodules (Discord Activity, etc.) also need those services.
        themeshop::http::shutdown();
        return out;
    });
}

void SettingsScreen::pollClockSync() {
    if (!m_clockSyncFuture.valid())
        return;
    if (m_clockSyncFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    const ClockSyncFetch fetched = m_clockSyncFuture.get();
    const bool failureOnly = m_clockSyncToastFailureOnly;
    m_clockSyncToastFailureOnly = false;
    auto& i18n = nxui::I18n::instance();
    if (!fetched.ok) {
        DebugLog::log("[settings-time] web clock sync fetch failed: %s",
                      fetched.error.c_str());
        if (failureOnly && m_clockSyncBootRetryCb
            && isRetriableClockSyncError(fetched.error)) {
            m_clockSyncBootRetryCb(fetched.error);
            return;
        }
        const std::string prefix = i18n.tr(
            "settings.system.web_time_failed",
            "Could not sync clock from the web.");
        if (fetched.error.empty())
            emitClockSyncToast(prefix);
        else
            emitClockSyncToast(prefix + " " + fetched.error);
        return;
    }

    // Prefer setsys over time:a — Discord can own the admin time session.
    bool automatic = false;
    if (R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&automatic))
        && automatic) {
        const Result disableRc =
            setsysSetUserSystemClockAutomaticCorrectionEnabled(false);
        DebugLog::log("[settings-time] web sync disabled nintendo auto rc=0x%X",
                      disableRc);
    }

    const std::string nintendoLabel = i18n.tr(
        "settings.system.internet_time",
        "Synchronize Clock via Nintendo Servers");
    const std::string publicLabel = i18n.tr(
        "settings.system.web_time",
        "Synchronize Clock via Public Servers");
    for (auto& tab : m_tabs) {
        for (auto& item : tab.items) {
            if (item.type != ItemType::Toggle)
                continue;
            if (item.label == nintendoLabel) {
                item.boolVal = false;
                item.anim01 = 0.f;
            } else if (item.label == publicLabel) {
                item.boolVal = m_webClockSyncEnabled;
                item.anim01 = m_webClockSyncEnabled ? 1.f : 0.f;
            }
        }
    }

    const Result applyRc =
        switchu::services::ClockService::applyPosix(fetched.posixUtc);
    DebugLog::log(
        "[settings-time] web sync menu apply posix=%llu rc=0x%X quiet=%d",
        static_cast<unsigned long long>(fetched.posixUtc), applyRc,
        failureOnly ? 1 : 0);

    if (R_SUCCEEDED(applyRc)) {
        switchu::services::system_time::reseedWallClock("web clock sync");
        m_clockVerifyPosix = fetched.posixUtc;
        m_clockVerifyFrames = 60 * 3;
        m_clockVerifyQuiet = failureOnly;
        m_clockVerifyAnnounced = false;
        return;
    }

    // Last resort: daemon time:s cannot write, but keep the queue for logs.
    const Result daemonRc = switchu::menu::smi_cmd::setPosixNetworkTime(
        fetched.posixUtc, false);
    DebugLog::log(
        "[settings-time] web sync daemon fallback posix=%llu rc=0x%X",
        static_cast<unsigned long long>(fetched.posixUtc), daemonRc);
    if (failureOnly)
        return;

    char buf[128];
    std::snprintf(
        buf, sizeof(buf), "%s (0x%X)",
        i18n.tr("settings.system.time_change_failed",
                "The date and time setting could not be changed.").c_str(),
        applyRc);
    emitClockSyncToast(buf);
}

void SettingsScreen::pollClockVerify() {
    if (m_clockVerifyPosix == 0)
        return;

    u64 now = 0;
    Result rc = timeGetCurrentTime(TimeType_UserSystemClock, &now);
    if (R_FAILED(rc))
        rc = timeGetCurrentTime(TimeType_LocalSystemClock, &now);
    if (R_SUCCEEDED(rc)) {
        const u64 delta = now > m_clockVerifyPosix ? now - m_clockVerifyPosix
                                                   : m_clockVerifyPosix - now;
        if (delta <= 15) {
            DebugLog::log(
                "[settings-time] web sync verified posix=%llu delta=%llu quiet=%d",
                static_cast<unsigned long long>(m_clockVerifyPosix),
                static_cast<unsigned long long>(delta),
                m_clockVerifyQuiet ? 1 : 0);
            if (!m_clockVerifyQuiet) {
                auto& i18n = nxui::I18n::instance();
                TimeCalendarTime calendar{};
                TimeCalendarAdditionalInfo additional{};
                char buf[96];
                if (R_SUCCEEDED(timeToCalendarTimeWithMyRule(
                        now, &calendar, &additional))) {
                    std::snprintf(buf, sizeof(buf),
                                  "%s (%04u-%02u-%02u %02u:%02u)",
                                  i18n.tr("settings.system.web_time_saved",
                                          "Clock synced from the web.").c_str(),
                                  static_cast<unsigned>(calendar.year),
                                  static_cast<unsigned>(calendar.month),
                                  static_cast<unsigned>(calendar.day),
                                  static_cast<unsigned>(calendar.hour),
                                  static_cast<unsigned>(calendar.minute));
                } else {
                    std::snprintf(buf, sizeof(buf), "%s",
                                  i18n.tr("settings.system.web_time_saved",
                                          "Clock synced from the web.").c_str());
                }
                emitClockSyncToast(buf, 3.2f);
            }
            m_clockVerifyPosix = 0;
            m_clockVerifyFrames = 0;
            return;
        }
    }

    if (m_clockVerifyFrames <= 0)
        return;
    --m_clockVerifyFrames;
    if (m_clockVerifyFrames > 0)
        return;

    DebugLog::log("[settings-time] web sync still pending posix=%llu",
                  static_cast<unsigned long long>(m_clockVerifyPosix));
    if (!m_clockVerifyQuiet && !m_clockVerifyAnnounced) {
        m_clockVerifyAnnounced = true;
        emitClockSyncToast(nxui::I18n::instance().tr(
            "settings.system.web_time_waiting",
            "Waiting for time services to free, then applying."));
    }
}
