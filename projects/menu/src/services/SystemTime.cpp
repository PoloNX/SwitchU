#include "services/SystemTime.hpp"

#include "core/DebugLog.hpp"

#include <cerrno>
#include <ctime>
#include <mutex>
#include <sys/time.h>

extern "C" {
// libnx (newlib.o): seeds __boottime from timeGetCurrentTime(__nx_time_type),
// falling back to the user clock, or marks the wall clock unavailable.
void __libnx_init_time(void);
extern TimeType __nx_time_type;

int __real_clock_gettime(clockid_t clockId, struct timespec* tp);

// Linked with -Wl,--wrap=clock_gettime (xmake.lua). libnx derives
// CLOCK_MONOTONIC from the wall clock seeded at boot and fails it (EIO) when
// that seed is missing; libstdc++'s steady_clock ignores the error and curl
// then reads stack garbage as "now". Elapsed CPU ticks are monotonic and need
// no service, so answer CLOCK_MONOTONIC from them.
int __wrap_clock_gettime(clockid_t clockId, struct timespec* tp) {
    if (clockId != CLOCK_MONOTONIC)
        return __real_clock_gettime(clockId, tp);
    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    const std::uint64_t ns = armTicksToNs(armGetSystemTick());
    tp->tv_sec = static_cast<time_t>(ns / 1'000'000'000ULL);
    tp->tv_nsec = static_cast<long>(ns % 1'000'000'000ULL);
    return 0;
}
}

namespace switchu::services::system_time {

namespace {

constexpr std::uint64_t kNsPerSecond = 1'000'000'000ULL;
// A failing time service is retried at most this often (one IPC round each).
constexpr std::uint64_t kRecoveryIntervalNs = 2 * kNsPerSecond;
// An open session whose clocks keep failing this long is re-opened.
constexpr std::uint64_t kReopenAfterNs = 10 * kNsPerSecond;

constexpr TimeType kClockOrder[] = {
    TimeType_UserSystemClock,
    TimeType_NetworkSystemClock,
    TimeType_LocalSystemClock,
};

std::mutex g_mutex;
std::uint64_t g_lastRecoveryNs = 0;
std::uint64_t g_failingSinceNs = 0;
unsigned g_recoveryAttempts = 0;

const char* clockName(TimeType type) {
    switch (type) {
    case TimeType_UserSystemClock:    return "user";
    case TimeType_NetworkSystemClock: return "network";
    case TimeType_LocalSystemClock:   return "local";
    default:                          return "?";
    }
}

bool serviceActive() {
    Service* srv = timeGetServiceSession();
    return srv && serviceIsActive(srv);
}

// First clock that answers. Caller holds g_mutex.
Result readClocksLocked(std::uint64_t* out, TimeType* used) {
    Result rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (!serviceActive())
        return rc;
    for (TimeType type : kClockOrder) {
        u64 value = 0;
        rc = timeGetCurrentTime(type, &value);
        if (R_SUCCEEDED(rc)) {
            *out = value;
            if (used) *used = type;
            return rc;
        }
    }
    return rc;
}

bool wallClockWorks() {
    timeval tv{};
    return ::gettimeofday(&tv, nullptr) == 0;
}

// Caller holds g_mutex and has a working clock of `type`.
bool reseedLocked(TimeType type, const char* reason) {
    const TimeType previous = __nx_time_type;
    __nx_time_type = type;
    __libnx_init_time();
    __nx_time_type = previous;
    const bool ok = wallClockWorks();
    DebugLog::log("[time] wall clock reseed (%s) from %s clock: %s",
                  reason, clockName(type), ok ? "ok" : "still unavailable");
    return ok;
}

// Opens (or re-opens) the time service session. Caller holds g_mutex.
Result reopenServiceLocked(bool forceReopen) {
    if (serviceActive()) {
        if (!forceReopen)
            return 0;
        timeExit();
        if (serviceActive()) // Someone else still holds a reference.
            return 0;
    }
    return timeInitialize();
}

} // namespace

std::uint64_t uptimeNs() {
    return armTicksToNs(armGetSystemTick());
}

bool wallClockAvailable() {
    return wallClockWorks();
}

Result currentPosixTime(std::uint64_t* out) {
    std::lock_guard<std::mutex> lk(g_mutex);
    TimeType used = TimeType_UserSystemClock;
    Result rc = readClocksLocked(out, &used);

    if (R_FAILED(rc)) {
        const std::uint64_t now = uptimeNs();
        if (g_failingSinceNs == 0)
            g_failingSinceNs = now;
        if (g_lastRecoveryNs != 0 && now - g_lastRecoveryNs < kRecoveryIntervalNs)
            return rc;
        g_lastRecoveryNs = now;
        ++g_recoveryAttempts;

        const bool reopen = now - g_failingSinceNs >= kReopenAfterNs;
        const Result initRc = reopenServiceLocked(reopen);
        rc = readClocksLocked(out, &used);
        // Rate-limit the log as well: the first attempts, then every 30th.
        if (g_recoveryAttempts <= 3 || g_recoveryAttempts % 30 == 0 || R_SUCCEEDED(rc)) {
            DebugLog::log("[time] recovery #%u reopen=%d init=0x%X clocks=0x%X",
                          g_recoveryAttempts, reopen ? 1 : 0, initRc, rc);
        }
        if (R_FAILED(rc))
            return rc;
    }

    g_failingSinceNs = 0;
    if (!wallClockWorks()) {
        static std::uint64_t lastReseedNs = 0;
        const std::uint64_t now = uptimeNs();
        if (lastReseedNs == 0 || now - lastReseedNs >= kRecoveryIntervalNs) {
            lastReseedNs = now;
            reseedLocked(used, "time service answered");
        }
    }
    return rc;
}

bool reseedWallClock(const char* reason) {
    std::lock_guard<std::mutex> lk(g_mutex);
    std::uint64_t unused = 0;
    TimeType used = TimeType_UserSystemClock;
    if (R_FAILED(readClocksLocked(&unused, &used)))
        return wallClockWorks();
    return reseedLocked(used, reason);
}

void logBootState() {
    std::lock_guard<std::mutex> lk(g_mutex);
    std::uint64_t value = 0;
    TimeType used = TimeType_UserSystemClock;
    const Result rc = readClocksLocked(&value, &used);
    DebugLog::log("[time] boot: service=%d clocks=0x%X (%s) wall=%d",
                  serviceActive() ? 1 : 0, rc,
                  R_SUCCEEDED(rc) ? clockName(used) : "none",
                  wallClockWorks() ? 1 : 0);
}

} // namespace switchu::services::system_time
