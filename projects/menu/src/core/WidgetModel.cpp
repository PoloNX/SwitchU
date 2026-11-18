#include "WidgetStore.hpp"

#include <algorithm>

namespace switchu::widgets {

const char* typeKey(WidgetType type) {
    switch (type) {
        case WidgetType::Clock: return "clock";
        case WidgetType::RecentlyPlayed: return "recently_played";
        case WidgetType::RecentPlaytime: return "recent_playtime";
        case WidgetType::RandomScreenshot: return "random_screenshot";
        case WidgetType::ImagePin: return "image_pin";
        case WidgetType::Batteries: return "batteries";
    }
    return "clock";
}

const char* fallbackName(WidgetType type) {
    switch (type) {
        case WidgetType::Clock: return "Clock";
        case WidgetType::RecentlyPlayed: return "Recently played";
        case WidgetType::RecentPlaytime: return "Recent playtime";
        case WidgetType::RandomScreenshot: return "Random screenshot";
        case WidgetType::ImagePin: return "Image pin";
        case WidgetType::Batteries: return "Batteries";
    }
    return "Widget";
}

bool parseType(const std::string& value, WidgetType& type) {
    for (WidgetType candidate : {WidgetType::Clock, WidgetType::RecentlyPlayed,
                                 WidgetType::RecentPlaytime, WidgetType::RandomScreenshot,
                                 WidgetType::ImagePin, WidgetType::Batteries}) {
        if (value == typeKey(candidate)) {
            type = candidate;
            return true;
        }
    }
    return false;
}

std::vector<WidgetSize> supportedSizes(WidgetType type, AppLayoutMode layout) {
    if (layout == AppLayoutMode::DynamicLine) {
        if (type == WidgetType::RecentlyPlayed || type == WidgetType::RecentPlaytime ||
            type == WidgetType::RandomScreenshot || type == WidgetType::Batteries)
            return {};
        return {{1, 1}};
    }
    switch (type) {
        case WidgetType::Clock:
            return {{1, 1}, {2, 1}};
        case WidgetType::RecentlyPlayed:
            // The user-facing 1x2 card is one row high and two columns wide.
            return {{2, 1}};
        case WidgetType::RecentPlaytime:
            return {{2, 1}};
        case WidgetType::RandomScreenshot:
            return {};
        case WidgetType::ImagePin:
            return {{1, 1}, {2, 1}, {1, 2}, {2, 2}};
        case WidgetType::Batteries:
            return {{2, 1}, {2, 2}};
    }
    return {{1, 1}};
}

WidgetSize validatedSize(WidgetType type, WidgetSize requested, AppLayoutMode layout) {
    const auto sizes = supportedSizes(type, layout);
    if (sizes.empty()) return {0, 0};
    const auto found = std::find(sizes.begin(), sizes.end(), requested);
    return found == sizes.end() ? sizes.front() : *found;
}

std::vector<WidgetSize> gameTileDemotionChain(WidgetSize preferred) {
    preferred.columns = std::max(1, preferred.columns);
    preferred.rows = std::max(1, preferred.rows);
    // Legacy 3×2 → 2×2 so saved layouts keep a multi-cell tile without wide art.
    if (preferred.columns == 3 && preferred.rows == 2)
        preferred = {2, 2};

    const bool knownGame =
        (preferred.columns == 1 && preferred.rows == 1) ||
        (preferred.columns == 2 &&
         (preferred.rows == 1 || preferred.rows == 2 || preferred.rows == 3)) ||
        (preferred.columns == 4 && preferred.rows == 2);

    std::vector<WidgetSize> chain;
    auto pushUnique = [&](WidgetSize size) {
        if (chain.empty() || !(chain.back() == size))
            chain.push_back(size);
    };

    pushUnique(preferred);
    if (knownGame) {
        if (preferred.columns == 4 && preferred.rows == 2) {
            pushUnique({2, 2});
            pushUnique({2, 1});
        } else if (preferred.columns == 2 && preferred.rows == 3) {
            pushUnique({2, 2});
            pushUnique({2, 1});
        } else if (preferred.columns == 2 && preferred.rows == 2) {
            pushUnique({2, 1});
        }
    } else {
        // Non-game spans (e.g. widget 1×2): shrink the oversized axis next.
        if (preferred.columns > 1 && preferred.rows > 1)
            pushUnique({std::max(1, preferred.columns / 2),
                        std::max(1, preferred.rows / 2)});
        if (preferred.columns > 1)
            pushUnique({1, preferred.rows});
        if (preferred.rows > 1)
            pushUnique({preferred.columns, 1});
    }
    pushUnique({1, 1});
    return chain;
}

WidgetSize demoteGameTileToFit(WidgetSize preferred, int gridColumns, int gridRows) {
    const int cols = std::max(1, gridColumns);
    const int rows = std::max(1, gridRows);
    for (const WidgetSize candidate : gameTileDemotionChain(preferred)) {
        if (candidate.columns <= cols && candidate.rows <= rows)
            return candidate;
    }
    return {1, 1};
}

WidgetSize demoteWidgetTileToFit(WidgetType type, WidgetSize requested,
                                 AppLayoutMode layout,
                                 int gridColumns, int gridRows) {
    const int cols = std::max(1, gridColumns);
    const int rows = std::max(1, gridRows);
    const WidgetSize preferred = validatedSize(type, requested, layout);
    if (preferred.columns == 0 || preferred.rows == 0)
        return preferred;
    if (preferred.columns <= cols && preferred.rows <= rows)
        return preferred;

    auto sizes = supportedSizes(type, layout);
    std::sort(sizes.begin(), sizes.end(), [](const WidgetSize& a, const WidgetSize& b) {
        const int areaA = a.columns * a.rows;
        const int areaB = b.columns * b.rows;
        if (areaA != areaB)
            return areaA > areaB;
        if (a.columns != b.columns)
            return a.columns > b.columns;
        return a.rows > b.rows;
    });
    // Prefer a shrink of the requested face (no axis growth) before a reshape.
    for (const WidgetSize candidate : sizes) {
        if (candidate.columns <= cols && candidate.rows <= rows &&
            candidate.columns <= preferred.columns &&
            candidate.rows <= preferred.rows)
            return candidate;
    }
    for (const WidgetSize candidate : sizes) {
        if (candidate.columns <= cols && candidate.rows <= rows)
            return candidate;
    }
    return {1, 1};
}

} // namespace switchu::widgets
