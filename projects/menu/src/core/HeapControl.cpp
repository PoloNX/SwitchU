#include "HeapControl.hpp"

#include "DebugLog.hpp"

#include <algorithm>
#include <switch.h>

#ifndef SWITCHU_HOMEBREW
extern "C" size_t g_switchuHeapSize;
// libnx newlib heap bounds (defined in libnx crt / runtime).
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;
#endif

namespace switchu::menu {
namespace {

#ifndef SWITCHU_HOMEBREW
constexpr std::size_t kHeapAlign = 0x200000; // 2 MiB — Horizon heap alignment
// Keep enough process free after shrink for clocks/menus/focus icon.
constexpr std::uint64_t kUiReserveBytes = 12ull * 1024ull * 1024ull;
constexpr std::uint64_t kUiReserveAggressiveBytes = 6ull * 1024ull * 1024ull;
// Extra Applet-pool cushion beyond GetDonorCap so FT's map isn't edge-tight.
constexpr std::uint64_t kDonorSpareBytes = 8ull * 1024ull * 1024ull;
constexpr std::size_t kMinHeapNormal = 224u * 1024u * 1024u;
// Viewer-prefill may go leaner so Applet-pool free hits ~42 MB before FT asks.
constexpr std::size_t kMinHeapAggressive = 192u * 1024u * 1024u;

std::size_t g_originalHeapSize = 0;
bool g_heapShrunkForFt = false;

std::size_t alignDownHeap(std::size_t bytes) {
    return bytes & ~(kHeapAlign - 1);
}

std::size_t alignUpHeap(std::size_t bytes) {
    return (bytes + kHeapAlign - 1) & ~(kHeapAlign - 1);
}

bool readProcessMem(std::uint64_t& total, std::uint64_t& used) {
    total = 0;
    used = 0;
    if (R_FAILED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
        return false;
    if (R_FAILED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)))
        return false;
    return total > 0;
}

bool shrinkHeapOnce(std::uint32_t needMb, std::uint64_t uiReserve, std::size_t minHeap,
                    const char* tag) {
    if (needMb == 0)
        needMb = 42;
    if (g_switchuHeapSize == 0)
        return false;
    if (g_originalHeapSize == 0)
        g_originalHeapSize = g_switchuHeapSize;
    if (g_heapShrunkForFt && g_switchuHeapSize <= minHeap)
        return true;

    std::uint64_t total = 0, used = 0;
    if (!readProcessMem(total, used) || total <= used)
        return false;

    const std::uint64_t freeBytes = total - used;
    if (freeBytes <= uiReserve) {
        DebugLog::log("[ft-heap] %s skip: free=%lluKB <= uiReserve=%lluKB heap=%zuKB total=%lluKB used=%lluKB",
                      tag,
                      static_cast<unsigned long long>(freeBytes / 1024ull),
                      static_cast<unsigned long long>(uiReserve / 1024ull),
                      g_switchuHeapSize / 1024u,
                      static_cast<unsigned long long>(total / 1024ull),
                      static_cast<unsigned long long>(used / 1024ull));
        return false;
    }

    const std::uint64_t wantRelease =
        static_cast<std::uint64_t>(needMb) * 1024ull * 1024ull + kDonorSpareBytes;
    const std::uint64_t maxRelease = freeBytes - uiReserve;
    const std::uint64_t release = std::min(wantRelease, maxRelease);
    if (release < 16ull * 1024ull * 1024ull) {
        DebugLog::log("[ft-heap] %s skip: release=%lluKB too small (need=%uMB free=%lluKB)",
                      tag, static_cast<unsigned long long>(release / 1024ull), needMb,
                      static_cast<unsigned long long>(freeBytes / 1024ull));
        return false;
    }

    const std::size_t heapBefore = g_switchuHeapSize;
    std::size_t target = alignDownHeap(heapBefore - static_cast<std::size_t>(release));
    if (target < minHeap)
        target = minHeap;
    const std::uint64_t heapUsedApprox =
        heapBefore > freeBytes ? static_cast<std::uint64_t>(heapBefore) - freeBytes : 0;
    const std::size_t minByUsed = alignUpHeap(static_cast<std::size_t>(heapUsedApprox + uiReserve));
    if (target < minByUsed)
        target = minByUsed;

    if (target >= heapBefore) {
        DebugLog::log("[ft-heap] %s skip: target=%zuKB >= heap=%zuKB (total=%lluKB used=%lluKB free=%lluKB need=%uMB)",
                      tag, target / 1024u, heapBefore / 1024u,
                      static_cast<unsigned long long>(total / 1024ull),
                      static_cast<unsigned long long>(used / 1024ull),
                      static_cast<unsigned long long>(freeBytes / 1024ull),
                      needMb);
        return false;
    }

    void* address = nullptr;
    const Result rc = svcSetHeapSize(&address, target);
    if (R_FAILED(rc) || !address) {
        DebugLog::log("[ft-heap] %s svcSetHeapSize(%zuKB) failed rc=0x%X (heap was %zuKB)",
                      tag, target / 1024u, rc, heapBefore / 1024u);
        return false;
    }

    g_switchuHeapSize = target;
    fake_heap_start = static_cast<char*>(address);
    fake_heap_end = static_cast<char*>(address) + target;

    std::uint64_t total2 = 0, used2 = 0;
    readProcessMem(total2, used2);
    // Soak 2026-10-31: svcSetHeapSize often leaves process total unchanged, so
    // Applet-pool free never moves — only a leaner *boot* heap helps FT.
    const bool poolReturned = total2 + (64ull * 1024ull) < total;
    if (poolReturned)
        g_heapShrunkForFt = true;
    DebugLog::log("[ft-heap] %s need=%uMB heap %zuKB -> %zuKB (total %lluKB -> %lluKB, free %lluKB -> %lluKB)%s",
                  tag, needMb, heapBefore / 1024u, target / 1024u,
                  static_cast<unsigned long long>(total / 1024ull),
                  static_cast<unsigned long long>(total2 / 1024ull),
                  static_cast<unsigned long long>(freeBytes / 1024ull),
                  static_cast<unsigned long long>((total2 > used2 ? total2 - used2 : 0) / 1024ull),
                  poolReturned ? "" : " [WARN total unchanged — Applet pool not freed]");
    return poolReturned;
}
#endif

} // namespace

bool heapShrunkForFrameTap() {
#ifdef SWITCHU_HOMEBREW
    return false;
#else
    return g_heapShrunkForFt;
#endif
}

std::size_t currentHeapSizeBytes() {
#ifdef SWITCHU_HOMEBREW
    return 0;
#else
    return g_switchuHeapSize;
#endif
}

std::size_t originalHeapSizeBytes() {
#ifdef SWITCHU_HOMEBREW
    return 0;
#else
    return g_originalHeapSize ? g_originalHeapSize : g_switchuHeapSize;
#endif
}

bool tryShrinkHeapForFrameTap(std::uint32_t needMb) {
#ifdef SWITCHU_HOMEBREW
    (void)needMb;
    return false;
#else
    // Process free ≈ unused heap tail. Shrink relative to g_switchuHeapSize,
    // never TotalMemorySize (non-heap mappings made the first soak skip always).
    return shrinkHeapOnce(needMb, kUiReserveBytes, kMinHeapNormal, "SHRINK");
#endif
}

bool tryShrinkHeapForFrameTapAggressive(std::uint32_t needMb) {
#ifdef SWITCHU_HOMEBREW
    (void)needMb;
    return false;
#else
    if (shrinkHeapOnce(needMb, kUiReserveAggressiveBytes, kMinHeapAggressive, "SHRINK-AGG"))
        return true;
    return shrinkHeapOnce(needMb, kUiReserveBytes, kMinHeapNormal, "SHRINK");
#endif
}

bool tryRestoreHeapAfterFrameTap() {
#ifdef SWITCHU_HOMEBREW
    return false;
#else
    if (!g_heapShrunkForFt)
        return false;

    const std::size_t want = g_originalHeapSize ? g_originalHeapSize : g_switchuHeapSize;
    if (want <= g_switchuHeapSize) {
        g_heapShrunkForFt = false;
        return false;
    }

    // Step down from original if the Applet pool cannot grant the full size.
    static constexpr std::size_t kRestoreLadder[] = {
        416u * 1024u * 1024u,
        400u * 1024u * 1024u,
        384u * 1024u * 1024u,
        368u * 1024u * 1024u,
        352u * 1024u * 1024u,
        320u * 1024u * 1024u,
        288u * 1024u * 1024u,
        256u * 1024u * 1024u,
        224u * 1024u * 1024u,
    };

    void* address = nullptr;
    std::size_t granted = 0;
    if (R_SUCCEEDED(svcSetHeapSize(&address, want)) && address) {
        granted = want;
    } else {
        for (std::size_t requested : kRestoreLadder) {
            if (requested <= g_switchuHeapSize || requested > want)
                continue;
            if (R_SUCCEEDED(svcSetHeapSize(&address, requested)) && address) {
                granted = requested;
                break;
            }
        }
    }

    if (granted == 0 || !address) {
        DebugLog::log("[ft-heap] restore failed (want=%zuKB current=%zuKB)",
                      want / 1024u, g_switchuHeapSize / 1024u);
        return false;
    }

    g_switchuHeapSize = granted;
    fake_heap_start = static_cast<char*>(address);
    fake_heap_end = static_cast<char*>(address) + granted;
    g_heapShrunkForFt = false;
    DebugLog::log("[ft-heap] RESTORE -> %zuKB (original=%zuKB)",
                  granted / 1024u, want / 1024u);
    return true;
#endif
}

} // namespace switchu::menu
