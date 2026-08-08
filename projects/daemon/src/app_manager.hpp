#pragma once

#include <switch.h>

#include <cstdint>

namespace switchu::daemon::app {

enum class SessionState : std::uint8_t {
    Idle,
    Created,
    Started,
    Foreground,
    Suspended,
    Terminating,
    Error,
};

struct SessionSnapshot {
    SessionState state = SessionState::Idle;
    std::uint64_t sessionId = 0;
    std::uint64_t titleId = 0;
    Result lastResult = 0;
    AppletApplicationExitReason lastExitReason = AppletApplicationExitReason_Normal;
};

// Owns the IApplicationAccessor and is the sole authority for its lifecycle.
// All transitions either commit a valid new state or close the accessor before
// returning an error.
class ApplicationSession final {
public:
    ApplicationSession() = default;
    ~ApplicationSession();

    ApplicationSession(const ApplicationSession&) = delete;
    ApplicationSession& operator=(const ApplicationSession&) = delete;

    Result launch(std::uint64_t titleId, AccountUid uid);
    Result resume();
    Result terminate();
    Result requestExitLibraryAppletOrTerminate(std::uint64_t timeoutNs);
    Result areLibraryAppletsLeft(bool* out);

    bool checkFinished();
    void onHomeSuspend();
    void cleanup();

    bool isRunning() const;
    bool hasForeground() const;
    std::uint64_t suspendedTitleId() const;
    SessionSnapshot snapshot() const;
    Event* stateChangedEvent();

private:
    Result stopCurrent(std::uint64_t gracefulTimeoutNs, const char* reason);
    void closeAccessor();
    void resetToIdle();
    Result failTransition(Result rc, const char* stage, bool applicationStarted);

    AppletApplication m_application{};
    SessionState m_state = SessionState::Idle;
    std::uint64_t m_sessionId = 0;
    std::uint64_t m_titleId = 0;
    Result m_lastResult = 0;
    AppletApplicationExitReason m_lastExitReason = AppletApplicationExitReason_Normal;
};

ApplicationSession& session();

// Compatibility facade used by the daemon while call sites are migrated to
// the session object.
bool isRunning();
bool hasForeground();
std::uint64_t suspendedTitleId();
SessionSnapshot snapshot();
Event* stateChangedEvent();
Result launch(std::uint64_t titleId, AccountUid uid);
Result resume();
Result terminate();
Result areLibraryAppletsLeft(bool* out);
Result requestExitLibraryAppletOrTerminate(std::uint64_t timeoutNs);
bool checkFinished();
void onHomeSuspend();
void cleanup();

} // namespace switchu::daemon::app
