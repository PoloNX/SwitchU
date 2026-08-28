#include "core/WidgetStore.hpp"

#include <array>
#include <cassert>
#include <string>

using switchu::widgets::WidgetSize;
using switchu::widgets::WidgetType;

int main() {
    constexpr std::array types{
        WidgetType::Clock,
        WidgetType::RecentlyPlayed,
        WidgetType::RecentPlaytime,
        WidgetType::RandomScreenshot,
        WidgetType::ImagePin,
        WidgetType::Batteries,
    };

    for (const WidgetType type : types) {
        const auto singleRow = switchu::widgets::supportedSizes(
            type, AppLayoutMode::DynamicLine);
        if (type == WidgetType::RecentlyPlayed || type == WidgetType::RecentPlaytime ||
            type == WidgetType::Batteries ||
            type == WidgetType::RandomScreenshot) {
            assert(singleRow.empty());
            assert((switchu::widgets::validatedSize(
                        type, {2, 1}, AppLayoutMode::DynamicLine) == WidgetSize{0, 0}));
        } else {
            assert(singleRow.size() == 1);
            assert((singleRow.front() == WidgetSize{1, 1}));
            assert((switchu::widgets::validatedSize(
                        type, {2, 2}, AppLayoutMode::DynamicLine) == WidgetSize{1, 1}));
        }
    }

    assert(switchu::widgets::supportedSizes(
               WidgetType::Clock, AppLayoutMode::Grid).size() == 2);
    const auto recentlyPlayed = switchu::widgets::supportedSizes(
        WidgetType::RecentlyPlayed, AppLayoutMode::Grid);
    assert(recentlyPlayed.size() == 1);
    assert((recentlyPlayed.front() == WidgetSize{2, 1}));
    assert(switchu::widgets::supportedSizes(
               WidgetType::RandomScreenshot, AppLayoutMode::Grid).empty());
    assert((switchu::widgets::supportedSizes(
                WidgetType::RecentPlaytime, AppLayoutMode::Grid) ==
            std::vector<WidgetSize>{{2, 1}}));
    assert((switchu::widgets::supportedSizes(
                WidgetType::Batteries, AppLayoutMode::Grid) ==
            std::vector<WidgetSize>{{2, 1}, {2, 2}}));
    assert((switchu::widgets::validatedSize(
                WidgetType::Clock, {2, 2}, AppLayoutMode::Grid) == WidgetSize{1, 1}));
    assert((switchu::widgets::validatedSize(
                WidgetType::ImagePin, {1, 2}, AppLayoutMode::Grid) == WidgetSize{1, 2}));

    for (const std::uint32_t id : {1u, 42u, 0xFFFFFFFFu}) {
        const auto titleId = switchu::widgets::widgetTitleId(id);
        assert(switchu::widgets::isWidgetTitleId(titleId));
        assert(switchu::widgets::widgetIdFromTitleId(titleId) == id);
    }
    assert(!switchu::widgets::isWidgetTitleId(0));
    assert(switchu::widgets::widgetIdFromTitleId(0) == 0);

    WidgetType parsed = WidgetType::Clock;
    assert(switchu::widgets::parseType("image_pin", parsed));
    assert(parsed == WidgetType::ImagePin);
    assert(!switchu::widgets::parseType("unknown", parsed));
}
