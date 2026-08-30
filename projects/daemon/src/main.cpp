
#include <switch.h>
#include <switch/applets/friends_la.h>
#include <cstdlib>
#include <switchu/smi_protocol.hpp>
#include <switchu/smi_helpers.hpp>
#include <switchu/control_cache.hpp>
#include <switchu/ns_ext.hpp>
#include <switchu/file_log.hpp>
#include <switchu/sd_commit.hpp>
#include "app_manager.hpp"
#include "ecs.hpp"
#include "library_applet_runner.hpp"
#include "menu_launcher.hpp"
#include "system_action_queue.hpp"
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
    TimeServiceType __nx_time_service_type = TimeServiceType_System;
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
static std::atomic<bool> g_powerSequenceStarted{false};
static UEvent g_mainWakeEvent{};
static UEvent g_controlCacheWakeEvent{};
static Event g_generalChannelEvent{};
static bool g_generalChannelEventReady = false;
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
// A catalogue rebuild can complete while a full-application homebrew owns the
// foreground and the menu process is absent. Preserve that edge so the freshly
// relaunched menu receives one coalesced refresh instead of keeping a stale
// catalogue until the daemon is restarted.
static std::atomic<bool> g_catalogChangedWhileMenuAway{false};
static int g_appCatalogRefreshDelay = 0;
static constexpr const char* kAppCatalogPath = "sdmc:/config/SwitchU/applist.bin";
static constexpr const char* kAppCatalogTmpPath = "sdmc:/config/SwitchU/applist.tmp";
static constexpr const char* kAppCatalogBackupPath = "sdmc:/config/SwitchU/applist.bak";
static std::mutex g_controlCacheQueueMutex;
static std::vector<uint64_t> g_controlCacheQueue;
static std::atomic<bool> g_controlCacheRefreshPending{false};
static std::atomic<int> g_controlCacheRefreshDelay{0};

static daemon::SystemActionQueue g_actionQueue;
static smi::OperationOutcome g_lastOperationFailure{};
static bool g_foregroundAppletActive = false;
static bool g_pendingForegroundAppletHome = false;
static bool g_pendingHomeMenuLaunch = false;
static const char* g_pendingHomeMenuSource = nullptr;
static uint64_t g_pendingHomeMenuStartedAt = 0;
static uint8_t g_lastBatteryPercent = 0xFF;
static PsmChargerType g_lastChargerType = (PsmChargerType)0xFF;

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
    bool queued = false;
    std::lock_guard<std::mutex> lock(g_controlCacheQueueMutex);
    for (const auto& record : records) {
        if (record.id == 0 || switchu::control_cache::hasMeta(record.id))
            continue;
        if (std::find(g_controlCacheQueue.begin(), g_controlCacheQueue.end(), record.id) ==
            g_controlCacheQueue.end()) {
            g_controlCacheQueue.push_back(record.id);
            queued = true;
        }
    }
    if (queued)
        ueventSignal(&g_controlCacheWakeEvent);
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

    // FAT does not guarantee replace-existing rename semantics. Keep the last
    // complete catalog until the new file is in place so a power loss never
    // leaves the menu with only a truncated/absent catalog.
    fsEc.clear();
    std::filesystem::remove(kAppCatalogBackupPath, fsEc);
    fsEc.clear();
    if (std::filesystem::exists(kAppCatalogPath, fsEc)) {
        fsEc.clear();
        std::filesystem::rename(kAppCatalogPath, kAppCatalogBackupPath, fsEc);
        if (fsEc) {
            std::filesystem::remove(kAppCatalogTmpPath, fsEc);
            switchu::FileLog::log("[catalog] backup rename FAIL");
            return false;
        }
    }

    fsEc.clear();
    std::filesystem::rename(kAppCatalogTmpPath, kAppCatalogPath, fsEc);
    if (fsEc) {
        std::error_code restoreEc;
        if (std::filesystem::exists(kAppCatalogBackupPath, restoreEc)) {
            restoreEc.clear();
            std::filesystem::rename(kAppCatalogBackupPath, kAppCatalogPath, restoreEc);
        }
        fsEc.clear();
        std::filesystem::remove(kAppCatalogTmpPath, fsEc);
        switchu::FileLog::log("[catalog] commit rename FAIL restore=%d",
                              restoreEc ? 0 : 1);
        return false;
    }

    fsEc.clear();
    std::filesystem::remove(kAppCatalogBackupPath, fsEc);

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

        switchu::control_cache::Meta meta{};
        if (switchu::control_cache::readMeta(tid, meta)) {
            const std::size_t nameLength = strnlen(meta.name, sizeof(meta.name));
            if (nameLength > 0 &&
                switchu::control_cache::isValidUtf8(meta.name, nameLength + 1)) {
                ent.name.assign(meta.name, nameLength);
            }
            ent.startupUserAccount = meta.startup_user_account;
            ent.startupUserAccountOption = meta.startup_user_account_option;
            ent.startupUserKnown = true;
        }
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
    st.last_failure = g_lastOperationFailure;
    return st;
}

static void recordOperationResult(uint64_t requestId,
                                  smi::SystemMessage command,
                                  Result result,
                                  uint64_t titleId = 0) {
    if (R_SUCCEEDED(result)) {
        g_lastOperationFailure = {};
        return;
    }

    g_lastOperationFailure = {
        .request_id = requestId,
        .title_id = titleId,
        .command = static_cast<uint32_t>(command),
        .result = static_cast<uint32_t>(result),
    };
    switchu::FileLog::log(
        "[operation] request=%lu command=%u title=0x%016lX FAIL=0x%X",
        static_cast<unsigned long>(requestId),
        static_cast<unsigned>(command), titleId, result);
}

struct ScopedService {
    Service value{};

    ScopedService() = default;
    ScopedService(const ScopedService&) = delete;
    ScopedService& operator=(const ScopedService&) = delete;

    ~ScopedService() {
        serviceClose(&value);
    }
};

static Result openTimeAdminService(ScopedService& out) {
    const Result rc = smGetService(&out.value, "time:a");
    switchu::FileLog::log(
        "[settings-time] daemon open time:a rc=0x%X", rc);
    return rc;
}

static Result setLiveAutomaticCorrection(bool enabled) {
    ScopedService admin;
    Result rc = openTimeAdminService(admin);
    if (R_SUCCEEDED(rc)) {
        const u8 flag = enabled ? 1 : 0;
        rc = serviceDispatchIn(&admin.value, 101, flag);
    }

    switchu::FileLog::log(
        "[settings-time] daemon live automaticCorrection enabled=%d rc=0x%X",
        enabled ? 1 : 0, rc);
    return rc;
}

static Result setInternetTimeSync(bool enabled) {
    const Result setsysRc =
        setsysSetUserSystemClockAutomaticCorrectionEnabled(enabled);
    const Result liveRc = R_SUCCEEDED(setsysRc)
        ? setLiveAutomaticCorrection(enabled)
        : setsysRc;

    bool confirmed = !enabled;
    const Result confirmRc =
        setsysIsUserSystemClockAutomaticCorrectionEnabled(&confirmed);
    Result result = 0;
    if (R_FAILED(setsysRc)) {
        result = setsysRc;
    } else if (R_FAILED(liveRc)) {
        result = liveRc;
    } else if (R_FAILED(confirmRc)) {
        result = confirmRc;
    } else if (confirmed != enabled) {
        result = MAKERESULT(Module_Libnx, 908);
    }

    switchu::FileLog::log(
        "[settings-time] daemon internetTimeSync enabled=%d setsys=0x%X live=0x%X confirm=0x%X state=%d result=0x%X",
        enabled ? 1 : 0, setsysRc, liveRc, confirmRc,
        confirmed ? 1 : 0, result);
    return result;
}

static Result setManualDateTime(const smi::ManualDateTimeArgs& args) {
    TimeCalendarTime calendar{};
    calendar.year = static_cast<u16>(args.year);
    calendar.month = static_cast<u8>(args.month);
    calendar.day = static_cast<u8>(args.day);
    calendar.hour = static_cast<u8>(args.hour);
    calendar.minute = static_cast<u8>(args.minute);
    calendar.second = 0;

    u64 timestamps[2]{};
    s32 timestampCount = 0;
    Result rc = timeToPosixTimeWithMyRule(&calendar, timestamps, 2, &timestampCount);
    switchu::FileLog::log(
        "[settings-time] daemon convert %04u-%02u-%02u %02u:%02u rc=0x%X count=%d posix=%llu",
        args.year, args.month, args.day, args.hour, args.minute,
        rc, timestampCount,
        (unsigned long long)(timestampCount > 0 ? timestamps[0] : 0));
    if (R_SUCCEEDED(rc) && timestampCount <= 0)
        rc = MAKERESULT(Module_Libnx, 902);
    if (R_FAILED(rc))
        return rc;
    const u64 targetTimestamp = timestamps[0];

    auto isCloseToTarget = [&](u64 actual) -> bool {
        const u64 delta = actual > targetTimestamp ? actual - targetTimestamp
                                                   : targetTimestamp - actual;
        return delta <= 120;
    };

    auto waitForUserClock = [&]() -> Result {
        Result lastRc = 0;
        u64 last = 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            lastRc = timeGetCurrentTime(TimeType_UserSystemClock, &last);
            const bool close = R_SUCCEEDED(lastRc) && isCloseToTarget(last);
            if (close)
                return 0;
            svcSleepThread(100'000'000ULL);
        }
        switchu::FileLog::log(
            "[settings-time] daemon user clock verification failed rc=0x%X posix=%llu",
            lastRc, (unsigned long long)last);
        return R_FAILED(lastRc) ? lastRc : MAKERESULT(Module_Libnx, 906);
    };

    const Result enableRc = setLiveAutomaticCorrection(true);
    Result networkRc = enableRc;
    Result waitRc = enableRc;
    if (R_SUCCEEDED(enableRc)) {
        networkRc = timeSetCurrentTime(
            TimeType_NetworkSystemClock, targetTimestamp);
        switchu::FileLog::log(
            "[settings-time] daemon temporary automaticCorrection network set rc=0x%X",
            networkRc);
        if (R_SUCCEEDED(networkRc))
            waitRc = waitForUserClock();
    }

    const Result restoreRc = setLiveAutomaticCorrection(false);
    switchu::FileLog::log(
        "[settings-time] daemon temporary automaticCorrection enable=0x%X network=0x%X wait=0x%X restore=0x%X",
        enableRc, networkRc, waitRc, restoreRc);

    if (R_FAILED(enableRc))
        return enableRc;
    if (R_FAILED(networkRc))
        return networkRc;
    if (R_FAILED(waitRc))
        return waitRc;
    return restoreRc;
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

    const uint64_t startedAt = armGetSystemTick();
    Result fgRc = appletRequestToGetForeground();
    switchu::FileLog::log("[%s] RequestToGetForeground rc=0x%X elapsed=%lums",
                          source, fgRc,
                          static_cast<unsigned long>(
                              armTicksToNs(armGetSystemTick() - startedAt) / 1'000'000ULL));
    if (R_FAILED(fgRc))
        return false;

    daemon::app::onHomeSuspend();
    return true;
}

static Result startControlCacheWorker();
static void stopControlCacheWorker();
static Result startEventManager();
static void stopEventManager();

static Result launchPendingHomeMenu() {
    if (!g_pendingHomeMenuLaunch)
        return 0;

    const char* source = g_pendingHomeMenuSource ? g_pendingHomeMenuSource : "home";
    const uint64_t startedAt = g_pendingHomeMenuStartedAt;
    g_pendingHomeMenuLaunch = false;
    g_pendingHomeMenuSource = nullptr;
    g_pendingHomeMenuStartedAt = 0;

    const auto status = buildSystemStatus();
    switchu::FileLog::log(
        "[%s] HOME foreground acquired; launching MainMenu status.running=%d suspended=0x%016lX",
        source, status.app_running ? 1 : 0, status.suspended_app_id);
    const uint64_t launchStartedAt = armGetSystemTick();
    const Result rc = daemon::menu_la::launch(smi::MenuStartMode::MainMenu, status);
    const uint64_t launchDoneAt = armGetSystemTick();
    switchu::FileLog::log(
        "[%s] HOME MainMenu launch rc=0x%X foreground_wait=%lums launch=%lums total=%lums",
        source, rc,
        static_cast<unsigned long>(armTicksToNs(launchStartedAt - startedAt) / 1'000'000ULL),
        static_cast<unsigned long>(armTicksToNs(launchDoneAt - launchStartedAt) / 1'000'000ULL),
        static_cast<unsigned long>(armTicksToNs(launchDoneAt - startedAt) / 1'000'000ULL));
    if (R_SUCCEEDED(rc))
        g_appCatalogRefreshDelay = 200;
    logHomeState(source, "after");
    return rc;
}

static void requestPowerStateChange(const char* source, bool reboot) {
    Result rc = spsmInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = spsmShutdown(reboot);
        spsmExit();
        if (R_SUCCEEDED(rc))
            return;
    }

    char message[128]{};
    std::snprintf(message, sizeof(message),
                  "[power] %s spsm failed rc=0x%X; using applet fallback",
                  source, rc);
    svcOutputDebugString(message, std::strlen(message));
    if (reboot)
        appletStartRebootSequence();
    else
        appletStartShutdownSequence();
}

static void startPowerSequence(const char* source, smi::SystemMessage action) {
    cancelViewPolling(source);
    takeForegroundFromRunningApp(source);

    // Sleep keeps the filesystem mounted and the daemon alive. Stopping its
    // workers here would leave notifications and metadata caching disabled
    // after wake, which is an unsafe side effect of the fork implementation.
    if (action == smi::SystemMessage::EnterSleep) {
        appletStartSleepSequence(true);
        return;
    }

    g_powerSequenceStarted.store(true);

    // The menu committed its writers before sending this command. The daemon
    // can still write cache/catalog/log data in the small IPC gap, so join all
    // of its background writers and commit once more from this process.
    stopEventManager();
    stopControlCacheWorker();
    if (!switchu::commitSdCard("daemon power handoff")) {
        constexpr const char* failure =
            "[SwitchU-daemon] SD commit failed; power cancelled";
        svcOutputDebugString(failure, std::strlen(failure));
        g_powerSequenceStarted.store(false);
        startControlCacheWorker();
        startEventManager();
        return;
    }

    switch (action) {
        case smi::SystemMessage::Shutdown:
            requestPowerStateChange(source, false);
            break;
        case smi::SystemMessage::Reboot:
            requestPowerStateChange(source, true);
            break;
        default:
            break;
    }
}

static void openMenuFromHome(const char* source) {
    const uint64_t homeStartedAt = armGetSystemTick();
    logHomeState(source, "request");
    cancelViewPolling("home");

    if (g_pendingHomeMenuLaunch) {
        switchu::FileLog::log("[%s] HOME ignored: foreground handoff already pending", source);
        return;
    }

    if (daemon::app::isRunning() && daemon::app::hasForeground()) {
        if (!takeForegroundFromRunningApp(source)) {
            switchu::FileLog::log("[%s] HOME aborted: foreground request failed", source);
            return;
        }

        // appletRequestToGetForeground() only queues the ownership transfer.
        // Creating an AllForeground library applet before AE confirms message 1
        // races qlaunch's own foreground transition and can crash the daemon.
        g_pendingHomeMenuLaunch = true;
        g_pendingHomeMenuSource = source;
        g_pendingHomeMenuStartedAt = homeStartedAt;
        switchu::FileLog::log("[%s] HOME waiting for ChangeIntoForeground", source);
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
        launchPendingHomeMenu();
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
        // The application keeps its IApplicationAccessor across system sleep,
        // but must reacquire the foreground after wake. Keep our session state
        // in sync so the Wakeup path is allowed to call app::resume().
        daemon::app::onHomeSuspend();
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
            } else {
                switchu::FileLog::log("[ae] wake resume OK");
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

static void pumpForegroundAppletMessages() {
    handleGeneralChannel();
    handleAppletMessages();
}

static bool consumeForegroundAppletHomeRequest() {
    if (!g_pendingForegroundAppletHome)
        return false;
    g_pendingForegroundAppletHome = false;
    return true;
}

static Result launchLibraryApplet(AppletId id, const char* name,
                                  const void* inData = nullptr, size_t inDataSize = 0,
                                  u32 libAppletVersion = 0) {
    switchu::FileLog::log("[applet] launching %s id=0x%X version=0x%X in=%zu",
                          name, (u32)id, libAppletVersion, inDataSize);
    Result fgRc = appletRequestToGetForeground();
    switchu::FileLog::log("[applet] %s RequestToGetForeground rc=0x%X", name, fgRc);

    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;
    daemon::LibraryAppletInput input{inData, inDataSize};
    const daemon::LibraryAppletRequest request{
        .id = id,
        .name = name,
        .version = libAppletVersion,
        .pushCommonArgs = libAppletVersion != 0,
        .playStartupSound = true,
        .inputs = inData && inDataSize ? &input : nullptr,
        .inputCount = inData && inDataSize ? 1U : 0U,
    };
    const Result rc = daemon::runLibraryApplet(
        request, pumpForegroundAppletMessages,
        consumeForegroundAppletHomeRequest);
    g_foregroundAppletActive = false;
    return rc;
}

static u32 controllerAppletVersion() {
    if (hosversionAtLeast(11, 0, 0)) return 0x8;
    if (hosversionAtLeast(8, 0, 0)) return 0x7;
    if (hosversionAtLeast(6, 0, 0)) return 0x5;
    if (hosversionAtLeast(3, 0, 0)) return 0x4;
    return 0x3;
}

static Result setupControllerPrivateArg(HidLaControllerSupportArgPrivate& privateArg,
                                        HidLaControllerSupportMode mode,
                                        size_t publicArgSize,
                                        bool homeMenuStyle) {
    privateArg.private_size = sizeof(privateArg);
    privateArg.arg_size = publicArgSize;
    privateArg.flag0 = homeMenuStyle ? 1 : 0;
    privateArg.flag1 = 1;
    privateArg.mode = mode;
    if (hosversionAtLeast(3, 0, 0)) {
        Result setupRc = hidGetSupportedNpadStyleSet(&privateArg.npad_style_set);
        HidNpadJoyHoldType holdType{};
        if (R_SUCCEEDED(setupRc))
            setupRc = hidGetNpadJoyHoldType(&holdType);
        privateArg.npad_joy_hold_type = holdType;
        return setupRc;
    } else {
        privateArg.npad_style_set = 0;
        privateArg.npad_joy_hold_type = HidNpadJoyHoldType_Horizontal;
    }
    return 0;
}

static Result runControllerApplet(const char* name,
                                  HidLaControllerSupportArgPrivate& privateArg,
                                  const void* publicArg,
                                  size_t publicArgSize) {
    HidLaControllerSupportResultInfoInternal output{};
    const daemon::LibraryAppletInput inputs[] = {
        {&privateArg, sizeof(privateArg)},
        {publicArg, publicArgSize},
    };
    daemon::LibraryAppletRequest request{
        .id = AppletId_LibraryAppletController,
        .name = name,
        .version = controllerAppletVersion(),
        .playStartupSound = true,
        .inputs = inputs,
        .inputCount = 2,
        .output = &output,
        .outputSize = sizeof(output),
    };

    appletRequestToGetForeground();
    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;
    Result rc = daemon::runLibraryApplet(
        request, pumpForegroundAppletMessages,
        consumeForegroundAppletHomeRequest);
    g_foregroundAppletActive = false;
    switchu::FileLog::log(
        "[applet] %s output res=0x%X players=%d selected=%u runner=0x%X",
        name, output.res, output.info.player_count, output.info.selected_id, rc);
    if (R_SUCCEEDED(rc) && output.res == 1) {
        switchu::FileLog::log("[applet] %s completed outcome=cancelled", name);
    } else if (R_SUCCEEDED(rc) && output.res != 0) {
         rc = MAKERESULT(Module_Libnx, LibnxError_LibAppletBadExit);
    }
    return rc;
}

static Result launchControllerPairing() {
    switchu::FileLog::log("[applet] launching Controller pairing");
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_max = 8;
    arg.hdr.enable_single_mode = false;

    HidLaControllerSupportArgV3 legacyArg{};
    const void* publicArg = &arg;
    size_t publicArgSize = sizeof(arg);
    if (hosversionBefore(8, 0, 0)) {
        legacyArg.hdr = arg.hdr;
        std::memcpy(legacyArg.identification_color, arg.identification_color,
                    sizeof(legacyArg.identification_color));
        legacyArg.enable_explain_text = arg.enable_explain_text;
        std::memcpy(legacyArg.explain_text, arg.explain_text,
                    sizeof(legacyArg.explain_text));
        legacyArg.hdr.player_count_min = std::min<s8>(legacyArg.hdr.player_count_min, 4);
        legacyArg.hdr.player_count_max = std::min<s8>(legacyArg.hdr.player_count_max, 4);
        publicArg = &legacyArg;
        publicArgSize = sizeof(legacyArg);
    }

    HidLaControllerSupportArgPrivate privateArg{};
    Result rc = setupControllerPrivateArg(
        privateArg, HidLaControllerSupportMode_ShowControllerSupport,
        publicArgSize, true);
    if (R_SUCCEEDED(rc))
        rc = runControllerApplet("Controllers", privateArg, publicArg, publicArgSize);
    if (R_FAILED(rc))
        switchu::FileLog::log("[applet] Controller FAIL: 0x%X", rc);
    else
        switchu::FileLog::log("[applet] Controller pairing done");
    return rc;
}

static Result launchControllerRemapping() {
    if (hosversionBefore(11, 0, 0))
        return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);

    HidLaControllerKeyRemappingArg arg{};
    hidLaCreateControllerKeyRemappingArg(&arg);
    HidLaControllerSupportArgPrivate privateArg{};
    privateArg.controller_support_caller = HidLaControllerSupportCaller_System;
    Result rc = setupControllerPrivateArg(
        privateArg,
        HidLaControllerSupportMode_ShowControllerKeyRemappingForSystem,
        sizeof(arg), false);
    // setup initializes individual fields but deliberately preserves caller.
    privateArg.controller_support_caller = HidLaControllerSupportCaller_System;
    if (R_SUCCEEDED(rc))
        rc = runControllerApplet("ControllerRemapping", privateArg, &arg, sizeof(arg));
    return rc;
}

static Result launchUserProfile(AccountUid uid) {
    FriendsLaArg current{};
    current.hdr.type = FriendsLaArgType_ShowMyProfile;
    current.hdr.uid = uid;

    FriendsLaArgV1 legacy{};
    const void* input = &current;
    size_t inputSize = sizeof(current);
    u32 version = 0x10000;
    if (hosversionBefore(9, 0, 0)) {
        legacy.hdr = current.hdr;
        legacy.data = current.data.common;
        input = &legacy;
        inputSize = sizeof(legacy);
        version = 0x1;
    }

    appletRequestToGetForeground();
    g_foregroundAppletActive = true;
    g_pendingForegroundAppletHome = false;
    const daemon::LibraryAppletInput appletInput{input, inputSize};
    const daemon::LibraryAppletRequest request{
        .id = AppletId_LibraryAppletMyPage,
        .name = "UserProfile",
        .version = version,
        .playStartupSound = true,
        .inputs = &appletInput,
        .inputCount = 1,
    };
    const Result rc = daemon::runLibraryApplet(
        request, pumpForegroundAppletMessages,
        consumeForegroundAppletHomeRequest);
    g_foregroundAppletActive = false;
    return rc;
}

static void pushCommandResult(uint64_t requestId, Result result, const char* command) {
    const smi::CommandHeader response{
        smi::kCommandMagic,
        static_cast<uint32_t>(result),
        requestId,
    };
    AppletStorage storage{};
    Result rc = appletCreateStorage(&storage, sizeof(response));
    const bool created = R_SUCCEEDED(rc);
    if (created)
        rc = appletStorageWrite(&storage, 0, &response, sizeof(response));
    if (R_SUCCEEDED(rc)) {
        daemon::menu_la::pushStorage(&storage);
        return;
    }
    if (created)
        appletStorageClose(&storage);
    switchu::FileLog::log(
        "[smi] %s response create FAIL: 0x%X", command, rc);
}

static void handleMenuCommand() {
    if (!daemon::menu_la::isActive()) return;

    AppletStorage st;
    if (R_FAILED(daemon::menu_la::popStorage(&st))) return;

    smi::StorageReader reader(st);
    if (!reader.valid()) return;

    auto msg = reader.systemMessage();
    const uint64_t requestId = reader.requestId();
    switchu::FileLog::log("[smi] command=%u", (u32)msg);

    Result result = 0;

    switch (msg) {
    case smi::SystemMessage::LaunchApplication: {
        auto args = reader.pop<smi::LaunchAppArgs>();
        daemon::SystemAction action{};
        action.type = daemon::SystemActionType::LaunchApplication;
        action.requestId = requestId;
        action.titleId = args.title_id;
        std::memcpy(&action.uid, args.user_uid, sizeof(action.uid));
        result = g_actionQueue.enqueue(action);
        recordOperationResult(requestId, msg, result, args.title_id);
        switchu::FileLog::log("[smi] queued launch 0x%016lX (actions=%zu)", args.title_id, g_actionQueue.size());
        break;
    }

    case smi::SystemMessage::ResumeApplication:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::ResumeApplication;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result,
                                  daemon::app::suspendedTitleId());
        }
        switchu::FileLog::logCommit("[smi] queued resume (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::TerminateApplication:
        {
        const uint64_t terminatingTitleId = daemon::app::suspendedTitleId();
        result = daemon::app::terminate();
        recordOperationResult(requestId, msg, result, terminatingTitleId);
        if (R_SUCCEEDED(result)) {
            pushNotification(smi::MenuMessage::ApplicationExited);
        } else {
            pushNotification(smi::MenuMessage::OperationFailed,
                             terminatingTitleId,
                             static_cast<uint32_t>(result));
        }
        }
        break;

    case smi::SystemMessage::LaunchAlbum:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenAlbum;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued album launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchMiiEditor:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenMiiEditor;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued Mii Editor launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchNetConnect:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenNetConnect;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued NetConnect launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchUserPage: {
        auto args = reader.pop<smi::UserArgs>();
        daemon::SystemAction action{};
        action.type = daemon::SystemActionType::OpenUserPage;
        action.requestId = requestId;
        std::memcpy(&action.uid, args.user_uid, sizeof(action.uid));
        result = g_actionQueue.enqueue(action);
        recordOperationResult(requestId, msg, result);
        switchu::FileLog::log("[smi] queued User Page launch (actions=%zu)", g_actionQueue.size());
        break;
    }

    case smi::SystemMessage::LaunchUserCreator:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenUserCreator;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued user creator launch (actions=%zu)",
                              g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchControllers:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenControllers;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued Controller launch (actions=%zu)", g_actionQueue.size());
        break;

    case smi::SystemMessage::LaunchControllerRemapping:
        {
            daemon::SystemAction action{};
            action.type = daemon::SystemActionType::OpenControllerRemapping;
            action.requestId = requestId;
            result = g_actionQueue.enqueue(action);
            recordOperationResult(requestId, msg, result);
        }
        switchu::FileLog::log("[smi] queued controller remapping (actions=%zu)",
                              g_actionQueue.size());
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

    case smi::SystemMessage::SetManualDateTime: {
        auto args = reader.pop<smi::ManualDateTimeArgs>();
        result = setManualDateTime(args);
        pushCommandResult(requestId, result, "SetManualDateTime");
        return;
    }

    case smi::SystemMessage::SetInternetTimeSync: {
        auto args = reader.pop<smi::InternetTimeSyncArgs>();
        result = setInternetTimeSync(args.enabled != 0);
        pushCommandResult(requestId, result, "SetInternetTimeSync");
        return;
    }

    case smi::SystemMessage::MenuReady:
        switchu::FileLog::log("[smi] menu ready");
        g_lastOperationFailure = {};
        g_batteryRefreshPending.store(true);
        break;

    case smi::SystemMessage::MenuClosing:
        switchu::FileLog::log("[smi] menu closing");
        break;

    }

}

static Result relaunchMenuAfterApplet(const char* appletName) {
    const auto status = buildSystemStatus();
    const Result rc = daemon::menu_la::launch(smi::MenuStartMode::AppletReturn, status);
    switchu::FileLog::log(
        "[action] %s menu restore rc=0x%X module=%u desc=%u running=%d suspended=0x%016lX",
        appletName, rc, R_MODULE(rc), R_DESCRIPTION(rc), status.app_running ? 1 : 0,
        status.suspended_app_id);
    return rc;
}

static bool handleAction(daemon::SystemAction& action) {
    if (daemon::menu_la::hasHolder() || g_foregroundAppletActive)
        return false;

    switchu::FileLog::log("[action] handling type=%u", (u32)action.type);
    switch (action.type) {
        case daemon::SystemActionType::LaunchApplication: {
            Result rc = daemon::app::launch(action.titleId, action.uid);
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchApplication,
                                  rc, action.titleId);
            if (R_FAILED(rc)) {
                switchu::FileLog::log("[action] launch 0x%016lX FAIL: 0x%X", action.titleId, rc);
                daemon::menu_la::launch(smi::MenuStartMode::MainMenu,
                                        buildSystemStatus());
            }
            return true;
        }

        case daemon::SystemActionType::ResumeApplication: {
            switchu::FileLog::logCommit("[action] resume: calling app::resume()");
            Result rc = daemon::app::resume();
            switchu::FileLog::logCommit("[action] resume: app::resume() -> 0x%X", rc);
            recordOperationResult(action.requestId, smi::SystemMessage::ResumeApplication,
                                  rc, daemon::app::suspendedTitleId());
            if (R_FAILED(rc)) {
                switchu::FileLog::log("[action] resume FAIL: 0x%X", rc);
                daemon::menu_la::launch(smi::MenuStartMode::MainMenu,
                                        buildSystemStatus());
            }
            return true;
        }

        case daemon::SystemActionType::OpenAlbum: {
            const u8 albumArg = AlbumLaArg_ShowAllAlbumFilesForHomeMenu;
            Result rc = launchLibraryApplet(AppletId_LibraryAppletPhotoViewer,
                                            "Album",
                                            &albumArg,
                                            sizeof(albumArg),
                                            0x10000);
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchAlbum, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] album FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("Album");
            return true;
        }

        case daemon::SystemActionType::OpenMiiEditor: {
            const auto miiVer = hosversionAtLeast(10, 2, 0) ? 0x4 : 0x3;
            const MiiLaAppletInput in = {
                .version = miiVer,
                .mode = MiiLaAppletMode_ShowMiiEdit,
                .special_key_code = MiiSpecialKeyCode_Normal,
            };
            Result rc = launchLibraryApplet(AppletId_LibraryAppletMiiEdit,
                                            "MiiEditor", &in, sizeof(in));
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchMiiEditor, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Mii Editor FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("MiiEditor");
            return true;
        }

        case daemon::SystemActionType::OpenControllers: {
            Result rc = launchControllerPairing();
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchControllers, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Controllers FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("Controllers");
            return true;
        }

        case daemon::SystemActionType::OpenControllerRemapping: {
            Result rc = launchControllerRemapping();
            recordOperationResult(action.requestId,
                                  smi::SystemMessage::LaunchControllerRemapping, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] Controller remapping FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("ControllerRemapping");
            return true;
        }

        case daemon::SystemActionType::OpenNetConnect: {
            const u32 netType = 1;
            Result rc = launchLibraryApplet(AppletId_LibraryAppletNetConnect,
                                            "NetConnect", &netType,
                                            sizeof(netType), 1);
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchNetConnect, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] NetConnect FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("NetConnect");
            return true;
        }

        case daemon::SystemActionType::OpenUserPage: {
            Result rc = launchUserProfile(action.uid);
            recordOperationResult(action.requestId, smi::SystemMessage::LaunchUserPage, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] User Page FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("UserPage");
            return true;
        }

        case daemon::SystemActionType::OpenUserCreator: {
            switchu::FileLog::log("[applet] launching system user creator");
            appletRequestToGetForeground();
            g_foregroundAppletActive = true;
            g_pendingForegroundAppletHome = false;
            // pselShowUserCreator() first calls accountIsUserRegistrationRequestPermitted(),
            // which acc denies us and turns into 0x6159; drive the applet directly instead.
            PselUiSettings ui{};
            Result rc = pselUiCreate(&ui, PselUiMode_UserCreator);
            if (R_SUCCEEDED(rc))
                rc = pselUiShow(&ui, nullptr);
            if (rc == MAKERESULT(124, 1)) // nn::account::ResultCancelledByUser
                rc = 0;
            g_foregroundAppletActive = false;
            recordOperationResult(action.requestId,
                                  smi::SystemMessage::LaunchUserCreator, rc);
            if (R_FAILED(rc))
                switchu::FileLog::log("[action] user creator FAIL: 0x%X", rc);
            relaunchMenuAfterApplet("UserCreator");
            return true;
        }
    }

    return false;
}

static bool consumeOneAction() {
    if (g_actionQueue.empty() || daemon::menu_la::hasHolder() ||
        g_foregroundAppletActive)
        return false;

    daemon::SystemAction action{};
    if (!g_actionQueue.pop(action))
        return false;
    return handleAction(action);
}

static void mainLoop() {
    handleGeneralChannel();
    handleAppletMessages();
    handleMenuCommand();

    // SPSM terminates the process asynchronously. Do not schedule catalogue,
    // cache or logging work after the power request has begun.
    if (g_powerSequenceStarted.load())
        return;

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
            if (catalogChanged) {
                if (daemon::menu_la::isActive())
                    pushNotification(smi::MenuMessage::AppRecordsChanged);
                else
                    g_catalogChangedWhileMenuAway.store(true);
            }
            didWork = true;
        }
    }
    if (g_controlCacheRefreshPending.load() && g_controlCacheRefreshDelay.load() > 0) {
        --g_controlCacheRefreshDelay;
    } else if (g_controlCacheRefreshPending.exchange(false)) {
        if (daemon::menu_la::isActive())
            pushNotification(smi::MenuMessage::AppRecordsChanged);
        else
            g_catalogChangedWhileMenuAway.store(true);
        didWork = true;
    }
    if (g_eventPollsRemaining > 0 && shouldDeferViewPolling()) {
        g_eventPollCountdown = 20;
    } else if (g_eventPollsRemaining > 0 && --g_eventPollCountdown == 0) {
        bool needFullReload = sendViewFlagsUpdates();
        if (needFullReload) {
            if (daemon::menu_la::isActive())
                pushNotification(smi::MenuMessage::AppRecordsChanged);
            else
                g_catalogChangedWhileMenuAway.store(true);
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
    }

    if (g_catalogChangedWhileMenuAway.load() && daemon::menu_la::isActive()) {
        g_catalogChangedWhileMenuAway.store(false);
        switchu::FileLog::log(
            "[catalog] delivering coalesced refresh after menu relaunch");
        pushNotification(smi::MenuMessage::AppRecordsChanged);
        didWork = true;
    }

    if (g_menuRelaunchCooldown > 0)
        --g_menuRelaunchCooldown;

    if (daemon::menu_la::checkFinished()) {
        const uint64_t runtimeNs = daemon::menu_la::lastRuntimeNs();
        switchu::FileLog::log("[main] menu exited (reason=%d runtime=%lums)",
            (int)daemon::menu_la::exitReason(),
            static_cast<unsigned long>(runtimeNs / 1'000'000ULL));
        if (runtimeNs < 1'000'000'000ULL)
            ++g_menuFastExitCount;
        else
            g_menuFastExitCount = 0;
        if (g_menuFastExitCount >= 3) {
            g_menuRelaunchCooldown = 500;
            switchu::FileLog::log("[main] menu fast-exit guard active count=%d cooldown=%d",
                                  g_menuFastExitCount, g_menuRelaunchCooldown);
            g_menuFastExitCount = 0;
        }
        didWork = true;
    }

    didWork |= consumeOneAction();

    if (daemon::app::checkFinished()) {
        switchu::FileLog::log("[main] app exited");
        if (g_pendingHomeMenuLaunch) {
            switchu::FileLog::log("[main] clearing pending HOME foreground handoff: app exited");
            g_pendingHomeMenuLaunch = false;
            g_pendingHomeMenuSource = nullptr;
            g_pendingHomeMenuStartedAt = 0;
        }
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

static bool mainLoopNeedsFastTick() {
    if (g_powerSequenceStarted.load())
        return false;
    return g_eventPollsRemaining > 0
        || g_appCatalogRefreshPending.load()
        || g_controlCacheRefreshPending.load()
        || g_menuRelaunchCooldown > 0
        || !g_actionQueue.empty();
}

static void waitForMainWork() {
    Waiter waiters[6]{};
    s32 waiterCount = 0;

    if (Event* messageEvent = appletGetMessageEvent())
        waiters[waiterCount++] = waiterForEvent(messageEvent);
    if (g_generalChannelEventReady)
        waiters[waiterCount++] = waiterForEvent(&g_generalChannelEvent);

    waiters[waiterCount++] = waiterForUEvent(&g_mainWakeEvent);

    if (Event* commandEvent = daemon::menu_la::commandEvent())
        waiters[waiterCount++] = waiterForEvent(commandEvent);
    if (Event* menuStateEvent = daemon::menu_la::stateChangedEvent())
        waiters[waiterCount++] = waiterForEvent(menuStateEvent);
    if (Event* appStateEvent = daemon::app::stateChangedEvent())
        waiters[waiterCount++] = waiterForEvent(appStateEvent);

    if (waiterCount == 0) {
        svcSleepThread(1'000'000'000ULL);
        return;
    }

    s32 signalledIndex = -1;
    const u64 timeout = mainLoopNeedsFastTick()
        ? 10'000'000ULL
        : 1'000'000'000ULL;
    static Result s_lastWaitFailure = 0;
    static uint32_t s_waitFailureRepeatCount = 0;
    const Result rc = waitObjects(&signalledIndex, waiters, waiterCount, timeout);
    if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut)) {
        if (rc == s_lastWaitFailure) {
            ++s_waitFailureRepeatCount;
        } else {
            s_lastWaitFailure = rc;
            s_waitFailureRepeatCount = 1;
        }
        if (s_waitFailureRepeatCount <= 3 || (s_waitFailureRepeatCount % 120) == 0) {
            switchu::FileLog::log("[main] waitObjects FAIL: 0x%X count=%u waiters=%d",
                                  rc,
                                  (unsigned)s_waitFailureRepeatCount,
                                  (int)waiterCount);
        }
        // Applet holder or applet-manager events can be rejected by waitObjects
        // while foreground ownership is changing. The next main-loop tick polls
        // all holders and channels again, so keep a conservative backoff instead
        // of turning an invalid waiter into a hot loop.
        svcSleepThread(100'000'000ULL);
    } else {
        s_lastWaitFailure = 0;
        s_waitFailureRepeatCount = 0;
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
            ueventSignal(&g_mainWakeEvent);
        } else if (evIdx == 1 && hasGcEvent) {
            eventClear(&gcMountFailEvent);

            Result failRc = switchu::ns::getLastGameCardMountFailure();
            switchu::FileLog::log("[event] GameCardMountFailure rc=0x%X", failRc);

            g_eventGcMountRc.store(failRc);
            g_eventGcMountFailure.store(true);
            ueventSignal(&g_mainWakeEvent);
        } else if ((hasGcEvent && hasPsmEvent && evIdx == 2) ||
                   (!hasGcEvent && hasPsmEvent && evIdx == 1)) {
            eventClear(&psmSession.StateChangeEvent);
            switchu::FileLog::log("[event] PSM state change fired");
            g_batteryRefreshPending.store(true);
            ueventSignal(&g_mainWakeEvent);
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
            waitSingle(waiterForUEvent(&g_controlCacheWakeEvent), UINT64_MAX);
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
                ueventSignal(&g_mainWakeEvent);
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
    ueventSignal(&g_controlCacheWakeEvent);
    threadWaitForExit(&g_controlCacheThread);
    threadClose(&g_controlCacheThread);
    g_controlCacheStarted = false;
    switchu::FileLog::log("[control-cache] thread stopped");
}

int main(int argc, char* argv[]) {
    switchu::FileLog::log("[daemon] main() entry");

    ueventCreate(&g_mainWakeEvent, true);
    ueventCreate(&g_controlCacheWakeEvent, true);
    Result generalEventRc = appletGetPopFromGeneralChannelEvent(&g_generalChannelEvent);
    g_generalChannelEventReady = R_SUCCEEDED(generalEventRc);
    if (R_FAILED(generalEventRc))
        switchu::FileLog::log("[daemon] general channel event unavailable: 0x%X", generalEventRc);

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
        waitForMainWork();
    }

    stopEventManager();
    stopControlCacheWorker();
    daemon::menu_la::terminate();
    daemon::app::cleanup();
    if (g_generalChannelEventReady) {
        eventClose(&g_generalChannelEvent);
        g_generalChannelEventReady = false;
    }
    switchu::FileLog::log("[daemon] shutdown complete");
    return 0;
}
