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

} // namespace switchu::widgets
