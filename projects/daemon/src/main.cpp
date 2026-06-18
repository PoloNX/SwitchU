
#include <switch.h>
#include <switch/applets/friends_la.h>
#include <cstdlib>
#include <switchu/smi_protocol.hpp>
#include <switchu/smi_helpers.hpp>
#include <switchu/control_cache.hpp>
#include <switchu/ns_ext.hpp>
#include <switchu/file_log.hpp>
#include "app_manager.hpp"
#include "ecs.hpp"
#include "menu_launcher.hpp"
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

using namespace switchu;

static bool g_timeReady = false;
static bool g_setsysReady = false;
static bool g_setReady = false;
static bool g_nsReady = false;
static bool g_ldrShellReady = false;
static bool g_accountReady = false;
static bool g_nssuReady = false;
static bool g_avmReady = false;
static bool g_psmReady = false;
static bool g_lblReady = false;
static bool g_hidReady = false;

extern "C" {
    u32 __nx_applet_type = AppletType_SystemApplet;
    u32 __nx_fs_num_sessions = 3;

    size_t __nx_heap_size = 0x800000;
}

extern "C" void __appInit(void) {
    Result rc;

    svcOutputDebugString("[SwitchU-daemon] __appInit start", 34);
    daemon::initializeExternalContentAllocator();

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

    rc = timeInitialize();
    g_timeReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] timeInitialize FAIL", 37);

    rc = setsysInitialize();
    g_setsysReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] setsysInitialize FAIL", 39);

    rc = setInitialize();
    g_setReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] setInitialize FAIL", 36);

    if (g_setsysReady) {
        SetSysFirmwareVersion fw = {};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro) | BIT(31));
    }

    rc = nsInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] nsInitialize FAIL", 35);
        diagAbortWithResult(rc);
    }
    g_nsReady = true;

    rc = ldrShellInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] ldrShellInitialize FAIL", 41);
        diagAbortWithResult(rc);
    }
    g_ldrShellReady = true;

    rc = accountInitialize(AccountServiceType_System);
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] accountInitialize FAIL", 40);
        diagAbortWithResult(rc);
    }
    g_accountReady = true;

    rc = nssuInitialize();
    g_nssuReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] nssuInitialize FAIL", 37);

    rc = avmInitialize();
    g_avmReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] avmInitialize FAIL", 36);

    rc = psmInitialize();
    g_psmReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] psmInitialize FAIL", 36);

    rc = lblInitialize();
    g_lblReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] lblInitialize FAIL", 36);

    rc = hidInitialize();
    g_hidReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
        svcOutputDebugString("[SwitchU-daemon] hidInitialize FAIL", 36);

    rc = fsdevMountSdmc();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[SwitchU-daemon] fsdevMountSdmc FAIL", 37);
        svcSleepThread(100'000'000ULL);
        rc = fsdevMountSdmc();
    }

    switchu::FileLog::open("daemon");
    switchu::FileLog::log("[daemon] __appInit complete (sd mount: 0x%X)", rc);
    switchu::FileLog::log("[daemon] services time=%d setsys=%d set=%d ns=%d ldr=%d account=%d nssu=%d avm=%d psm=%d lbl=%d hid=%d",
                          g_timeReady ? 1 : 0,
                          g_setsysReady ? 1 : 0,
                          g_setReady ? 1 : 0,
                          g_nsReady ? 1 : 0,
                          g_ldrShellReady ? 1 : 0,
                          g_accountReady ? 1 : 0,
                          g_nssuReady ? 1 : 0,
                          g_avmReady ? 1 : 0,
                          g_psmReady ? 1 : 0,
                          g_lblReady ? 1 : 0,
                          g_hidReady ? 1 : 0);

    svcOutputDebugString("[SwitchU-daemon] __appInit done", 31);
}

extern "C" void __appExit(void) {
    switchu::FileLog::log("[daemon] __appExit");
    switchu::FileLog::close();

    if (g_hidReady) hidExit();
    if (g_lblReady) lblExit();
    if (g_psmReady) psmExit();
    if (g_avmReady) avmExit();
    if (g_nssuReady) nssuExit();
    if (g_accountReady) accountExit();
    if (g_ldrShellReady) ldrShellExit();
    if (g_nsReady) nsExit();
    if (g_setReady) setExit();
    if (g_setsysReady) setsysExit();
    if (g_timeReady) timeExit();

    appletExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_eventRefreshPending{false};
static std::atomic<bool> g_eventGcMountFailure{false};
static std::atomic<bool> g_batteryRefreshPending{true};
static std::atomic<Result> g_eventGcMountRc{0};
static bool g_initialEventSkipped = false;
static int  g_eventPollCountdown  = 0;
static int  g_eventPollsRemaining = 0;
static int  g_menuRelaunchCooldown = 0;
static int  g_menuFastExitCount = 0;
static s32      g_lastRecordCount = 0;
static uint64_t g_lastRecordTids[1024] = {};
static uint32_t g_lastViewFlags[1024]  = {};

static constexpr s32 kMaxTrackedApplicationRecords = 1024;
static constexpr s32 kApplicationRecordChunkCount = 30;

struct DaemonAppCatalogEntry {
    uint64_t titleId = 0;
    uint32_t viewFlags = 0;
    bool startupUserKnown = false;
    uint8_t startupUserAccount = 1;
    uint8_t startupUserAccountOption = 0;
    std::string name;
};

static std::vector<DaemonAppCatalogEntry> g_appCatalog;
static std::atomic<bool> g_appCatalogRefreshPending{false};
static int g_appCatalogRefreshDelay = 0;
static constexpr const char* kAppCatalogPath = "sdmc:/config/SwitchU/applist.bin";
static constexpr const char* kAppCatalogTmpPath = "sdmc:/config/SwitchU/applist.tmp";
static std::mutex g_controlCacheQueueMutex;
static std::vector<uint64_t> g_controlCacheQueue;
static std::atomic<bool> g_controlCacheRefreshPending{false};
static std::atomic<int> g_controlCacheRefreshDelay{0};

enum class ActionType : uint32_t {
    LaunchApplication,
    ResumeApplication,
    OpenAlbum,
    OpenMiiEditor,
    OpenControllers,
    OpenNetConnect,
    OpenUserPage,
};

struct Action {
    ActionType type;
    uint64_t title_id = 0;
    AccountUid uid = {};
};

static std::vector<Action> g_actionQueue;
static bool g_foregroundAppletActive = false;
static bool g_pendingForegroundAppletHome = false;
static uint8_t g_lastBatteryPercent = 0xFF;
static PsmChargerType g_lastChargerType = (PsmChargerType)0xFF;
static int g_batteryPollCountdown = 0;

static bool shouldDeferViewPolling() {
    return daemon::app::isRunning() &&
           daemon::app::hasForeground() &&
           !daemon::menu_la::isActive() &&
           !g_foregroundAppletActive;
}

static bool listApplicationRecords(std::vector<switchu::ns::ExtApplicationRecord>& records,
                                   const char* tag) {
    records.clear();

    switchu::ns::ExtApplicationRecord chunk[kApplicationRecordChunkCount] = {};
    s32 offset = 0;
    while (offset < kMaxTrackedApplicationRecords) {
        s32 readCount = 0;
        Result rc = nsListApplicationRecord(
            reinterpret_cast<NsApplicationRecord*>(chunk),
            kApplicationRecordChunkCount,
            offset,
            &readCount);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[%s] nsListApplicationRecord FAIL: 0x%X offset=%d",
                                  tag, rc, offset);
            records.clear();
            return false;
        }
        if (readCount <= 0)
            break;

        const s32 remaining = kMaxTrackedApplicationRecords - offset;
        const s32 appendCount = readCount > remaining ? remaining : readCount;
        records.insert(records.end(), chunk, chunk + appendCount);
        offset += readCount;

        if (readCount < kApplicationRecordChunkCount)
            break;
    }

    std::sort(records.begin(), records.end(),
              [](const switchu::ns::ExtApplicationRecord& a,
                 const switchu::ns::ExtApplicationRecord& b) {
                  return a.id < b.id;
              });
    return true;
}

static bool queryApplicationViews(const std::vector<switchu::ns::ExtApplicationRecord>& records,
                                  std::vector<switchu::ns::ExtApplicationView>& views,
                                  const char* tag) {
    views.clear();
    if (records.empty())
        return true;

    std::vector<uint64_t> tids(records.size());
    for (size_t i = 0; i < records.size(); ++i)
        tids[i] = records[i].id;

    views.resize(records.size());
    Result rc = switchu::ns::queryApplicationViews(
        tids.data(),
        static_cast<int>(tids.size()),
        views.data());
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[%s] queryApplicationViews FAIL: 0x%X", tag, rc);
        std::fill(views.begin(), views.end(), switchu::ns::ExtApplicationView{});
        return false;
    }

    return true;
}

static void enqueueControlCacheRecords(const std::vector<switchu::ns::ExtApplicationRecord>& records) {
    std::lock_guard<std::mutex> lock(g_controlCacheQueueMutex);
    for (const auto& record : records) {
        if (record.id == 0 || switchu::control_cache::hasMeta(record.id))
            continue;
        if (std::find(g_controlCacheQueue.begin(), g_controlCacheQueue.end(), record.id) ==
            g_controlCacheQueue.end()) {
            g_controlCacheQueue.push_back(record.id);
        }
    }
}

static bool popControlCacheTitle(uint64_t& outTitleId) {
    std::lock_guard<std::mutex> lock(g_controlCacheQueueMutex);
    if (g_controlCacheQueue.empty())
        return false;

    outTitleId = g_controlCacheQueue.front();
    g_controlCacheQueue.erase(g_controlCacheQueue.begin());
    return true;
}

static bool writeAppCatalogFile() {
    std::error_code fsEc;
    std::filesystem::create_directory("sdmc:/config", fsEc);
    fsEc.clear();
    std::filesystem::create_directory("sdmc:/config/SwitchU", fsEc);

    std::ofstream f(kAppCatalogTmpPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        switchu::FileLog::log("[catalog] fopen tmp FAIL");
        return false;
    }

    uint32_t count = static_cast<uint32_t>(g_appCatalog.size());
    bool ok = static_cast<bool>(f.write(reinterpret_cast<const char*>(&count), sizeof(count)));
    for (const auto& ent : g_appCatalog) {
        if (!ok) break;

        smi::AppEntryHeader eh{};
        eh.title_id = ent.titleId;
        eh.name_len = static_cast<uint32_t>(ent.name.size());
        eh.icon_data_len = 0;
        eh.view_flags = ent.viewFlags;
        eh.startup_user_account = ent.startupUserAccount;
        eh.startup_user_account_option = ent.startupUserAccountOption;
        eh.startup_user_known = ent.startupUserKnown ? 1 : 0;

        ok = static_cast<bool>(f.write(reinterpret_cast<const char*>(&eh), sizeof(eh)));
        if (ok && eh.name_len > 0)
            ok = static_cast<bool>(f.write(ent.name.data(), static_cast<std::streamsize>(eh.name_len)));
    }

    f.close();
    ok = ok && static_cast<bool>(f);
    if (!ok) {
        fsEc.clear();
        std::filesystem::remove(kAppCatalogTmpPath, fsEc);
        switchu::FileLog::log("[catalog] write FAIL");
        return false;
    }

    fsEc.clear();
    std::filesystem::remove(kAppCatalogPath, fsEc);
    fsEc.clear();
    std::filesystem::rename(kAppCatalogTmpPath, kAppCatalogPath, fsEc);
    if (fsEc) {
        fsEc.clear();
        std::filesystem::remove(kAppCatalogTmpPath, fsEc);
        switchu::FileLog::log("[catalog] rename FAIL");
        return false;
    }

    return true;
}

static bool rebuildAppCatalog(const char* reason, bool* outChanged = nullptr) {
    std::vector<switchu::ns::ExtApplicationRecord> records;
    if (!listApplicationRecords(records, "catalog"))
        return false;

    std::vector<switchu::ns::ExtApplicationView> views;
    queryApplicationViews(records, views, "catalog");
    enqueueControlCacheRecords(records);

    const s32 count = static_cast<s32>(records.size());
    g_appCatalog.clear();
    g_appCatalog.reserve(count);

    for (s32 i = 0; i < count; ++i) {
        const uint64_t tid = records[i].id;
        DaemonAppCatalogEntry ent;
        ent.titleId = tid;
        ent.viewFlags = views[i].flags;
        char fallbackName[17] = {};
        std::snprintf(fallbackName, sizeof(fallbackName), "%016lX",
                      static_cast<unsigned long>(tid));
        ent.name = fallbackName;
        g_appCatalog.push_back(std::move(ent));
    }

    const s32 prevCount = g_lastRecordCount;
    bool changed = prevCount != count;
    if (!changed) {
        for (s32 i = 0; i < count; ++i) {
            if (g_lastRecordTids[i] != records[i].id) {
                changed = true;
                break;
            }
        }
    }
    g_lastRecordCount = count;
    for (s32 i = 0; i < count; ++i) {
        g_lastRecordTids[i] = records[i].id;
        g_lastViewFlags[i] = views[i].flags;
    }
    for (s32 i = count; i < prevCount && i < kMaxTrackedApplicationRecords; ++i) {
        g_lastRecordTids[i] = 0;
        g_lastViewFlags[i] = 0;
    }

    const bool ok = writeAppCatalogFile();
    switchu::FileLog::log("[catalog] rebuilt %d/%d apps reason=%s write=%d",
                          (int)g_appCatalog.size(), (int)count, reason, ok ? 1 : 0);
    g_appCatalog.clear();
    g_appCatalog.shrink_to_fit();
    if (outChanged)
        *outChanged = changed;
    return ok;
}

static void cancelViewPolling(const char* reason) {
    const bool hadPendingEvent = g_eventRefreshPending.exchange(false);
    if (hadPendingEvent || g_eventPollsRemaining > 0) {
        switchu::FileLog::log("[views] cancelling background poll (%s) pending=%d remaining=%d",
                              reason,
                              hadPendingEvent ? 1 : 0,
                              g_eventPollsRemaining);
    }
    g_eventPollCountdown = 0;
    g_eventPollsRemaining = 0;
}

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
        Result rc = appletStorageWrite(&st, 0, &notif, sizeof(notif));
        if (R_SUCCEEDED(rc)) {
            switchu::FileLog::log("[notify] push msg=%u", (unsigned)msg);
            daemon::menu_la::pushStorage(&st);
        } else {
            appletStorageClose(&st);
            switchu::FileLog::log("[notify] write FAIL: 0x%X msg=%u", rc, (unsigned)msg);
        }
    } else {
        switchu::FileLog::log("[notify] push FAIL (alloc) msg=%u", (unsigned)msg);
    }
}

static bool queryBatteryStatus(uint8_t& percent, PsmChargerType& chargerType) {
    if (!g_psmReady)
        return false;

    u32 charge = 100;
    Result chargeRc = psmGetBatteryChargePercentage(&charge);
    if (R_FAILED(chargeRc)) {
        switchu::FileLog::log("[battery] psmGetBatteryChargePercentage FAIL: 0x%X", chargeRc);
        return false;
    }

    PsmChargerType ct = PsmChargerType_Unconnected;
    Result chargerRc = psmGetChargerType(&ct);
    if (R_FAILED(chargerRc)) {
        switchu::FileLog::log("[battery] psmGetChargerType FAIL: 0x%X", chargerRc);
        ct = PsmChargerType_Unconnected;
    }

    if (charge > 100)
        charge = 100;
    percent = static_cast<uint8_t>(charge);
    chargerType = ct;
    return true;
}

static void pushBatteryStatusNotification(bool force) {
    if (!daemon::menu_la::isActive())
        return;

    uint8_t percent = 0;
    PsmChargerType chargerType = PsmChargerType_Unconnected;
    if (!queryBatteryStatus(percent, chargerType))
        return;

    if (!force && percent == g_lastBatteryPercent && chargerType == g_lastChargerType)
        return;

    g_lastBatteryPercent = percent;
    g_lastChargerType = chargerType;

    const uint32_t payload = smi::makeBatteryPayload(percent, static_cast<uint32_t>(chargerType));
    switchu::FileLog::log("[battery] notify percent=%u charger=%u",
                          (unsigned)percent,
                          (unsigned)chargerType);
    pushNotification(smi::MenuMessage::BatteryStatusChanged, 0, payload);
}

static void logHomeState(const char* source, const char* stage) {
    switchu::FileLog::log("[%s] HOME %s: appRunning=%d appFg=%d suspended=0x%016lX menuHolder=%d menuActive=%d fgApplet=%d",
                          source, stage,
                          daemon::app::isRunning() ? 1 : 0,
                          daemon::app::hasForeground() ? 1 : 0,
                          daemon::app::suspendedTitleId(),
                          daemon::menu_la::hasHolder() ? 1 : 0,
                          daemon::menu_la::isActive() ? 1 : 0,
                          g_foregroundAppletActive ? 1 : 0);
}

static bool takeForegroundFromRunningApp(const char* source) {
    if (!daemon::app::isRunning() || !daemon::app::hasForeground())
        return true;

    Result unlockRc = appletUnlockForeground();
    switchu::FileLog::log("[%s] UnlockForeground rc=0x%X", source, unlockRc);
    Result fgRc = appletRequestToGetForeground();
    switchu::FileLog::log("[%s] RequestToGetForeground rc=0x%X", source, fgRc);
    if (R_FAILED(fgRc))
        return false;

    daemon::app::onHomeSuspend();
    return true;
}

static void startPowerSequence(const char* source, smi::SystemMessage action) {
    cancelViewPolling(source);
    takeForegroundFromRunningApp(source);

    switch (action) {
        case smi::SystemMessage::EnterSleep:
            appletStartSleepSequence(true);
            break;
        case smi::SystemMessage::Shutdown:
            appletStartShutdownSequence();
            break;
        case smi::SystemMessage::Reboot:
            appletStartRebootSequence();
            break;
        default:
            break;
    }
}

static void openMenuFromHome(const char* source) {
    logHomeState(source, "request");
    cancelViewPolling("home");

    if (daemon::app::isRunning() && daemon::app::hasForeground()) {
        if (!takeForegroundFromRunningApp(source)) {
            switchu::FileLog::log("[%s] HOME aborted: foreground request failed", source);
            return;
        }
        const auto status = buildSystemStatus();
        switchu::FileLog::log("[%s] HOME launching MainMenu status.running=%d suspended=0x%016lX",
                              source, status.app_running ? 1 : 0, status.suspended_app_id);
        Result menuRc = daemon::menu_la::launch(smi::MenuStartMode::MainMenu, status);
        switchu::FileLog::log("[%s] HOME MainMenu launch rc=0x%X", source, menuRc);
        if (R_SUCCEEDED(menuRc))
            g_appCatalogRefreshDelay = 200;
        logHomeState(source, "after");
        return;
    }

    if (daemon::menu_la::isActive()) {
        switchu::FileLog::log("[%s] HOME forwarding HomeRequest to active menu", source);
        pushNotification(smi::MenuMessage::HomeRequest);
    } else if (g_foregroundAppletActive) {
        switchu::FileLog::log("[%s] HOME requested while foreground applet active", source);
        g_pendingForegroundAppletHome = true;
    } else {
        switchu::FileLog::log("[%s] HOME no app/menu active; launching MainMenu", source);
        Result menuRc = daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
        switchu::FileLog::log("[%s] HOME MainMenu launch rc=0x%X", source, menuRc);
        if (R_SUCCEEDED(menuRc))
            g_appCatalogRefreshDelay = 80;
    }
}

static bool sendViewFlagsUpdates() {
    std::vector<switchu::ns::ExtApplicationRecord> records;
    if (!listApplicationRecords(records, "views"))
        return false;

    std::vector<switchu::ns::ExtApplicationView> views;
    queryApplicationViews(records, views, "views");

    const s32 count = static_cast<s32>(records.size());

    if (count != g_lastRecordCount) {
        const s32 prevCount = g_lastRecordCount;
        switchu::FileLog::log("[views] title count changed %d -> %d, full reload needed",
                              g_lastRecordCount, count);

        for (s32 i = 0; i < count; ++i) {
            g_lastRecordTids[i] = records[i].id;
            g_lastViewFlags[i]  = views[i].flags;
        }
        for (s32 i = count; i < prevCount && i < kMaxTrackedApplicationRecords; ++i) {
            g_lastRecordTids[i] = 0;
            g_lastViewFlags[i]  = 0;
        }
        g_lastRecordCount = count;

        return true;
    }

    int pushed = 0;
    for (s32 i = 0; i < count; ++i) {
        uint32_t newFlags = views[i].flags;
        uint32_t oldFlags = 0;
        for (s32 j = 0; j < g_lastRecordCount; ++j) {
            if (g_lastRecordTids[j] == records[i].id) {
                oldFlags = g_lastViewFlags[j];
                break;
            }
        }
        if (newFlags != oldFlags) {
            pushNotification(smi::MenuMessage::AppViewFlagsUpdate,
                             records[i].id, newFlags);
            ++pushed;
        }
        g_lastRecordTids[i] = records[i].id;
        g_lastViewFlags[i]  = newFlags;
    }
    g_lastRecordCount = count;

    switchu::FileLog::log("[views] checked %d titles, pushed %d flag updates",
                          count, pushed);
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
        openMenuFromHome("sams");
        break;
        case 3:
        switchu::FileLog::log("[sams] -> Sleep");
        startPowerSequence("sams-sleep", smi::SystemMessage::EnterSleep);
        break;
        case 5:
        switchu::FileLog::log("[sams] -> Shutdown");
        startPowerSequence("sams-shutdown", smi::SystemMessage::Shutdown);
        break;
        case 6:
        switchu::FileLog::log("[sams] -> Reboot");
        startPowerSequence("sams-reboot", smi::SystemMessage::Reboot);
        break;
    }
}

static void handleAppletMessages() {
    u32 msg = 0;
    Result rc = appletGetMessage(&msg);
    if (R_FAILED(rc))
        return;

    switchu::FileLog::log("[ae] msg=%u", msg);
    switch (msg) {
        case 1:
        switchu::FileLog::log("[ae] -> ChangeIntoForeground");
        break;

        case 2:
        // AppletMessage_ChangeIntoBackground: another foreground participant is
        // taking over. The menu no longer stays alive in a hidden suspended
        // state, so there is no extra holder to clean up here.
        switchu::FileLog::log("[ae] -> ChangeIntoBackground");
        break;

        case 20:
        openMenuFromHome("ae");
        break;

        case 22:
        case 29:
        case 32:
        switchu::FileLog::log("[ae] -> Sleep (msg=%u)", msg);
        appletStartSleepSequence(true);
        break;

        case 26:
        switchu::FileLog::log("[ae] -> Wakeup");
        g_batteryRefreshPending.store(true);
        if (daemon::app::isRunning() && !daemon::menu_la::isActive()) {
            Result rc = daemon::app::resume();
            if (R_FAILED(rc)) {
                switchu::FileLog::log("[ae] wake resume FAIL: 0x%X", rc);
                appletRequestToGetForeground();
            }
        } else {
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


static Result launchLibraryApplet(AppletId id, const char* name,
                                  const void* inData = nullptr, size_t inDataSize = 0,
                                  u32 libAppletVersion = 0) {
    switchu::FileLog::log("[applet] launching %s id=0x%X version=0x%X in=%zu",
                          name, (u32)id, libAppletVersion, inDataSize);
    Result fgRc = appletRequestToGetForeground();
    switchu::FileLog::log("[applet] %s RequestToGetForeground rc=0x%X", name, fgRc);

    AppletHolder holder;
    switchu::FileLog::log("[applet] %s create call", name);
    Result rc = appletCreateLibraryApplet(&holder, id, LibAppletMode_AllForeground);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[applet] %s create FAIL: 0x%X", name, rc);
        return rc;
    }
    switchu::FileLog::log("[applet] %s create ok", name);

    if (libAppletVersion != 0) {
        LibAppletArgs args;
        libappletArgsCreate(&args, libAppletVersion);
        libappletArgsSetPlayStartupSound(&args, true);
        rc = libappletArgsPush(&args, &holder);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[applet] %s args FAIL: 0x%X", name, rc);
            appletHolderClose(&holder);
            return rc;
        }
        switchu::FileLog::log("[applet] %s args ok", name);
    }

    if (inData && inDataSize > 0) {
        AppletStorage inStor;
        rc = appletCreateStorage(&inStor, inDataSize);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[applet] %s in storage FAIL: 0x%X", name, rc);
            appletHolderClose(&holder);
            return rc;
        }
        rc = appletStorageWrite(&inStor, 0, inData, inDataSize);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[applet] %s in data write FAIL: 0x%X", name, rc);
            appletStorageClose(&inStor);
            appletHolderClose(&holder);
            return rc;
        }
        rc = appletHolderPushInData(&holder, &inStor);
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[applet] %s in data push FAIL: 0x%X", name, rc);
            appletStorageClose(&inStor);
            appletHolderClose(&holder);
            return rc;
        }
        switchu::FileLog::log("[applet] %s in data ok", name);
        appletStorageClose(&inStor);
    }

    switchu::FileLog::log("[applet] %s start call", name);
    rc = appletHolderStart(&holder);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[applet] %s start FAIL: 0x%X", name, rc);
        appletHolderClose(&holder);
        return rc;
    }
    switchu::FileLog::log("[applet] %s start ok", name);

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
        Action action{};
        action.type = ActionType::LaunchApplication;
        action.title_id = args.title_id;
        std::memcpy(&action.uid, args.user_uid, sizeof(action.uid));
        g_actionQueue.push_back(action);
        switchu::FileLog::log("[smi] queued launch 0x%016lX (actions=%zu)", args.title_id, g_actionQueue.size());
        break;
    }

    case smi::SystemMessage::ResumeApplication:
        {
            Action action{};
            action.type = ActionType::ResumeApplication;
            g_actionQueue.push_back(action);
        }
        switchu::FileLog::log("[smi] queued resume (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::TerminateApplication:
        result = daemon::app::terminate();
        if (R_SUCCEEDED(result)) {
            pushNotification(smi::MenuMessage::ApplicationExited);
        }
        break;

    case smi::SystemMessage::LaunchAlbum:
        {
            Action action{};
            action.type = ActionType::OpenAlbum;
            g_actionQueue.push_back(action);
        }
        switchu::FileLog::log("[smi] queued album launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchMiiEditor:
        {
            Action action{};
            action.type = ActionType::OpenMiiEditor;
            g_actionQueue.push_back(action);
        }
        switchu::FileLog::log("[smi] queued Mii Editor launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchNetConnect:
        {
            Action action{};
            action.type = ActionType::OpenNetConnect;
            g_actionQueue.push_back(action);
        }
        switchu::FileLog::log("[smi] queued NetConnect launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchUserPage: {
        auto args = reader.pop<smi::UserArgs>();
        Action action{};
        action.type = ActionType::OpenUserPage;
        std::memcpy(&action.uid, args.user_uid, sizeof(action.uid));
        g_actionQueue.push_back(action);
        switchu::FileLog::log("[smi] queued User Page launch (actions=%zu)", g_actionQueue.size());
        break;
    }

    case smi::SystemMessage::LaunchControllers:
        {
            Action action{};
            action.type = ActionType::OpenControllers;
            g_actionQueue.push_back(action);
        }
        switchu::FileLog::log("[smi] queued Controller launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::EnterSleep:
        startPowerSequence("smi-sleep", smi::SystemMessage::EnterSleep);
        break;

    case smi::SystemMessage::Shutdown:
        startPowerSequence("smi-shutdown", smi::SystemMessage::Shutdown);
        break;

    case smi::SystemMessage::Reboot:
        startPowerSequence("smi-reboot", smi::SystemMessage::Reboot);
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
        Result respRc = writer.createStorage(respSt);
        if (R_SUCCEEDED(respRc))
            daemon::menu_la::pushStorage(&respSt);
        else
            switchu::FileLog::log("[smi] GetSystemStatus response create FAIL: 0x%X", respRc);
        return;
    }

    case smi::SystemMessage::MenuReady:
        switchu::FileLog::log("[smi] menu ready");
        g_batteryRefreshPending.store(true);
        break;

    case smi::SystemMessage::MenuClosing:
        switchu::FileLog::log("[smi] menu closing");
        break;

    }

    if (msg != smi::SystemMessage::MenuClosing) {
        smi::StorageWriter writer(result);
        AppletStorage respSt;
        Result respRc = writer.createStorage(respSt);
        if (R_SUCCEEDED(respRc))
            daemon::menu_la::pushStorage(&respSt);
        else
            switchu::FileLog::log("[smi] response create FAIL: 0x%X", respRc);
    }
}

static bool handleAction(Action& action) {
    if (daemon::menu_la::hasHolder() || g_foregroundAppletActive)
        return false;

    switchu::FileLog::log("[action] handling type=%u", (u32)action.type);
    switch (action.type) {
        case ActionType::LaunchApplication: {
            Result rc = daemon::app::launch(action.title_id, action.uid);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] launch 0x%016lX FAIL: 0x%X", action.title_id, rc);
            return true;
        }

        case ActionType::ResumeApplication: {
            Result rc = daemon::app::resume();
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] resume FAIL: 0x%X", rc);
            return true;
        }

        case ActionType::OpenAlbum: {
            const u8 albumArg = AlbumLaArg_ShowAllAlbumFilesForHomeMenu;
            Result rc = launchLibraryApplet(AppletId_LibraryAppletPhotoViewer,
                                            "Album",
                                            &albumArg,
                                            sizeof(albumArg),
                                            0x10000);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] album FAIL: 0x%X", rc);
            switchu::FileLog::log("[action] relaunching menu after album");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
            return true;
        }

        case ActionType::OpenMiiEditor: {
            const auto miiVer = hosversionAtLeast(10, 2, 0) ? 0x4 : 0x3;
            const MiiLaAppletInput in = {
                .version = miiVer,
                .mode = MiiLaAppletMode_ShowMiiEdit,
                .special_key_code = MiiSpecialKeyCode_Normal,
            };
            Result rc = launchLibraryApplet(AppletId_LibraryAppletMiiEdit,
                                            "MiiEditor", &in, sizeof(in));
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Mii Editor FAIL: 0x%X", rc);
            switchu::FileLog::log("[action] relaunching menu after Mii Editor");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
            return true;
        }

        case ActionType::OpenControllers: {
            Result rc = launchControllerPairing();
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Controllers FAIL: 0x%X", rc);
            switchu::FileLog::log("[action] relaunching menu after Controllers");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
            return true;
        }

        case ActionType::OpenNetConnect: {
            const u32 netType = 1;
            Result rc = launchLibraryApplet(AppletId_LibraryAppletNetConnect,
                                            "NetConnect", &netType,
                                            sizeof(netType), 1);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] NetConnect FAIL: 0x%X", rc);
            switchu::FileLog::log("[action] relaunching menu after NetConnect");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
            return true;
        }

        case ActionType::OpenUserPage: {
            Result rc = friendsLaShowMyProfileForHomeMenu(action.uid);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] User Page FAIL: 0x%X", rc);
            switchu::FileLog::log("[action] relaunching menu after User Page");
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
            return true;
        }
    }

    return false;
}

static bool consumeOneAction() {
    if (g_actionQueue.empty())
        return false;

    for (size_t i = 0; i < g_actionQueue.size(); ++i) {
        if (handleAction(g_actionQueue[i])) {
            g_actionQueue.erase(g_actionQueue.begin() + i);
            return true;
        }
    }
    return false;
}

static void mainLoop() {
    handleGeneralChannel();
    handleAppletMessages();
    handleMenuCommand();

    bool didWork = false;

    if (g_eventRefreshPending.load() && shouldDeferViewPolling()) {
        g_eventPollCountdown = 20;
        g_eventPollsRemaining = 1;
    } else if (g_eventRefreshPending.exchange(false)) {
        if (!g_initialEventSkipped) {
            g_initialEventSkipped = true;
            g_appCatalogRefreshPending.store(false);
            switchu::FileLog::log("[views] skipping initial catch-up event");
        } else {
            switchu::FileLog::log("[views] app record event — starting poll");
            g_eventPollCountdown  = 10;
            g_eventPollsRemaining = 50;
        }
    }

    if (g_appCatalogRefreshPending.load() && !shouldDeferViewPolling() &&
        g_appCatalogRefreshDelay > 0) {
        --g_appCatalogRefreshDelay;
    } else if (g_appCatalogRefreshPending.load() && !shouldDeferViewPolling()) {
        g_appCatalogRefreshPending.store(false);
        bool catalogChanged = false;
        if (rebuildAppCatalog("record-event", &catalogChanged)) {
            if (catalogChanged && daemon::menu_la::isActive())
                pushNotification(smi::MenuMessage::AppRecordsChanged);
            didWork = true;
        }
    }
    if (g_controlCacheRefreshPending.load() && g_controlCacheRefreshDelay.load() > 0) {
        --g_controlCacheRefreshDelay;
    } else if (g_controlCacheRefreshPending.exchange(false)) {
        if (daemon::menu_la::isActive())
            pushNotification(smi::MenuMessage::AppRecordsChanged);
        didWork = true;
    }
    if (g_eventPollsRemaining > 0 && shouldDeferViewPolling()) {
        g_eventPollCountdown = 20;
    } else if (g_eventPollsRemaining > 0 && --g_eventPollCountdown == 0) {
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
    if (g_batteryRefreshPending.exchange(false)) {
        pushBatteryStatusNotification(true);
        g_batteryPollCountdown = 50;
    } else if (daemon::menu_la::isActive()) {
        if (g_batteryPollCountdown > 0)
            --g_batteryPollCountdown;
        if (g_batteryPollCountdown <= 0) {
            pushBatteryStatusNotification(false);
            g_batteryPollCountdown = 50;
        }
    }

    if (g_menuRelaunchCooldown > 0)
        --g_menuRelaunchCooldown;

    if (daemon::menu_la::checkFinished()) {
        switchu::FileLog::log("[main] menu exited (reason=%d)",
            (int)daemon::menu_la::exitReason());
        ++g_menuFastExitCount;
        if (g_menuFastExitCount >= 3) {
            g_menuRelaunchCooldown = 500;
            switchu::FileLog::log("[main] menu fast-exit guard active count=%d cooldown=%d",
                                  g_menuFastExitCount, g_menuRelaunchCooldown);
        }
        didWork = true;
    }

    didWork |= consumeOneAction();

    if (daemon::app::checkFinished()) {
        switchu::FileLog::log("[main] app exited");
        if (daemon::menu_la::isActive()) {
            pushNotification(smi::MenuMessage::ApplicationExited);
        } else {
            daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
        }
        didWork = true;
    }

    if (!didWork && g_menuRelaunchCooldown <= 0 && g_actionQueue.empty() &&
        !daemon::app::isRunning() && !daemon::menu_la::hasHolder() &&
        !g_foregroundAppletActive) {
        switchu::FileLog::log("[main] no app/menu active; relaunching menu");
        daemon::menu_la::launch(smi::MenuStartMode::MainMenu, buildSystemStatus());
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

    PsmSession psmSession{};
    bool hasPsmEvent = false;
    if (g_psmReady) {
        rc = psmBindStateChangeEvent(&psmSession, true, true, true);
        if (R_SUCCEEDED(rc)) {
            hasPsmEvent = true;
            switchu::FileLog::log("[event] registered PSM state change event");
        } else {
            switchu::FileLog::log("[event] psmBindStateChangeEvent FAIL: 0x%X", rc);
        }
    } else {
        switchu::FileLog::log("[event] PSM unavailable; battery events disabled");
    }

    while (g_eventRunning.load()) {
        s32 evIdx = -1;
        Result waitRc;
        if (hasGcEvent && hasPsmEvent) {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                waiterForEvent(&recordEvent),
                waiterForEvent(&gcMountFailEvent),
                waiterForEvent(&psmSession.StateChangeEvent));
        } else if (hasGcEvent) {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                waiterForEvent(&recordEvent),
                waiterForEvent(&gcMountFailEvent));
        } else if (hasPsmEvent) {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                waiterForEvent(&recordEvent),
                waiterForEvent(&psmSession.StateChangeEvent));
        } else {
            waitRc = waitMulti(&evIdx, 1'000'000'000ULL,
                waiterForEvent(&recordEvent));
        }

        if (waitRc == KERNELRESULT(TimedOut)) continue;
        if (R_FAILED(waitRc)) continue;

        if (evIdx == 0) {
            eventClear(&recordEvent);
            switchu::FileLog::log("[event] ApplicationRecordUpdateSystemEvent fired");

            g_appCatalogRefreshPending.store(true);
            g_eventRefreshPending.store(true);
        } else if (evIdx == 1 && hasGcEvent) {
            eventClear(&gcMountFailEvent);

            Result failRc = switchu::ns::getLastGameCardMountFailure();
            switchu::FileLog::log("[event] GameCardMountFailure rc=0x%X", failRc);

            g_eventGcMountRc.store(failRc);
            g_eventGcMountFailure.store(true);
        } else if ((hasGcEvent && hasPsmEvent && evIdx == 2) ||
                   (!hasGcEvent && hasPsmEvent && evIdx == 1)) {
            eventClear(&psmSession.StateChangeEvent);
            switchu::FileLog::log("[event] PSM state change fired");
            g_batteryRefreshPending.store(true);
        }

        svcSleepThread(100'000ULL);
    }

    eventClose(&recordEvent);
    if (hasGcEvent) eventClose(&gcMountFailEvent);
    if (hasPsmEvent) psmUnbindStateChangeEvent(&psmSession);
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

static Thread g_controlCacheThread = {};
static std::atomic<bool> g_controlCacheRunning{false};
static bool g_controlCacheStarted = false;

static void controlCacheThreadFunc(void* arg) {
    (void)arg;
    switchu::FileLog::log("[control-cache] thread alive");
    switchu::control_cache::ensureDirectory();

    while (g_controlCacheRunning.load()) {
        uint64_t titleId = 0;
        if (!popControlCacheTitle(titleId)) {
            svcSleepThread(100'000'000ULL);
            continue;
        }

        if (titleId == 0 || switchu::control_cache::hasMeta(titleId))
            continue;

        auto* controlData = new NsApplicationControlData();
        if (!controlData) {
            switchu::FileLog::log("[control-cache] alloc FAIL title=0x%016lX", titleId);
            svcSleepThread(250'000'000ULL);
            continue;
        }

        size_t controlSize = 0;
        const uint64_t startTick = armGetSystemTick();
        Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                titleId,
                                                controlData,
                                                sizeof(*controlData),
                                                &controlSize);
        const uint64_t elapsedMs = armTicksToNs(armGetSystemTick() - startTick) / 1'000'000ULL;
        if (R_SUCCEEDED(rc) && controlSize >= sizeof(NacpStruct)) {
            const bool ok = switchu::control_cache::writeFromControlData(
                titleId,
                *controlData,
                controlSize);
            switchu::FileLog::log("[control-cache] cached 0x%016lX size=%zu elapsed=%lums ok=%d",
                                  titleId,
                                  controlSize,
                                  static_cast<unsigned long>(elapsedMs),
                                  ok ? 1 : 0);
            if (ok) {
                g_controlCacheRefreshPending.store(true);
                g_controlCacheRefreshDelay.store(60);
            }
        } else {
            switchu::FileLog::log("[control-cache] GetControlData FAIL title=0x%016lX rc=0x%X size=%zu elapsed=%lums",
                                  titleId,
                                  rc,
                                  controlSize,
                                  static_cast<unsigned long>(elapsedMs));
        }

        delete controlData;
        svcSleepThread(10'000'000ULL);
    }

    switchu::FileLog::log("[control-cache] thread exiting");
}

static Result startControlCacheWorker() {
    g_controlCacheRunning.store(true);
    Result rc = threadCreate(&g_controlCacheThread, controlCacheThreadFunc, nullptr,
                             nullptr, 0x10000, 0x2D, -2);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[control-cache] threadCreate FAIL: 0x%X", rc);
        return rc;
    }

    rc = threadStart(&g_controlCacheThread);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[control-cache] threadStart FAIL: 0x%X", rc);
        threadClose(&g_controlCacheThread);
        return rc;
    }

    switchu::FileLog::log("[control-cache] thread started");
    g_controlCacheStarted = true;
    return 0;
}

static void stopControlCacheWorker() {
    if (!g_controlCacheStarted)
        return;
    g_controlCacheRunning.store(false);
    threadWaitForExit(&g_controlCacheThread);
    threadClose(&g_controlCacheThread);
    g_controlCacheStarted = false;
    switchu::FileLog::log("[control-cache] thread stopped");
}

int main(int argc, char* argv[]) {
    switchu::FileLog::log("[daemon] main() entry");

    appletLoadAndApplyIdlePolicySettings();

    rebuildAppCatalog("boot");

    Result rc = startControlCacheWorker();
    if (R_FAILED(rc))
        switchu::FileLog::log("[daemon] control cache worker failed: 0x%X (non-fatal)", rc);

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
    stopControlCacheWorker();
    daemon::menu_la::terminate();
    daemon::app::cleanup();
    switchu::FileLog::log("[daemon] shutdown complete");
    return 0;
}
