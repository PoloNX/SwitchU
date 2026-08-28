#pragma once

#include <vector>

enum class GridNavigationDirection {
    Left,
    Right,
    Up,
    Down,
};

struct GridNavigationItem {
    int index = -1;
    int column = 0;
    int row = 0;
    int columns = 1;
    int rows = 1;
};

// Returns the nearest selectable grid item in the requested half-plane. A
// multi-cell item is represented once, by its complete occupied rectangle.
int findGridNavigationTarget(const std::vector<GridNavigationItem>& items,
                             int sourceIndex,
                             GridNavigationDirection direction);
