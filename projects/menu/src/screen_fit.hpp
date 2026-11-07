#pragma once
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

// Re-reads operation mode + tv.underscan and resizes the layer. Returns true on
// success, or when there is nothing to adjust (standalone homebrew / no layer).
bool applyScreenUnderscan();

} // namespace switchu::menu
