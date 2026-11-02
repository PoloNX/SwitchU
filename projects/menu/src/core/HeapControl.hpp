#pragma once

#include <cstddef>
#include <cstdint>

namespace switchu::menu {

// Runtime Applet-heap helpers for Frame Tap donor remount.
// No-ops on homebrew builds and when FT is not driving a shrink.
// Normal (no viewer) SwitchU never calls these.

// Shrink unused heap tail so Applet-pool free can satisfy needMb.
// Only releases process free above a UI reserve — never touches FT-absent paths.
bool tryShrinkHeapForFrameTap(std::uint32_t needMb);

// Prefer releasing at least needMb (+ spare) even if it means a tighter UI floor.
// Used on viewer-prefill so the pool is ready before FT's 2.5s NeedHeadroom window.
bool tryShrinkHeapForFrameTapAggressive(std::uint32_t needMb);

// Grow back toward the boot-time heap size after the viewer leaves.
bool tryRestoreHeapAfterFrameTap();

bool heapShrunkForFrameTap();
std::size_t currentHeapSizeBytes();
std::size_t originalHeapSizeBytes();

} // namespace switchu::menu
