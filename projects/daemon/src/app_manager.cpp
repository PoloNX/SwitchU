#include "app_manager.hpp"

#include <switchu/control_cache.hpp>
#include <switchu/file_log.hpp>

namespace switchu::daemon::app {
namespace {

constexpr std::uint64_t kGracefulExitTimeoutNs = 15'000'000'000ULL;
constexpr std::uint64_t kShutdownExitTimeoutNs = 5'000'000'000ULL;

bool startupUserRequiresInteractiveSelection(std::uint8_t account, std::uint8_t option) {
    return account == 1 && option == 0;
}

void ensureSaveData(std::uint64_t applicationId,
                    std::uint64_t ownerId,
                    AccountUid userId,
                    FsSaveDataType type,
                    FsSaveDataSpaceId spaceId,
                    std::uint64_t saveSize,
                    std::uint64_t journalSize) {
    if (saveSize == 0)
        return;

    FsSaveDataAttribute attribute{};
    attribute.application_id = applicationId;
    attribute.uid = userId;
    attribute.save_data_type = type;
    attribute.save_data_rank = FsSaveDataRank_Primary;

    FsSaveDataCreationInfo creation{};
    creation.save_data_size = static_cast<s64>(saveSize);
    creation.journal_size = static_cast<s64>(journalSize);
    creation.available_size = 0x4000;
    creation.owner_id = ownerId;
    creation.save_data_space_id = static_cast<u8>(spaceId);

    FsSaveDataMetaInfo meta{};
    meta.size = type == FsSaveDataType_Bcat ? 0 : 0x40060;
    meta.type = type == FsSaveDataType_Bcat ? FsSaveDataMetaType_None
                                            : FsSaveDataMetaType_Thumbnail;

    FsFileSystem filesystem{};
    if (R_SUCCEEDED(fsOpenSaveDataFileSystem(&filesystem, spaceId, &attribute))) {
        fsFsClose(&filesystem);
        return;
    }

    const Result rc = fsCreateSaveDataFileSystem(&attribute, &creation, &meta);
    if (R_FAILED(rc)) {
        switchu::FileLog::log("[app] ensureSaveData type=%d FAIL: 0x%X",
                              static_cast<int>(type), rc);
    }
}

struct LaunchMetadata {
    bool acceptsUser = true;
    bool needsUser = true;
    std::uint8_t startupUserAccount = 1;
    std::uint8_t startupUserAccountOption = 0;
};

LaunchMetadata ensureApplicationSaveData(std::uint64_t titleId, AccountUid uid) {
    LaunchMetadata launch{};
    switchu::control_cache::Meta meta{};
    if (!switchu::control_cache::readMeta(titleId, meta)) {
        switchu::FileLog::log(
            "[app] control cache missing for 0x%016lX; save data not precreated",
            titleId);
        return launch;
    }

    launch.startupUserAccount = meta.startup_user_account;
    launch.startupUserAccountOption = meta.startup_user_account_option;
    launch.acceptsUser = launch.startupUserAccount != 0;
    launch.needsUser = startupUserRequiresInteractiveSelection(
        launch.startupUserAccount, launch.startupUserAccountOption);

    ensureSaveData(titleId, meta.save_data_owner_id, uid,
                   FsSaveDataType_Account, FsSaveDataSpaceId_User,
                   meta.user_account_save_data_size,
                   meta.user_account_save_data_journal_size);

    const AccountUid emptyUid{};
    ensureSaveData(titleId, meta.save_data_owner_id, emptyUid,
                   FsSaveDataType_Device, FsSaveDataSpaceId_User,
                   meta.device_save_data_size,
                   meta.device_save_data_journal_size);
    ensureSaveData(titleId, meta.save_data_owner_id, emptyUid,
                   FsSaveDataType_Temporary, FsSaveDataSpaceId_Temporary,
                   meta.temporary_storage_size, 0);
    ensureSaveData(titleId, meta.save_data_owner_id, emptyUid,
                   FsSaveDataType_Cache, FsSaveDataSpaceId_User,
                   meta.cache_storage_size,
                   meta.cache_storage_journal_size);
    ensureSaveData(titleId, 0x010000000000000C, emptyUid,
                   FsSaveDataType_Bcat, FsSaveDataSpaceId_User,
                   meta.bcat_delivery_cache_storage_size, 0x200000);
    return launch;
}

ApplicationSession g_session;

} // namespace

ApplicationSession::~ApplicationSession() {
    cleanup();
}

bool ApplicationSession::isRunning() const {
    return m_state == SessionState::Started
        || m_state == SessionState::Foreground
        || m_state == SessionState::Suspended
        || m_state == SessionState::Terminating;
}

bool ApplicationSession::hasForeground() const {
    return m_state == SessionState::Foreground;
}

std::uint64_t ApplicationSession::suspendedTitleId() const {
    return isRunning() ? m_titleId : 0;
}

SessionSnapshot ApplicationSession::snapshot() const {
    return {
        .state = m_state,
        .sessionId = m_sessionId,
        .titleId = m_titleId,
        .lastResult = m_lastResult,
        .lastExitReason = m_lastExitReason,
    };
}

Event* ApplicationSession::stateChangedEvent() {
    return appletApplicationActive(&m_application)
        ? &m_application.StateChangedEvent
        : nullptr;
}

void ApplicationSession::closeAccessor() {
    if (appletApplicationActive(&m_application))
        appletApplicationClose(&m_application);
    m_application = {};
}

void ApplicationSession::resetToIdle() {
    closeAccessor();
    m_state = SessionState::Idle;
    m_titleId = 0;
}

Result ApplicationSession::failTransition(Result rc,
                                          const char* stage,
                                          bool applicationStarted) {
    switchu::FileLog::log(
        "[app] session=%lu transition=%s FAIL rc=0x%X started=%d",
        static_cast<unsigned long>(m_sessionId), stage, rc,
        applicationStarted ? 1 : 0);

    if (applicationStarted && appletApplicationActive(&m_application)) {
        m_state = SessionState::Terminating;
        const Result terminateRc = appletApplicationTerminate(&m_application);
        switchu::FileLog::log("[app] rollback terminate rc=0x%X", terminateRc);
        appletApplicationJoin(&m_application);
        m_lastExitReason = appletApplicationGetExitReason(&m_application);
    }

    closeAccessor();
    m_state = SessionState::Error;
    m_lastResult = rc;
    return rc;
}

Result ApplicationSession::stopCurrent(std::uint64_t gracefulTimeoutNs,
                                       const char* reason) {
    if (!appletApplicationActive(&m_application)) {
        resetToIdle();
        return 0;
    }

    m_state = SessionState::Terminating;
    switchu::FileLog::log(
        "[app] session=%lu stop reason=%s title=0x%016lX",
        static_cast<unsigned long>(m_sessionId), reason, m_titleId);

    const Result requestRc = appletApplicationRequestExit(&m_application);
    switchu::FileLog::log("[app] RequestExit rc=0x%X", requestRc);

    Result waitRc = eventWait(&m_application.StateChangedEvent, gracefulTimeoutNs);
    if (waitRc == KERNELRESULT(TimedOut) || R_FAILED(waitRc)) {
        switchu::FileLog::log("[app] graceful exit unavailable rc=0x%X; forcing terminate",
                              waitRc);
        const Result terminateRc = appletApplicationTerminate(&m_application);
        if (R_FAILED(terminateRc)) {
            switchu::FileLog::log("[app] force terminate FAIL rc=0x%X", terminateRc);
            closeAccessor();
            m_state = SessionState::Error;
            m_lastResult = terminateRc;
            m_titleId = 0;
            return terminateRc;
        }
    }

    appletApplicationJoin(&m_application);
    m_lastExitReason = appletApplicationGetExitReason(&m_application);
    switchu::FileLog::log("[app] joined exitReason=%d",
                          static_cast<int>(m_lastExitReason));
    resetToIdle();

    // RequestExit can fail when the application has already started exiting;
    // a successful join still means the session was cleaned up correctly.
    m_lastResult = 0;
    return 0;
}

Result ApplicationSession::launch(std::uint64_t titleId, AccountUid uid) {
    if (appletApplicationActive(&m_application)) {
        const Result stopRc = stopCurrent(kGracefulExitTimeoutNs, "replace-before-launch");
        if (R_FAILED(stopRc))
            return stopRc;
    } else if (m_state != SessionState::Idle) {
        resetToIdle();
    }

    ++m_sessionId;
    m_titleId = titleId;
    m_lastResult = 0;
    m_lastExitReason = AppletApplicationExitReason_Normal;
    switchu::FileLog::log(
        "[app] session=%lu launch title=0x%016lX uid_valid=%d",
        static_cast<unsigned long>(m_sessionId), titleId,
        accountUidIsValid(&uid) ? 1 : 0);

    const Result touchRc = nsTouchApplication(titleId);
    if (R_FAILED(touchRc))
        switchu::FileLog::log("[app] nsTouchApplication non-fatal rc=0x%X", touchRc);

    const LaunchMetadata metadata = ensureApplicationSaveData(titleId, uid);
    Result rc = appletCreateApplication(&m_application, titleId);
    if (R_FAILED(rc))
        return failTransition(rc, "create", false);
    m_state = SessionState::Created;

    struct PreselectedUserArgument {
        u32 magic;
        u8 isSelected;
        u8 padding[3];
        AccountUid uid;
        u8 unused[0x70];
    } userArgument{};
    static_assert(sizeof(userArgument) == 0x88);

    if (metadata.acceptsUser && accountUidIsValid(&uid)) {
        userArgument.magic = 0xC79497CA;
        userArgument.isSelected = 1;
        userArgument.uid = uid;

        AppletStorage storage{};
        rc = appletCreateStorage(&storage, sizeof(userArgument));
        if (R_SUCCEEDED(rc)) {
            rc = appletStorageWrite(&storage, 0, &userArgument, sizeof(userArgument));
            if (R_SUCCEEDED(rc)) {
                rc = appletApplicationPushLaunchParameter(
                    &m_application, AppletLaunchParameterKind_PreselectedUser, &storage);
                // PushLaunchParameter consumes the storage on every result.
            } else {
                appletStorageClose(&storage);
            }
        }
        if (R_FAILED(rc)) {
            switchu::FileLog::log("[app] preselected user ignored rc=0x%X", rc);
            // User preselection is an optimization. The title can still show
            // its own account selector, so this is intentionally non-fatal.
        }
    }

    appletUnlockForeground();
    rc = appletApplicationStart(&m_application);
    if (R_FAILED(rc))
        return failTransition(rc, "start", false);
    m_state = SessionState::Started;

    rc = appletApplicationRequestForApplicationToGetForeground(&m_application);
    if (R_FAILED(rc))
        return failTransition(rc, "request-foreground", true);

    m_state = SessionState::Foreground;
    m_lastResult = 0;
    switchu::FileLog::log("[app] session=%lu foreground title=0x%016lX",
                          static_cast<unsigned long>(m_sessionId), titleId);
    return 0;
}

Result ApplicationSession::resume() {
    if (m_state != SessionState::Suspended && m_state != SessionState::Started)
        return MAKERESULT(Module_Libnx, 0xFE);
    if (!appletApplicationActive(&m_application))
        return failTransition(MAKERESULT(Module_Libnx, 0xFD), "resume-inactive", false);

    appletUnlockForeground();
    const Result rc = appletApplicationRequestForApplicationToGetForeground(&m_application);
    if (R_FAILED(rc)) {
        m_lastResult = rc;
        switchu::FileLog::log("[app] session=%lu resume FAIL rc=0x%X",
                              static_cast<unsigned long>(m_sessionId), rc);
        return rc;
    }

    m_state = SessionState::Foreground;
    m_lastResult = 0;
    return 0;
}

Result ApplicationSession::terminate() {
    if (!isRunning() && !appletApplicationActive(&m_application)) {
        resetToIdle();
        return 0;
    }
    return stopCurrent(kGracefulExitTimeoutNs, "explicit-terminate");
}

Result ApplicationSession::areLibraryAppletsLeft(bool* out) {
    if (!out)
        return MAKERESULT(Module_Libnx, 0xFC);
    *out = false;
    if (!isRunning() || !appletApplicationActive(&m_application))
        return 0;
    return appletApplicationAreAnyLibraryAppletsLeft(&m_application, out);
}

Result ApplicationSession::requestExitLibraryAppletOrTerminate(std::uint64_t timeoutNs) {
    if (!isRunning() || !appletApplicationActive(&m_application))
        return 0;
    return appletApplicationRequestExitLibraryAppletOrTerminate(&m_application, timeoutNs);
}

bool ApplicationSession::checkFinished() {
    if (!isRunning() || !appletApplicationActive(&m_application))
        return false;
    if (!appletApplicationCheckFinished(&m_application))
        return false;

    appletApplicationJoin(&m_application);
    m_lastExitReason = appletApplicationGetExitReason(&m_application);
    switchu::FileLog::log("[app] session=%lu finished reason=%d",
                          static_cast<unsigned long>(m_sessionId),
                          static_cast<int>(m_lastExitReason));
    resetToIdle();
    m_lastResult = 0;
    return true;
}

void ApplicationSession::onHomeSuspend() {
    if (m_state == SessionState::Foreground)
        m_state = SessionState::Suspended;
}

void ApplicationSession::cleanup() {
    if (appletApplicationActive(&m_application))
        stopCurrent(kShutdownExitTimeoutNs, "daemon-shutdown");
    else
        resetToIdle();
}

ApplicationSession& session() { return g_session; }
bool isRunning() { return session().isRunning(); }
bool hasForeground() { return session().hasForeground(); }
std::uint64_t suspendedTitleId() { return session().suspendedTitleId(); }
SessionSnapshot snapshot() { return session().snapshot(); }
Event* stateChangedEvent() { return session().stateChangedEvent(); }
Result launch(std::uint64_t titleId, AccountUid uid) { return session().launch(titleId, uid); }
Result resume() { return session().resume(); }
Result terminate() { return session().terminate(); }
Result areLibraryAppletsLeft(bool* out) { return session().areLibraryAppletsLeft(out); }
Result requestExitLibraryAppletOrTerminate(std::uint64_t timeoutNs) {
    return session().requestExitLibraryAppletOrTerminate(timeoutNs);
}
bool checkFinished() { return session().checkFinished(); }
void onHomeSuspend() { session().onHomeSuspend(); }
void cleanup() { session().cleanup(); }

} // namespace switchu::daemon::app
