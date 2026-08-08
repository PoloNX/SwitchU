#include "AppletLauncher.hpp"
#include "core/DebugLog.hpp"
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
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

bool AppletLauncher::preparePowerAction() {
    if (!m_cb.beforePowerAction)
        return true;
    if (m_cb.beforePowerAction())
        return true;
    DebugLog::log("[launcher] power action cancelled: SD commit failed");
    return false;
}

void AppletLauncher::launchAlbum() {
    DebugLog::log("[launcher] requesting Album launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchAlbum);
    DebugLog::log("[launcher] Album rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchMiiEditor() {
    DebugLog::log("[launcher] requesting Mii Editor launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchMiiEditor);
    DebugLog::log("[launcher] Mii Editor rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchControllerPairing() {
    DebugLog::log("[launcher] requesting Controller pairing via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchControllers);
    DebugLog::log("[launcher] Controller pairing rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchControllerRemapping() {
    DebugLog::log("[launcher] requesting controller remapping via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(
        switchu::smi::SystemMessage::LaunchControllerRemapping);
    DebugLog::log("[launcher] controller remapping rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchNetConnect() {
    DebugLog::log("[launcher] requesting NetConnect launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(switchu::smi::SystemMessage::LaunchNetConnect);
    DebugLog::log("[launcher] NetConnect rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::launchUserPage(AccountUid uid) {
    DebugLog::log("[launcher] requesting User Page launch via daemon");
    Result rc = switchu::menu::smi_cmd::launchUserPage(uid);
    DebugLog::log("[launcher] User Page rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit)      m_cb.requestExit();
    }
}

void AppletLauncher::enterSleep() {
    if (!preparePowerAction()) return;
    DebugLog::log("[launcher] requesting sleep");
    switchu::menu::smi_cmd::enterSleep();
}

void AppletLauncher::shutdown() {
    if (!preparePowerAction()) return;
    DebugLog::log("[launcher] requesting shutdown");
    switchu::menu::smi_cmd::shutdown();
}

void AppletLauncher::reboot() {
    if (!preparePowerAction()) return;
    DebugLog::log("[launcher] requesting reboot");
    switchu::menu::smi_cmd::reboot();
}

void AppletLauncher::launchApplication(uint64_t titleId, AccountUid uid) {
    DebugLog::log("[launcher] tid=%016lX", titleId);
    Result rc = switchu::menu::smi_cmd::launchApplication(titleId, uid);
    if (R_FAILED(rc)) {
        DebugLog::log("[launcher] FAIL: 0x%X", rc);
        return;
    }
    DebugLog::log("[launcher] command sent, closing menu");
    if (m_cb.requestExit) m_cb.requestExit();
}

void AppletLauncher::resumeApplication() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[launcher] no app suspended!");
        return;
    }
    DebugLog::log("[launcher] resume, closing menu");
    switchu::menu::smi_cmd::resumeApplication();
    if (m_cb.requestExit) m_cb.requestExit();
}

void AppletLauncher::terminateApplication() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[launcher] no app suspended, nothing to terminate");
        return;
    }
    DebugLog::log("[launcher] requesting terminate 0x%016lX", (uint64_t)m_suspendedTitleId);
    const Result rc = switchu::menu::smi_cmd::terminateApplication();
    if (R_FAILED(rc))
        DebugLog::log("[launcher] terminate command FAIL: 0x%X", rc);
}

void AppletLauncher::checkRunningApplication() {
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
void AppletLauncher::launchControllerRemapping() {}
void AppletLauncher::launchNetConnect()        {}
void AppletLauncher::launchUserPage(AccountUid) {}
void AppletLauncher::enterSleep()              {}
void AppletLauncher::shutdown()                {}
void AppletLauncher::reboot()                  {}
void AppletLauncher::launchApplication(uint64_t, AccountUid) {}
void AppletLauncher::resumeApplication()       {}
void AppletLauncher::terminateApplication()    {}
void AppletLauncher::checkRunningApplication() {}

#endif
