#pragma once

// Shared shelf geometry for Theme Shop browse tabs (Installed / Community / Icons).
// Landscape: 3×2. Portrait top-rail: 2×3. Page size stays 6 either way so
// preview prefetch windows stay in lockstep with the visible shelf.
namespace themeshop::layout {

constexpr int kGridCols = 3;
constexpr int kVisibleRows = 2;
constexpr int kPortraitGridCols = 2;
constexpr int kPortraitVisibleRows = 3;
constexpr int kEntriesPerPage = kGridCols * kVisibleRows;
static_assert(kEntriesPerPage == kPortraitGridCols * kPortraitVisibleRows);

} // namespace themeshop::layout
