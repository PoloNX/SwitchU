#pragma once

// Shared shelf geometry for Theme Shop browse tabs (Installed / Community / Icons).
// Keep UI draw + preview prefetch / trim in lockstep — a 3-col shelf with a
// 2-col prefetch window was leaving covers blank.
namespace themeshop::layout {

constexpr int kGridCols = 3;
constexpr int kVisibleRows = 2;
constexpr int kEntriesPerPage = kGridCols * kVisibleRows;

} // namespace themeshop::layout
