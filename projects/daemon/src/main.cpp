
#include <switch.h>
#include <cstdlib>
#include <nxtc.h>
#include <switchu/smi_protocol.hpp>
#include <switchu/smi_helpers.hpp>
#include <switchu/ns_ext.hpp>
#include <switchu/file_log.hpp>
#include "app_manager.hpp"
#include "menu_launcher.hpp"
#include "ipc_server.hpp"
#include <cstring>
#include <atomic>
#include <vector>

using namespace switchu;

extern "C" {
    u32 __nx_applet_type = AppletType_SystemApplet;

    size_t __nx_heap_size = 0x1400000;
}

extern "C" void __appInit(void) {
    Result rc;

    svcOutputDebugString("[SwitchU-daemon] __appInit start", 34);

    rc = smInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] smInitialize FAIL", 35);
        diagAbortWithResult(MAKERESULT(Module_Libnx, 500));
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] fsInitialize FAIL", 35);
        diagAbortWithResult(MAKERESULT(Module_Libnx, 501));
    }

    rc = appletInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] appletInitialize FAIL", 39);
        diagAbortWithResult(MAKERESULT(Module_Libnx, 502));
    }
    svcOutputDebugString("[SwitchU-daemon] appletInitialize OK", 37);

    timeInitialize();
    setsysInitialize();
    setInitialize();

    {
        SetSysFirmwareVersion fw = {};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro) | BIT(31));
    }

    nsInitialize();
    accountInitialize(AccountServiceType_System);
    nssuInitialize();
    avmInitialize();
    psmInitialize();
    lblInitialize();
    hidInitialize();

    rc = fsdevMountSdmc();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] fsdevMountSdmc FAIL", 37);
        svcSleepThread(100'000'000ULL);
        rc = fsdevMountSdmc();
    }

    switchu::FileLog::open("daemon");
    switchu::FileLog::log("[daemon] __appInit complete (sd mount: 0x%X)", rc);

    svcOutputDebugString("[SwitchU-daemon] __appInit done", 31);
}

extern "C" void __appExit(void) {
    switchu::FileLog::log("[daemon] __appExit");
    switchu::FileLog::close();

    hidExit();
    lblExit();
    psmExit();
    avmExit();
    nssuExit();
    accountExit();
    nsExit();
    setExit();
    setsysExit();
    timeExit();

    appletExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_eventRefreshPending{false};
static std::atomic<bool> g_eventGcMountFailure{false};
static std::atomic<Result> g_eventGcMountRc{0};
static bool g_initialEventSkipped = false;
static int  g_eventPollCountdown  = 0;
static int  g_eventPollsRemaining = 0;
static s32      g_lastRecordCount = 0;
static uint64_t g_lastRecordTids[1024] = {};
static uint32_t g_lastViewFlags[1024]  = {};
static void pushWakeSignal(const char* tag, uint32_t reason, uint64_t suspendedTid) {
    AppletStorage st;
    Result rc = appletCreateStorage(&st, sizeof(smi::WakeSignal));
    if (R_SUCCEEDED(rc)) {
        smi::WakeSignal ws{};
        ws.magic = smi::kWakeMagic;
        ws.reason = reason;
        ws.suspended_tid = suspendedTid;
        appletStorageWrite(&st, 0, &ws, sizeof(ws));
        daemon::menu_la::pushStorage(&st);
        daemon::menu_la::setSuspended(false);
        switchu::FileLog::log("[%s] pushed wake signal (reason=%u tid=0x%016lX)", tag, reason, suspendedTid);
    }
}
static bool     g_pendingLaunch = false;
static uint64_t g_pendingTitleId = 0;
static AccountUid g_pendingUid = {};

static bool g_pendingResume = false;
static bool g_pendingAlbum = false;
static bool g_pendingMiiEditor = false;
static bool g_pendingNetConnect = false;
static bool g_foregroundAppletActive = false;
static bool g_pendingForegroundAppletHome = false;

static smi::SystemStatus buildSystemStatus() {
    smi::SystemStatus st{};
    st.suspended_app_id = daemon::app::suspendedTitleId();
    st.app_running = daemon::app::isRunning();
    return st;
}

static void pushNotification(smi::MenuMessage msg,
                             uint64_t app_id = 0,
                             uint32_t payload = 0) {
    if (!daemon::menu_la::isActive()) return;
    smi::DaemonNotification notif{};
    notif.magic   = smi::kNotifyMagic;
    notif.msg     = msg;
    notif.app_id  = app_id;
    notif.payload = payload;
    AppletStorage st;
    if (R_SUCCEEDED(appletCreateStorage(&st, sizeof(notif)))) {
        appletStorageWrite(&st, 0, &notif, sizeof(notif));
        switchu::FileLog::log("[notify] push msg=%u", (unsigned)msg);
        daemon::menu_la::pushStorage(&st);
    } else {
        switchu::FileLog::log("[notify] push FAIL (alloc) msg=%u", (unsigned)msg);
    }
}

static bool sendViewFlagsUpdates() {
    NsApplicationRecord records[1024] = {};
    s32 recordCount = 0;
    nsListApplicationRecord(records, 1024, 0, &recordCount);

    static switchu::ns::ExtApplicationView views[1024] = {};
    if (recordCount > 0) {
        uint64_t tids[1024];
        for (s32 i = 0; i < recordCount && i < 1024; ++i)
            tids[i] = records[i].application_id;
        Result rc = switchu::ns::queryApplicationViews(tids, recordCount, views);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[views] queryApplicationViews FAIL: 0x%X", rc);
            std::memset(views, 0, sizeof(views));
        }
    }

    if (recordCount != g_lastRecordCount) {
        const s32 prevCount = g_lastRecordCount;
        switchu::FileLog::log("[views] title count changed %d -> %d, full reload needed",
                              g_lastRecordCount, recordCount);

        for (s32 i = 0; i < recordCount && i < 1024; ++i) {
            g_lastRecordTids[i] = records[i].application_id;
            g_lastViewFlags[i]  = views[i].flags;
        }
        for (s32 i = recordCount; i < prevCount && i < 1024; ++i) {
            g_lastRecordTids[i] = 0;
            g_lastViewFlags[i]  = 0;
        }
        g_lastRecordCount = recordCount;

        return true;
    }

    int pushed = 0;
    for (s32 i = 0; i < recordCount && i < 1024; ++i) {
        uint32_t newFlags = views[i].flags;
        uint32_t oldFlags = 0;
        for (s32 j = 0; j < g_lastRecordCount; ++j) {
            if (g_lastRecordTids[j] == records[i].application_id) {
                oldFlags = g_lastViewFlags[j];
                break;
            }
        }
        if (newFlags != oldFlags) {
            pushNotification(smi::MenuMessage::AppViewFlagsUpdate,
                             records[i].application_id, newFlags);
            ++pushed;
        }
        g_lastRecordTids[i] = records[i].application_id;
        g_lastViewFlags[i]  = newFlags;
    }
    g_lastRecordCount = recordCount;

    switchu::FileLog::log("[views] checked %d titles, pushed %d flag updates",
                          recordCount, pushed);
    return false;
}

static void handleGeneralChannel() {
    AppletStorage st;
    if (R_FAILED(appletPopFromGeneralChannel(&st))) return;

    struct SamsHeader {
        u32 magic;
        u32 version;
        u32 msg;
        u32 reserved;
    } hdr = {};

    s64 sz = 0;
    appletStorageGetSize(&st, &sz);
    if (sz > 0)
        appletStorageRead(&st, 0, &hdr, (size_t)sz < sizeof(hdr) ? (size_t)sz : sizeof(hdr));
    appletStorageClose(&st);

    if (hdr.magic != 0x534D4153) return;

    switchu::FileLog::log("[sams] msg=%u", hdr.msg);
    switch (hdr.msg) {
        case 2:
        switchu::FileLog::log("[sams] -> Home");
        if (daemon::app::isRunning() && daemon::app::hasForeground()) {
            daemon::app::onHomeSuspend();
            appletRequestToGetForeground();
            if (daemon::menu_la::isActive()) {
                pushNotification(smi::MenuMessage::ApplicationSuspended,
                                 daemon::app::suspendedTitleId());
                if (daemon::menu_la::isSuspended())
                    pushWakeSignal("sams", 0, daemon::app::suspendedTitleId());
            } else {
                daemon::menu_la::launch(smi::MenuStartMode::Resume, buildSystemStatus());
            }
        } else if (daemon::menu_la::isActive()) {
            pushNotification(smi::MenuMessage::HomeRequest);
        } else if (g_foregroundAppletActive) {
            switchu::FileLog::log("[sams] -> Home requested while foreground applet active");
            g_pendingForegroundAppletHome = true;
        }
        break;
        case 3:
        switchu::FileLog::log("[sams] -> Sleep");
        appletStartSleepSequence(true);
        break;
        case 5:
        switchu::FileLog::log("[sams] -> Shutdown");
        appletStartShutdownSequence();
        break;
        case 6:
        switchu::FileLog::log("[sams] -> Reboot");
        appletStartRebootSequence();
        break;
    }
}

static void handleAppletMessages() {
    u32 msg = 0;
    while (R_SUCCEEDED(appletGetMessage(&msg))) {
        switchu::FileLog::log("[ae] msg=%u", msg);
        switch (msg) {
            case 20:
            appletRequestToGetForeground();
            if (daemon::app::isRunning() && daemon::app::hasForeground()) {
                daemon::app::onHomeSuspend();
                if (!daemon::menu_la::isActive()) {
                    daemon::menu_la::launch(smi::MenuStartMode::Resume, buildSystemStatus());
                } else {
                    pushNotification(smi::MenuMessage::ApplicationSuspended,
                                     daemon::app::suspendedTitleId());
                    if (daemon::menu_la::isSuspended())
                        pushWakeSignal("ae", 0, daemon::app::suspendedTitleId());
                }
            } else if (daemon::menu_la::isActive()) {
                pushNotification(smi::MenuMessage::HomeRequest);
            } else if (g_foregroundAppletActive) {
                switchu::FileLog::log("[ae] -> Home requested while foreground applet active");
                g_pendingForegroundAppletHome = true;
            }
            break;
            case 22:
            case 29:
        case 32:
            switchu::FileLog::log("[ae] -> Sleep (msg=%u)", msg);
            appletStartSleepSequence(true);
            break;
            case 26:
        switchu::FileLog::log("[ae] -> Wakeup");
            if (daemon::app::isRunning() && !daemon::menu_la::isActive()) {
            Result rc = daemon::app::resume();
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[ae] wake resume FAIL: 0x%X", rc);
            appletRequestToGetForeground();
        }
        } else if (!daemon::app::isRunning()) {
            appletRequestToGetForeground();
        }
        if (daemon::menu_la::isActive()) {
            pushNotification(smi::MenuMessage::WakeUp);
        } else if (!daemon::app::isRunning()) {
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
        }
        break;
        }
    }
}


static Result launchLibraryApplet(AppletId id, const char* name,
                                  const void* inData = nullptr, size_t inDataSize = 0,
                                  u32 libAppletVersion = 0) {
    switchu::FileLog::log("[applet] launching %s", name);
    AppletHolder holder;
    Result rc = appletCreateLibraryApplet(&holder, id, LibAppletMode_AllForeground);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[applet] %s create FAIL: 0x%X", name, rc);
        return rc;
    }

    if (libAppletVersion != 0) {
        LibAppletArgs args;
        libappletArgsCreate(&args, libAppletVersion);
        rc = libappletArgsPush(&args, &holder);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[applet] %s args FAIL: 0x%X", name, rc);
            appletHolderClose(&holder);
            return rc;
        }
    }

    if (inData && inDataSize > 0) {
        AppletStorage inStor;
        rc = appletCreateStorage(&inStor, inDataSize);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[applet] %s in storage FAIL: 0x%X", name, rc);
            appletHolderClose(&holder);
            return rc;
        }
        appletStorageWrite(&inStor, 0, inData, inDataSize);
        rc = appletHolderPushInData(&holder, &inStor);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[applet] %s in data push FAIL: 0x%X", name, rc);
            appletStorageClose(&inStor);
            appletHolderClose(&holder);
            return rc;
        }
    }

    rc = appletHolderStart(&holder);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[applet] %s start FAIL: 0x%X", name, rc);
        appletHolderClose(&holder);
        return rc;
    }

    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;

    while (appletHolderActive(&holder) && !appletHolderCheckFinished(&holder)) {
        handleGeneralChannel();
        handleAppletMessages();

        if (g_pendingForegroundAppletHome) {
            g_pendingForegroundAppletHome = false;
            switchu::FileLog::log("[applet] %s exiting on HOME request", name);
            appletHolderRequestExitOrTerminate(&holder, 5'000'000'000ULL);
        }

        svcSleepThread(10'000'000ULL);
    }

    g_foregroundAppletActive = false;
    appletHolderJoin(&holder);
    appletHolderClose(&holder);
    switchu::FileLog::log("[applet] %s closed", name);
    return 0;
}

static Result launchControllerPairing() {
    switchu::FileLog::log("[applet] launching Controller pairing");
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_max = 8;
    arg.hdr.enable_single_mode = false;
    Result rc = hidLaShowControllerSupportForSystem(nullptr, &arg, true);
    if (R_FAILED(rc))
        switchu::FileLog::log("[applet] Controller FAIL: 0x%X", rc);
    else
        switchu::FileLog::log("[applet] Controller pairing done");
    return rc;
}

static void handleMenuCommand() {
    if (!daemon::menu_la::isActive()) return;

    AppletStorage st;
    if (R_FAILED(daemon::menu_la::popStorage(&st))) return;

    smi::StorageReader reader(st);
    if (!reader.valid()) return;

    auto msg = reader.systemMessage();
    switchu::FileLog::log("[smi] command=%u", (u32)msg);

    Result result = 0;

    switch (msg) {
    case smi::SystemMessage::LaunchApplication: {
        auto args = reader.pop<smi::LaunchAppArgs>();
        g_pendingLaunch = true;
        g_pendingTitleId = args.title_id;
        std::memcpy(&g_pendingUid, args.user_uid, sizeof(g_pendingUid));
        switchu::FileLog::log("[smi] pending launch 0x%016lX (waiting for menu exit)", args.title_id);
        break;
    }

    case smi::SystemMessage::ResumeApplication:
        g_pendingResume = true;
        switchu::FileLog::log("[smi] pending resume (waiting for menu exit)");
        break;

    case smi::SystemMessage::TerminateApplication:
        result = daemon::app::terminate();
        if (R_SUCCEEDED(result)) {
            pushNotification(smi::MenuMessage::ApplicationExited);
        }
        break;

    case smi::SystemMessage::LaunchAlbum:
        g_pendingAlbum = true;
        g_pendingMiiEditor = false;
        g_pendingLaunch = false;
        g_pendingResume = false;
        switchu::FileLog::log("[smi] pending album launch (waiting for menu exit)");
        break;

    case smi::SystemMessage::LaunchMiiEditor:
        g_pendingMiiEditor = true;
        g_pendingAlbum = false;
        g_pendingLaunch = false;
        g_pendingResume = false;
        switchu::FileLog::log("[smi] pending Mii Editor launch (waiting for menu exit)");
        break;

    case smi::SystemMessage::LaunchNetConnect:
        g_pendingNetConnect = true;
        g_pendingMiiEditor = false;
        g_pendingLaunch = false;
        g_pendingResume = false;
        switchu::FileLog::log("[smi] pending NetConnect launch (waiting for menu exit)");
        break;

    case smi::SystemMessage::LaunchControllers:
        switchu::FileLog::log("[smi] LaunchControllers ignored while menu active");
        result = MAKERESULT(Module_Libnx, 0xFC);
        break;

    case smi::SystemMessage::EnterSleep:
        appletStartSleepSequence(true);
        break;

    case smi::SystemMessage::Shutdown:
        appletStartShutdownSequence();
        break;

    case smi::SystemMessage::Reboot:
        appletStartRebootSequence();
        break;

    case smi::SystemMessage::RequestForeground:
        appletRequestToGetForeground();
        break;

    case smi::SystemMessage::GetAppList: {
        break;
    }

    case smi::SystemMessage::GetSystemStatus: {
        auto status = buildSystemStatus();
        smi::StorageWriter writer((Result)0);
        writer.push(status);
        AppletStorage respSt;
        writer.createStorage(respSt);
        daemon::menu_la::pushStorage(&respSt);
        return;
    }

    case smi::SystemMessage::MenuReady:
        switchu::FileLog::log("[smi] menu ready");
        break;

    case smi::SystemMessage::MenuClosing:
        switchu::FileLog::log("[smi] menu closing");
        break;

    case smi::SystemMessage::MenuSuspending:
        switchu::FileLog::log("[smi] menu suspending (keep-alive)");
        daemon::menu_la::setSuspended(true);
        if (g_pendingAlbum) {
            switchu::FileLog::log("[smi] WARN: album pending during suspend");
            g_pendingAlbum = false;
        }
        if (g_pendingLaunch) {
            g_pendingLaunch = false;
            g_pendingResume = false;
            switchu::FileLog::log("[smi] executing pending launch 0x%016lX (menu suspended)", g_pendingTitleId);
            Result launchRc = daemon::app::launch(g_pendingTitleId, g_pendingUid);
            if (R_FAILED(launchRc))
                switchu::FileLog::log("[smi] pending launch FAIL: 0x%X", launchRc);
        } else if (g_pendingResume) {
            g_pendingResume = false;
            switchu::FileLog::log("[smi] executing pending resume (menu suspended)");
            Result resumeRc = daemon::app::resume();
            if (R_FAILED(resumeRc))
                switchu::FileLog::log("[smi] pending resume FAIL: 0x%X", resumeRc);
        }
        break;

    default:
        switchu::FileLog::log("[smi] unknown command %u", (u32)msg);
        result = MAKERESULT(Module_Libnx, 0xFB);
        break;
    }

    if (msg != smi::SystemMessage::MenuClosing) {
        smi::StorageWriter writer(result);
        AppletStorage respSt;
        writer.createStorage(respSt);
        daemon::menu_la::pushStorage(&respSt);
    }
}

static void mainLoop() {
    handleGeneralChannel();
    handleAppletMessages();
    handleMenuCommand();

    if (g_eventRefreshPending.exchange(false)) {
        if (!g_initialEventSkipped) {
            g_initialEventSkipped = true;
            switchu::FileLog::log("[views] skipping initial catch-up event");
        } else {
            switchu::FileLog::log("[views] app record event — starting poll");
            g_eventPollCountdown  = 10;
            g_eventPollsRemaining = 50;
        }
    }
    if (g_eventPollsRemaining > 0 && --g_eventPollCountdown == 0) {
        bool needFullReload = sendViewFlagsUpdates();
        if (needFullReload) {
            if (daemon::menu_la::isActive())
                pushNotification(smi::MenuMessage::AppRecordsChanged);
            g_eventPollsRemaining = 0;
        } else {
            --g_eventPollsRemaining;
            if (g_eventPollsRemaining > 0)
                g_eventPollCountdown = 20;
        }
    }
    if (g_eventGcMountFailure.exchange(false)) {
        if (daemon::menu_la::isActive()) {
            pushNotification(smi::MenuMessage::GameCardMountFailure, 0,
                             (uint32_t)g_eventGcMountRc.load());
        }
    }

    if (daemon::menu_la::checkFinished()) {
        switchu::FileLog::log("[main] menu exited (reason=%d)",
            (int)daemon::menu_la::exitReason());

        if (g_pendingAlbum) {
            g_pendingAlbum = false;
            switchu::FileLog::log("[main] executing pending album launch");
            Result rc = launchLibraryApplet(AppletId_LibraryAppletPhotoViewer, "Album");
            if (R_FAILED(rc))
                switchu::FileLog::log("[main] pending album FAIL: 0x%X", rc);
            switchu::FileLog::log("[main] relaunching menu after album");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
        } else if (g_pendingNetConnect) {
            g_pendingNetConnect = false;
            switchu::FileLog::log("[main] executing pending NetConnect launch");
            const u32 netType = 1;
            Result rc = launchLibraryApplet(AppletId_LibraryAppletNetConnect,
                                            "NetConnect", &netType,
                                            sizeof(netType), 1);
            if (R_FAILED(rc)) {
                switchu::FileLog::log("[main] NetConnect create FAIL: 0x%X", rc);
            }
            switchu::FileLog::log("[main] relaunching menu after NetConnect");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
        } else if (g_pendingMiiEditor) {
            g_pendingMiiEditor = false;
            switchu::FileLog::log("[main] executing pending Mii Editor launch");
            const auto miiVer = hosversionAtLeast(10, 2, 0) ? 0x4 : 0x3;
            const MiiLaAppletInput in = {
                .version = miiVer,
                .mode = MiiLaAppletMode_ShowMiiEdit,
                .special_key_code = MiiSpecialKeyCode_Normal,
            };
            Result rc = launchLibraryApplet(AppletId_LibraryAppletMiiEdit,
                                            "MiiEditor", &in, sizeof(in));
            if (R_FAILED(rc))
                switchu::FileLog::log("[main] pending Mii Editor FAIL: 0x%X", rc);
            switchu::FileLog::log("[main] relaunching menu after Mii Editor");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
        } else if (g_pendingLaunch) {
            g_pendingLaunch = false;
            g_pendingResume = false;
            switchu::FileLog::log("[main] executing pending launch 0x%016lX", g_pendingTitleId);
            Result rc = daemon::app::launch(g_pendingTitleId, g_pendingUid);
            if (R_FAILED(rc))
                switchu::FileLog::log("[main] pending launch FAIL: 0x%X", rc);
        } else if (g_pendingResume) {
            g_pendingResume = false;
            switchu::FileLog::log("[main] executing pending resume");
            Result rc = daemon::app::resume();
            if (R_FAILED(rc))
                switchu::FileLog::log("[main] pending resume FAIL: 0x%X", rc);
        } else if (!daemon::app::isRunning()) {
            switchu::FileLog::log("[main] relaunching menu");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
        }
    }

    if (daemon::app::checkFinished()) {
        switchu::FileLog::log("[main] app exited");
        if (daemon::menu_la::isActive()) {
            pushNotification(smi::MenuMessage::ApplicationExited);
            if (daemon::menu_la::isSuspended())
                pushWakeSignal("main", 1, 0);
        } else {
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
        }
    }
}


static Thread g_eventThread = {};
static std::atomic<bool> g_eventRunning{false};

static void eventManagerThreadFunc(void* arg) {
    (void)arg;
    switchu::FileLog::log("[event] thread alive");

    Event recordEvent = {};
    Result rc = nsGetApplicationRecordUpdateSystemEvent(&recordEvent);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[event] nsGetApplicationRecordUpdateSystemEvent FAIL: 0x%X", rc);
        return;
    }
    switchu::FileLog::log("[event] registered ApplicationRecordUpdateSystemEvent");

    Event gcMountFailEvent = {};
    bool hasGcEvent = false;
    if (hosversionAtLeast(3, 0, 0)) {
        rc = nsGetGameCardMountFailureEvent(&gcMountFailEvent);
        if (R_SUCCEEDED(rc)) {
            hasGcEvent = true;
            switchu::FileLog::log("[event] registered GameCardMountFailureEvent");
        } else {
            switchu::FileLog::log("[event] nsGetGameCardMountFailureEvent FAIL: 0x%X", rc);
        }
    } else {
        switchu::FileLog::log("[event] GameCardMountFailureEvent not supported on this firmware");
    }

    while (g_eventRunning.load()) {
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
            switchu::FileLog::log("[event] ApplicationRecordUpdateSystemEvent fired");

            g_eventRefreshPending.store(true);
        } else if (evIdx == 1 && hasGcEvent) {
            eventClear(&gcMountFailEvent);

            Result failRc = switchu::ns::getLastGameCardMountFailure();
            switchu::FileLog::log("[event] GameCardMountFailure rc=0x%X", failRc);

            g_eventGcMountRc.store(failRc);
            g_eventGcMountFailure.store(true);
        }

        svcSleepThread(100'000ULL);
    }

    eventClose(&recordEvent);
    if (hasGcEvent) eventClose(&gcMountFailEvent);
    switchu::FileLog::log("[event] thread exiting");
}

static Result startEventManager() {
    g_eventRunning.store(true);
    Result rc = threadCreate(&g_eventThread, eventManagerThreadFunc, nullptr,
                             nullptr, 0x4000, 0x2C, -2);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[event] threadCreate FAIL: 0x%X", rc);
        return rc;
    }
    rc = threadStart(&g_eventThread);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[event] threadStart FAIL: 0x%X", rc);
        threadClose(&g_eventThread);
        return rc;
    }
    switchu::FileLog::log("[event] thread started");
    return 0;
}

static void stopEventManager() {
    g_eventRunning.store(false);
    threadWaitForExit(&g_eventThread);
    threadClose(&g_eventThread);
    switchu::FileLog::log("[event] thread stopped");
}

int main(int argc, char* argv[]) {
    switchu::FileLog::log("[daemon] main() entry");

    appletLoadAndApplyIdlePolicySettings();

    if (!nxtcInitialize())
        switchu::FileLog::log("[daemon] nxtc init failed (non-fatal)");

    {
        NsApplicationRecord recs[1024] = {};
        nsListApplicationRecord(recs, 1024, 0, &g_lastRecordCount);
        if (g_lastRecordCount > 0) {
            uint64_t tids[1024];
            for (s32 i = 0; i < g_lastRecordCount; ++i)
                tids[i] = recs[i].application_id;
            static switchu::ns::ExtApplicationView vws[1024];
            switchu::ns::queryApplicationViews(tids, g_lastRecordCount, vws);
            for (s32 i = 0; i < g_lastRecordCount; ++i) {
                g_lastRecordTids[i] = recs[i].application_id;
                g_lastViewFlags[i]  = vws[i].flags;
            }
        }
    }

    Result rc = daemon::ipc::startServer();
    if (R_FAILED(rc))
        switchu::FileLog::log("[daemon] IPC server failed: 0x%X", rc);

    rc = startEventManager();
    if (R_FAILED(rc))
        switchu::FileLog::log("[daemon] event manager failed: 0x%X (non-fatal)", rc);

    switchu::FileLog::log("[daemon] launching menu...");
    rc = daemon::menu_la::launch(smi::MenuStartMode::StartupBoot, buildSystemStatus());
    if (R_FAILED(rc))
        switchu::FileLog::log("[daemon] menu launch failed: 0x%X", rc);

    while (g_running.load()) {
        mainLoop();
        svcSleepThread(10'000'000ULL);
    }

    stopEventManager();
    daemon::menu_la::terminate();
    daemon::app::cleanup();
    daemon::ipc::stopServer();
    nxtcExit();

    switchu::FileLog::log("[daemon] shutdown complete");
    return 0;
}
