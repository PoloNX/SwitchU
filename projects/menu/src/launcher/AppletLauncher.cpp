#include "AppletLauncher.hpp"
#include "core/DebugLog.hpp"
#ifdef SWITCHU_STANDALONE
#include "AppManager.hpp"
#endif
#ifdef SWITCHU_MENU
#ifndef SWITCHU_STANDALONE
#include "smi_commands.hpp"
#endif
#include <switchu/smi_protocol.hpp>
#endif
#include <switch.h>

void AppletLauncher::init(Callbacks cbs) {
    m_cb = std::move(cbs);
}

#ifdef SWITCHU_MENU
bool AppletLauncher::isAppRunning() const  { return m_appRunning; }
bool AppletLauncher::isAppSuspended(uint64_t titleId) const {
    return m_suspendedTitleId != 0 && m_suspendedTitleId == titleId;
}
uint64_t AppletLauncher::suspendedTitleId() const { return m_suspendedTitleId; }

void AppletLauncher::setAppRunning(bool v)          { m_appRunning = v; }
void AppletLauncher::setAppHasForeground(bool v)    { m_appHasForeground = v; }
void AppletLauncher::setSuspendedTitleId(uint64_t v){ m_suspendedTitleId = v; }

void AppletLauncher::setStartupStatus(uint64_t suspendedTitleId, bool appRunning) {
    m_suspendedTitleId = suspendedTitleId;
    m_appRunning       = appRunning;
    m_appHasForeground = false;
    DebugLog::log("[launcher] startup status: suspended=0x%016lX running=%d",
                  suspendedTitleId, appRunning);
}

void AppletLauncher::launchAlbum() {
#ifdef SWITCHU_STANDALONE
    DebugLog::log("[launcher-sa] Album requested");
    if (m_cb.launchLibraryApplet) m_cb.launchLibraryApplet(AppletId_LibraryAppletPhotoViewer);
    if (m_cb.playSfxModalHide)    m_cb.playSfxModalHide();
#else
    DebugLog::log("[launcher] requesting Album launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchAlbum);
    DebugLog::log("[launcher] Album rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
#endif
}

void AppletLauncher::launchMiiEditor() {
#ifdef SWITCHU_STANDALONE
    DebugLog::log("[launcher-sa] MiiEditor requested");
    if (m_cb.launchLibraryApplet) m_cb.launchLibraryApplet(AppletId_LibraryAppletMiiEdit);
    if (m_cb.playSfxModalHide)    m_cb.playSfxModalHide();
#else
    DebugLog::log("[launcher] requesting Mii Editor launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchMiiEditor);
    DebugLog::log("[launcher] Mii Editor rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
#endif
}

void AppletLauncher::launchControllerPairing() {
    DebugLog::log("[launcher] Controller pairing (direct)");
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_max = 8;
    arg.hdr.enable_single_mode = false;
    Result rc = hidLaShowControllerSupportForSystem(nullptr, &arg, true);
    if (R_SUCCEEDED(rc))
        DebugLog::log("[launcher] Controller pairing done");
    else
        DebugLog::log("[launcher] Controller direct FAIL: 0x%X", rc);
}

void AppletLauncher::launchNetConnect() {
#ifdef SWITCHU_STANDALONE
    DebugLog::log("[launcher-sa] NetConnect requested");
    if (m_cb.launchLibraryApplet) m_cb.launchLibraryApplet(AppletId_LibraryAppletNetConnect);
    if (m_cb.playSfxModalHide)    m_cb.playSfxModalHide();
#else
    DebugLog::log("[launcher] requesting NetConnect launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchNetConnect);
    DebugLog::log("[launcher] NetConnect rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
#endif
}

void AppletLauncher::enterSleep() {
    DebugLog::log("[launcher] requesting sleep");
#ifdef SWITCHU_STANDALONE
    appletStartSleepSequence(true);
#else
    switchu::menu::smi_cmd::enterSleep();
#endif
}

void AppletLauncher::launchApplication(uint64_t titleId, AccountUid uid) {
    DebugLog::log("[launcher] tid=%016lX", titleId);
#ifdef SWITCHU_STANDALONE
    Result rc = switchu::standalone::app::launch(titleId, uid);
    if (R_FAILED(rc)) {
        DebugLog::log("[launcher-sa] launch FAIL: 0x%X", rc);
        return;
    }
    DebugLog::log("[launcher-sa] launched, suspending render");
    if (m_cb.suspendForApp) m_cb.suspendForApp();
    else if (m_cb.requestExit) m_cb.requestExit();
#else
    Result rc = switchu::menu::smi_cmd::launchApplication(titleId, uid);
    if (R_FAILED(rc)) {
        DebugLog::log("[launcher] FAIL: 0x%X", rc);
        return;
    }
    DebugLog::log("[launcher] command sent, suspending menu");
    if (m_cb.suspendForApp) m_cb.suspendForApp();
    else if (m_cb.requestExit) m_cb.requestExit();
#endif
}

void AppletLauncher::resumeApplication() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[launcher] no app suspended!");
        return;
    }
    DebugLog::log("[launcher] resume, suspending menu");
#ifdef SWITCHU_STANDALONE
    Result rc = switchu::standalone::app::resume();
    if (R_FAILED(rc))
        DebugLog::log("[launcher-sa] resume FAIL: 0x%X", rc);
    if (m_cb.suspendForApp) m_cb.suspendForApp();
    else if (m_cb.requestExit) m_cb.requestExit();
#else
    switchu::menu::smi_cmd::resumeApplication();
    if (m_cb.suspendForApp) m_cb.suspendForApp();
    else if (m_cb.requestExit) m_cb.requestExit();
#endif
}

void AppletLauncher::terminateApplication() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[launcher] no app suspended, nothing to terminate");
        return;
    }
    DebugLog::log("[launcher] requesting terminate 0x%016lX", (uint64_t)m_suspendedTitleId);
#ifdef SWITCHU_STANDALONE
    switchu::standalone::app::terminate();
#else
    switchu::menu::smi_cmd::terminateApplication();
#endif
}

void AppletLauncher::checkRunningApplication() {
    // Handled by StandaloneManager::update() in standalone mode; no-op here.
}

#else

bool AppletLauncher::isAppRunning() const            { return false; }
bool AppletLauncher::isAppSuspended(uint64_t) const  { return false; }
uint64_t AppletLauncher::suspendedTitleId() const    { return 0; }
void AppletLauncher::setAppRunning(bool)             {}
void AppletLauncher::setAppHasForeground(bool)       {}
void AppletLauncher::setSuspendedTitleId(uint64_t)   {}

void AppletLauncher::launchAlbum()             {}
void AppletLauncher::launchMiiEditor()         {}
void AppletLauncher::launchControllerPairing() {}
void AppletLauncher::launchNetConnect()        {}
void AppletLauncher::enterSleep()              {}
void AppletLauncher::launchApplication(uint64_t, AccountUid) {}
void AppletLauncher::resumeApplication()       {}
void AppletLauncher::terminateApplication()    {}
void AppletLauncher::checkRunningApplication() {}

#endif
