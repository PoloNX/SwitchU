#pragma once
// Honor Nintendo's pctl service (PlayGuard / stock parental controls).
// Short-lived sessions only — never hold pctl open across pctlauth.

#include <switch.h>

#include <cstdint>

namespace switchu::pctl_honor {

inline constexpr u32 kCmdIsPlayTimerEnabled             = 1453;
inline constexpr u32 kCmdIsRestrictedByPlayTimer        = 1455;
inline constexpr u32 kCmdStartPlayTimer                 = 1451;
inline constexpr u32 kCmdStopPlayTimer                  = 1452;
inline constexpr u32 kCmdGetPlayTimerEventToRequestSusp = 1457;

struct ScopedSession {
    bool ok = false;

    ScopedSession() {
        ok = R_SUCCEEDED(pctlInitialize());
    }

    ~ScopedSession() {
        if (ok)
            pctlExit();
        ok = false;
    }

    ScopedSession(const ScopedSession&) = delete;
    ScopedSession& operator=(const ScopedSession&) = delete;

    Service* svc() const {
        return ok ? pctlGetServiceSession_Service() : nullptr;
    }
};

inline Result dispatchBool(Service* s, u32 cmd, bool* out) {
    if (!s || !out)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    u8 tmp = 0;
    const Result rc = serviceDispatchOut(s, cmd, tmp);
    if (R_SUCCEEDED(rc))
        *out = tmp != 0;
    return rc;
}

inline Result isPlayTimerEnabled(bool* out) {
    ScopedSession session;
    if (!session.ok)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    return dispatchBool(session.svc(), kCmdIsPlayTimerEnabled, out);
}

inline Result isRestrictedByPlayTimer(bool* out) {
    ScopedSession session;
    if (!session.ok)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    return dispatchBool(session.svc(), kCmdIsRestrictedByPlayTimer, out);
}

inline Result isTemporaryUnlocked(bool* out) {
    ScopedSession session;
    if (!session.ok)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    return pctlIsRestrictionTemporaryUnlocked(out);
}

inline Result isRestrictionEnabled(bool* out) {
    ScopedSession session;
    if (!session.ok)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    return pctlIsRestrictionEnabled(out);
}

inline Result startPlayTimer() {
    ScopedSession session;
    if (!session.ok)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    return serviceDispatch(session.svc(), kCmdStartPlayTimer);
}

inline Result stopPlayTimer() {
    ScopedSession session;
    if (!session.ok)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    return serviceDispatch(session.svc(), kCmdStopPlayTimer);
}

/// True when PlayGuard / stock play timer should block play right now.
inline bool playCurrentlyBlocked() {
    bool temporary = false;
    if (R_SUCCEEDED(isTemporaryUnlocked(&temporary)) && temporary)
        return false;

    bool restricted = false;
    if (R_FAILED(isRestrictedByPlayTimer(&restricted)))
        return false;
    return restricted;
}

/// Launch/resume gate used by menu + daemon.
inline bool shouldBlockApplicationStart() {
    return playCurrentlyBlocked();
}

} // namespace switchu::pctl_honor
