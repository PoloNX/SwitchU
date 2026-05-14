#ifdef SWITCHU_STANDALONE

#include "StandaloneManager.hpp"
#include "DebugLog.hpp"
#include "../launcher/AppManager.hpp"
#include <switchu/ns_ext.hpp>
#include <cstring>

using namespace switchu::standalone;

// ── public API ───────────────────────────────────────────────────────────────

void StandaloneManager::setCallbacks(StandaloneCallbacks cbs) {
    m_cb = std::move(cbs);
}

bool StandaloneManager::start() {
    m_eventRunning.store(true);
    Result rc = threadCreate(&m_eventThread, eventThreadFunc, this,
                             nullptr, 0x4000, 0x2C, -2);
    if (R_FAILED(rc)) {
        DebugLog::log("[standalone] event threadCreate FAIL: 0x%X", rc);
        m_eventRunning.store(false);
        return false;
    }
    rc = threadStart(&m_eventThread);
    if (R_FAILED(rc)) {
        DebugLog::log("[standalone] event threadStart FAIL: 0x%X", rc);
        threadClose(&m_eventThread);
        m_eventRunning.store(false);
        return false;
    }
    DebugLog::log("[standalone] event thread started");
    return true;
}

void StandaloneManager::stop() {
    m_eventRunning.store(false);
    threadWaitForExit(&m_eventThread);
    threadClose(&m_eventThread);
    DebugLog::log("[standalone] event thread stopped");

    if (m_libHolderActive) {
        appletHolderRequestExitOrTerminate(&m_libHolder, 5'000'000'000ULL);
        appletHolderJoin(&m_libHolder);
        appletHolderClose(&m_libHolder);
        m_libHolderActive = false;
    }

    app::cleanup();
}

void StandaloneManager::update() {
    handleGeneralChannel();
    handleAppletMessages();

    // NS application-record changed event
    if (m_recordEventPending.exchange(false)) {
        if (!m_initialEventSkipped) {
            m_initialEventSkipped = true;
            DebugLog::log("[standalone] skipping initial record event");
        } else {
            DebugLog::log("[standalone] app records changed");
            if (m_cb.onAppRecordsChanged) m_cb.onAppRecordsChanged();
        }
    }

    if (m_gcMountFailure.exchange(false)) {
        if (m_cb.onGcMountFailure)
            m_cb.onGcMountFailure(m_gcMountRc.load());
    }

    // Check whether the running application has exited
    if (app::checkFinished()) {
        DebugLog::log("[standalone] running app exited");
        if (m_cb.onAppExited) m_cb.onAppExited();
    }

    // Kick off a pending library-applet launch
    if (m_pendingApplet != PendingApplet::None && !m_libHolderActive)
        launchPendingApplet();

    // Poll already-active library applet
    if (m_libHolderActive)
        checkLibraryAppletFinished();
}

void StandaloneManager::requestLaunchAlbum()      { m_pendingApplet = PendingApplet::Album; }
void StandaloneManager::requestLaunchMiiEditor()  { m_pendingApplet = PendingApplet::MiiEditor; }
void StandaloneManager::requestLaunchNetConnect() { m_pendingApplet = PendingApplet::NetConnect; }

// ── private helpers ──────────────────────────────────────────────────────────

void StandaloneManager::handleGeneralChannel() {
    AppletStorage st;
    if (R_FAILED(appletPopFromGeneralChannel(&st))) return;

    struct SamsHeader { u32 magic; u32 version; u32 msg; u32 reserved; } hdr{};
    s64 sz = 0;
    appletStorageGetSize(&st, &sz);
    if (sz > 0)
        appletStorageRead(&st, 0, &hdr,
                          (size_t)sz < sizeof(hdr) ? (size_t)sz : sizeof(hdr));
    appletStorageClose(&st);

    if (hdr.magic != 0x534D4153) return;

    DebugLog::log("[standalone] SAMS msg=%u", hdr.msg);
    switch (hdr.msg) {
    case 2: // Home
        if (app::isRunning() && app::hasForeground()) {
            app::onHomeSuspend();
            appletRequestToGetForeground();
            if (m_cb.onAppSuspended) m_cb.onAppSuspended(app::suspendedTitleId());
        } else if (m_libHolderActive) {
            DebugLog::log("[standalone] HOME during library applet");
            m_foregroundAppletHome = true;
        } else {
            if (m_cb.onHomeRequest) m_cb.onHomeRequest();
        }
        break;
    case 3: appletStartSleepSequence(true);   break;
    case 5: appletStartShutdownSequence();    break;
    case 6: appletStartRebootSequence();      break;
    }
}

void StandaloneManager::handleAppletMessages() {
    u32 msg = 0;
    while (R_SUCCEEDED(appletGetMessage(&msg))) {
        DebugLog::log("[standalone] applet msg=%u", msg);
        switch (msg) {
        case 2: // ChangeIntoBackground
            // A game's own library applet is acquiring foreground.
            // As the system applet we don't hold a blocking AppletHolder for
            // the menu (we ARE the menu), so nothing to close here.
            break;
        case 20: // HOME pressed
            appletRequestToGetForeground();
            if (app::isRunning() && app::hasForeground()) {
                app::onHomeSuspend();
                if (m_cb.onAppSuspended) m_cb.onAppSuspended(app::suspendedTitleId());
            } else if (m_libHolderActive) {
                m_foregroundAppletHome = true;
            } else {
                if (m_cb.onHomeRequest) m_cb.onHomeRequest();
            }
            break;
        case 22: case 29: case 32:
            appletStartSleepSequence(true);
            break;
        case 26: // Wakeup from sleep
            if (app::isRunning())
                app::resume();
            else
                appletRequestToGetForeground();
            break;
        }
    }
}

void StandaloneManager::launchPendingApplet() {
    AppletId  id      = AppletId_LibraryAppletPhotoViewer;
    const char* name  = "Album";
    u32       version = 0;
    const void* inData = nullptr;
    size_t    inSize   = 0;

    // Static buffers for applet in-data (safe across frames).
    static MiiLaAppletInput s_miiIn  = {};
    static u32              s_netType = 1;

    switch (m_pendingApplet) {
    case PendingApplet::Album:
        id   = AppletId_LibraryAppletPhotoViewer;
        name = "Album";
        break;
    case PendingApplet::MiiEditor:
        id      = AppletId_LibraryAppletMiiEdit;
        name    = "MiiEditor";
        version = hosversionAtLeast(10, 2, 0) ? 0x4 : 0x3;
        s_miiIn = {};
        s_miiIn.version          = version;
        s_miiIn.mode             = MiiLaAppletMode_ShowMiiEdit;
        s_miiIn.special_key_code = MiiSpecialKeyCode_Normal;
        inData  = &s_miiIn;
        inSize  = sizeof(s_miiIn);
        break;
    case PendingApplet::NetConnect:
        id      = AppletId_LibraryAppletNetConnect;
        name    = "NetConnect";
        version = 1;
        s_netType = 1;
        inData  = &s_netType;
        inSize  = sizeof(s_netType);
        break;
    default:
        m_pendingApplet = PendingApplet::None;
        return;
    }

    m_pendingApplet = PendingApplet::None;

    // Notify the menu that it is about to lose the foreground.
    if (m_cb.onLibAppletStarted) m_cb.onLibAppletStarted();

    DebugLog::log("[standalone] launching library applet: %s", name);

    Result rc = appletCreateLibraryApplet(&m_libHolder, id,
                                          LibAppletMode_AllForeground);
    if (R_FAILED(rc)) {
        DebugLog::log("[standalone] %s create FAIL: 0x%X", name, rc);
        if (m_cb.onLibAppletReturned) m_cb.onLibAppletReturned();
        return;
    }

    if (version != 0) {
        LibAppletArgs args;
        libappletArgsCreate(&args, version);
        rc = libappletArgsPush(&args, &m_libHolder);
        if (R_FAILED(rc)) {
            DebugLog::log("[standalone] %s args FAIL: 0x%X", name, rc);
            appletHolderClose(&m_libHolder);
            if (m_cb.onLibAppletReturned) m_cb.onLibAppletReturned();
            return;
        }
    }

    if (inData && inSize > 0) {
        AppletStorage inSt;
        rc = appletCreateStorage(&inSt, (s64)inSize);
        if (R_SUCCEEDED(rc)) {
            appletStorageWrite(&inSt, 0, inData, inSize);
            rc = appletHolderPushInData(&m_libHolder, &inSt);
            if (R_FAILED(rc)) appletStorageClose(&inSt);
        }
    }

    rc = appletHolderStart(&m_libHolder);
    if (R_FAILED(rc)) {
        DebugLog::log("[standalone] %s start FAIL: 0x%X", name, rc);
        appletHolderClose(&m_libHolder);
        if (m_cb.onLibAppletReturned) m_cb.onLibAppletReturned();
        return;
    }

    m_libHolderActive        = true;
    m_foregroundAppletHome   = false;
    DebugLog::log("[standalone] %s started", name);
}

void StandaloneManager::checkLibraryAppletFinished() {
    if (m_foregroundAppletHome) {
        m_foregroundAppletHome = false;
        appletHolderRequestExitOrTerminate(&m_libHolder, 5'000'000'000ULL);
    }

    if (!appletHolderActive(&m_libHolder) ||
        appletHolderCheckFinished(&m_libHolder)) {
        appletHolderJoin(&m_libHolder);
        appletHolderClose(&m_libHolder);
        m_libHolderActive = false;
        DebugLog::log("[standalone] library applet closed");
        appletRequestToGetForeground();
        if (m_cb.onLibAppletReturned) m_cb.onLibAppletReturned();
    }
}

// ── background event thread ──────────────────────────────────────────────────

void StandaloneManager::eventThreadFunc(void* arg) {
    auto* self = static_cast<StandaloneManager*>(arg);
    DebugLog::log("[standalone] event thread alive");

    Event recordEvent{};
    Result rc = nsGetApplicationRecordUpdateSystemEvent(&recordEvent);
    if (R_FAILED(rc)) {
        DebugLog::log("[standalone] nsGetApplicationRecordUpdateSystemEvent FAIL: 0x%X", rc);
        return;
    }

    Event gcMountFailEvent{};
    bool hasGcEvent = false;
    if (hosversionAtLeast(3, 0, 0)) {
        rc = nsGetGameCardMountFailureEvent(&gcMountFailEvent);
        if (R_SUCCEEDED(rc)) {
            hasGcEvent = true;
            DebugLog::log("[standalone] GameCardMountFailureEvent registered");
        }
    }

    while (self->m_eventRunning.load()) {
        s32 evIdx = -1;
        Result waitRc;
        if (hasGcEvent) {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                               waiterForEvent(&recordEvent),
                               waiterForEvent(&gcMountFailEvent));
        } else {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                               waiterForEvent(&recordEvent));
        }
        if (waitRc == KERNELRESULT(TimedOut)) continue;
        if (R_FAILED(waitRc)) continue;

        if (evIdx == 0) {
            eventClear(&recordEvent);
            DebugLog::log("[standalone] ApplicationRecordUpdateSystemEvent fired");
            self->m_recordEventPending.store(true);
        } else if (evIdx == 1 && hasGcEvent) {
            eventClear(&gcMountFailEvent);
            Result failRc = switchu::ns::getLastGameCardMountFailure();
            DebugLog::log("[standalone] GameCardMountFailure rc=0x%X", failRc);
            self->m_gcMountRc.store((uint32_t)failRc);
            self->m_gcMountFailure.store(true);
        }
        svcSleepThread(100'000ULL);
    }

    eventClose(&recordEvent);
    if (hasGcEvent) eventClose(&gcMountFailEvent);
    DebugLog::log("[standalone] event thread exiting");
}

#endif // SWITCHU_STANDALONE
