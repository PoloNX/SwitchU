#pragma once
#include <switch.h>
#include <switchu/file_log.hpp>
#include <cstdio>
#include <cstring>

namespace switchu::daemon::app {

static AppletApplication g_app = {};
static bool g_running = false;
static bool g_hasForeground = false;
static uint64_t g_suspendedTitleId = 0;
// Some titles are sensitive to forced save-data precreation; keep this off by default.
static constexpr bool kEnableSaveDataEnsure = false;

inline bool isRunning() { return g_running; }
inline bool hasForeground() { return g_hasForeground; }
inline uint64_t suspendedTitleId() { return g_suspendedTitleId; }

static inline void ensureSaveData(uint64_t app_id, uint64_t owner_id,
                                  AccountUid user_id, FsSaveDataType type,
                                  FsSaveDataSpaceId space_id,
                                  uint64_t save_size, uint64_t journal_size) {
    if (save_size == 0) return;

    FsSaveDataAttribute attr = {};
    attr.application_id = app_id;
    attr.uid = user_id;
    attr.save_data_type = type;
    attr.save_data_rank = FsSaveDataRank_Primary;

    FsSaveDataCreationInfo cr = {};
    cr.save_data_size = (s64)save_size;
    cr.journal_size   = (s64)journal_size;
    cr.available_size = 0x4000;
    cr.owner_id       = owner_id;
    cr.save_data_space_id = (u8)space_id;

    FsSaveDataMetaInfo meta = {};
    if (type == FsSaveDataType_Bcat) {
        meta.size = 0;
        meta.type = FsSaveDataMetaType_None;
    } else {
        meta.size = 0x40060;
        meta.type = FsSaveDataMetaType_Thumbnail;
    }

    FsFileSystem fs;
    if (R_SUCCEEDED(fsOpenSaveDataFileSystem(&fs, space_id, &attr))) {
        fsFsClose(&fs);
    } else {
        Result rc = fsCreateSaveDataFileSystem(&attr, &cr, &meta);
        if (R_FAILED(rc))
            switchu::FileLog::log("[app] ensureSaveData type=%d FAIL: 0x%X", (int)type, rc);
    }
}

static inline void ensureApplicationSaveData(uint64_t title_id, AccountUid uid) {
    NsApplicationControlData* ctrl = new NsApplicationControlData();
    if (!ctrl) return;

    u64 got_size = 0;
    Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                            title_id, ctrl, sizeof(*ctrl), &got_size);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[app] GetControlData FAIL: 0x%X (save data not created)", rc);
        delete ctrl;
        return;
    }

    const NacpStruct& nacp = ctrl->nacp;
    uint64_t owner = nacp.save_data_owner_id;

    ensureSaveData(title_id, owner, uid,
                   FsSaveDataType_Account, FsSaveDataSpaceId_User,
                   nacp.user_account_save_data_size,
                   nacp.user_account_save_data_journal_size);

    AccountUid emptyUid = {};
    ensureSaveData(title_id, owner, emptyUid,
                   FsSaveDataType_Device, FsSaveDataSpaceId_User,
                   nacp.device_save_data_size,
                   nacp.device_save_data_journal_size);

    ensureSaveData(title_id, owner, emptyUid,
                   FsSaveDataType_Temporary, FsSaveDataSpaceId_Temporary,
                   nacp.temporary_storage_size, 0);

    ensureSaveData(title_id, owner, emptyUid,
                   FsSaveDataType_Cache, FsSaveDataSpaceId_User,
                   nacp.cache_storage_size,
                   nacp.cache_storage_journal_size);

    ensureSaveData(title_id, 0x010000000000000C, emptyUid,
                   FsSaveDataType_Bcat, FsSaveDataSpaceId_User,
                   nacp.bcat_delivery_cache_storage_size, 0x200000);

    delete ctrl;
    switchu::FileLog::log("[app] save data ensured for 0x%016lX", title_id);
}

inline Result launch(uint64_t title_id, AccountUid uid) {
    if (g_running) {
        switchu::FileLog::log("[app] closing previous app before launch");
        appletApplicationRequestExit(&g_app);
        appletApplicationJoin(&g_app);
        appletApplicationClose(&g_app);
        g_running = false;
    }

    if (kEnableSaveDataEnsure) {
        ensureApplicationSaveData(title_id, uid);
    } else {
        switchu::FileLog::log("[app] save data precreate skipped for 0x%016lX", title_id);
    }

    Result rc = appletCreateApplication(&g_app, title_id);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[app] CreateApp FAIL: 0x%X", rc);
        return rc;
    }

    struct {
        u32 magic;
        u8  is_selected;
        u8  pad[3];
        AccountUid uid;
        u8  unused[0x70];
    } userArg = {};
    static_assert(sizeof(userArg) == 0x88);

    if (accountUidIsValid(&uid)) {
        userArg.magic       = 0xC79497CA;
        userArg.is_selected = 1;
        userArg.uid         = uid;

        AppletStorage st;
        rc = appletCreateStorage(&st, sizeof(userArg));
        if (R_SUCCEEDED(rc)) {
            Result writeRc = appletStorageWrite(&st, 0, &userArg, sizeof(userArg));
            if (R_SUCCEEDED(writeRc)) {
                Result pushRc = appletApplicationPushLaunchParameter(&g_app,
                    AppletLaunchParameterKind_PreselectedUser, &st);
                if (R_FAILED(pushRc)) {
                    switchu::FileLog::log("[app] PushUser FAIL: 0x%X", pushRc);
                }
            } else {
                switchu::FileLog::log("[app] PushUser storage write FAIL: 0x%X", writeRc);
            }
            appletStorageClose(&st);
        } else {
            switchu::FileLog::log("[app] PushUser storage create FAIL: 0x%X", rc);
        }
    } else {
        switchu::FileLog::log("[app] launch without preselected user (invalid uid)");
    }

    appletUnlockForeground();

    rc = appletApplicationStart(&g_app);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[app] Start FAIL: 0x%X", rc);
        appletApplicationClose(&g_app);
        return rc;
    }

    rc = appletApplicationRequestForApplicationToGetForeground(&g_app);
    if (R_FAILED(rc))
        switchu::FileLog::log("[app] ReqFG FAIL: 0x%X (non-fatal)", rc);

    g_running = true;
    g_hasForeground = true;
    g_suspendedTitleId = title_id;
    switchu::FileLog::log("[app] launched 0x%016lX", title_id);
    return 0;
}

inline Result resume() {
    if (!g_running) return MAKERESULT(Module_Libnx, 0xFE);
    appletUnlockForeground();
    Result rc = appletApplicationRequestForApplicationToGetForeground(&g_app);
    if (R_FAILED(rc))
        switchu::FileLog::log("[app] resume ReqFG FAIL: 0x%X", rc);
    g_hasForeground = true;
    return rc;
}

inline Result terminate() {
    if (!g_running) return 0;
    appletApplicationRequestExit(&g_app);
    appletApplicationJoin(&g_app);
    appletApplicationClose(&g_app);
    g_running = false;
    g_hasForeground = false;
    g_suspendedTitleId = 0;
    return 0;
}

inline bool checkFinished() {
    if (!g_running) return false;
    if (appletApplicationCheckFinished(&g_app)) {
        switchu::FileLog::log("[app] finished (reason=%d)",
            (int)appletApplicationGetExitReason(&g_app));
        appletApplicationJoin(&g_app);
        appletApplicationClose(&g_app);
        g_running = false;
        g_hasForeground = false;
        g_suspendedTitleId = 0;
        return true;
    }
    return false;
}

inline void onHomeSuspend() {
    g_hasForeground = false;
}

inline void cleanup() {
    if (g_running) {
        appletApplicationRequestExit(&g_app);
        appletApplicationJoin(&g_app);
        appletApplicationClose(&g_app);
        g_running = false;
    }
}

}
