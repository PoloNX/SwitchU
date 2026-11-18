#pragma once
#include "core/HomeOrientation.hpp"
#include <cstdint>

namespace switchu::menu {

// Size SwitchU's managed display layer for the current operation mode.
// Docked: toward 1920×1080 with the console's "underscan" TV setting (0..100 —
// qlaunch's "Adjust Screen Size"). Handheld: reset to 1280×720 at (0,0).
//
// initScreenFitLayer stores the layer created in __appInit and applies once.
// applyScreenUnderscan re-reads mode + setsys and resizes the layer (settings
// preview, wake, and live dock/undock).

// Returns true when the layer was initialized successfully.
bool initScreenFitLayer(std::uint64_t managedLayerId);

// Re-reads operation mode + tv.underscan, resizes the managed layer, and when
// docked also applies viSetDisplayMagnification (stock Adjust Screen Size).
// Returns true on success, or when there is nothing to adjust (homebrew / no
// layer). operationMode: <0 = last daemon-known mode, else appletGetOperationMode();
// >=0 = AppletOperationMode (prefer the mode the daemon forwarded — menu libnx
// cache is often stale).
bool applyScreenUnderscan(int operationMode = -1);

// Rotate the default NWindow for Flip-Grip / TATE. UI framebuffer stays
// 1280×720; pair with Input::setDisplayOrientation so touch/sticks match.
bool applyHomeOrientation(HomeOrientation orientation);

} // namespace switchu::menu
