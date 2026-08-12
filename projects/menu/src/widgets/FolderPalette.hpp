#pragma once

#include <nxui/core/Types.hpp>
#include <algorithm>

namespace switchu::folders {

// Shared by the folder icon, the folder options screen and the page indicator.
inline nxui::Color colorForIndex(int index) {
    static const nxui::Color kColors[] = {
        {0.22f, 0.68f, 0.86f, 1.f}, // light blue
        {0.28f, 0.82f, 0.31f, 1.f}, // green
        {0.98f, 0.77f, 0.12f, 1.f}, // yellow
        {1.00f, 0.49f, 0.13f, 1.f}, // orange
        {0.93f, 0.28f, 0.30f, 1.f}, // red
        {0.94f, 0.30f, 0.64f, 1.f}, // pink
        {0.57f, 0.29f, 0.88f, 1.f}, // purple
        {0.38f, 0.40f, 0.43f, 1.f}, // grey
    };
    constexpr int kCount = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));
    return kColors[std::clamp(index, 0, kCount - 1)];
}

constexpr int kFolderColorCount = 8;

} // namespace switchu::folders
