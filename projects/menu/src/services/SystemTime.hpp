#pragma once

#include <switch.h>

#include <cstdint>

// libnx seeds its wall clock (gettimeofday, CLOCK_REALTIME) and, through the
// same base, CLOCK_MONOTONIC once at process start from the time service. When
// the menu starts before the time service answers (a HOME return), that base
// stays "unavailable" for the life of the process: the HUD clock never fills
// in, std::chrono::steady_clock reads garbage and curl times every request out
// at once, until the menu restarts.
//
// The menu links with --wrap=clock_gettime so CLOCK_MONOTONIC comes straight
// from the CPU counter (always valid), and this module re-opens the time
// service and re-seeds libnx's wall clock as soon as the service answers.
namespace switchu::services::system_time {

// Nanoseconds since boot from the CPU counter. Never fails.
std::uint64_t uptimeNs();

// Current POSIX time from the user, network or local system clock (first that
// answers). Re-opens the time service (rate-limited) when none does, and
// re-seeds libnx's wall clock whenever it is still unavailable.
Result currentPosixTime(std::uint64_t* out);

// Re-seeds libnx's wall clock from the time service, e.g. after the clock was
// changed or the console woke up. Returns true when the wall clock works.
bool reseedWallClock(const char* reason);

// True when gettimeofday / CLOCK_REALTIME currently work.
bool wallClockAvailable();

// Logs the state of the time service at boot (call once the SD log is open).
void logBootState();

} // namespace switchu::services::system_time
