#include "widgets/GridNavigation.hpp"

#include <cassert>
#include <vector>

int main() {

    std::vector<GridNavigationItem> items{
        {0, 0, 0, 1, 1},
        {1, 1, 0, 1, 1},
        {2, 2, 0, 1, 1},
        {3, 1, 1, 2, 1},
        {5, 0, 1, 1, 1},
        {6, 1, 2, 1, 1},
    };
    const int above = findGridNavigationTarget(
        items, 3, GridNavigationDirection::Up);
    assert(above == 1 || above == 2);
    assert(findGridNavigationTarget(items, 3, GridNavigationDirection::Left) == 5);
    assert(findGridNavigationTarget(items, 3, GridNavigationDirection::Down) == 6);

    items.push_back({7, 3, 1, 1, 2});
    assert(findGridNavigationTarget(items, 3, GridNavigationDirection::Right) == 7);

    assert(findGridNavigationTarget(items, 1, GridNavigationDirection::Up) == -1);
}
