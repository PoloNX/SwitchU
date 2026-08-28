#pragma once

#include "AppLayoutMode.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace switchu::widgets {

enum class WidgetType : std::uint8_t {
    Clock,
    RecentlyPlayed,
    RecentPlaytime,
    RandomScreenshot,
    ImagePin,
    Batteries,
};

struct WidgetSize {
    int columns = 1;
    int rows = 1;

    bool operator==(const WidgetSize&) const = default;
};

struct Widget {
    std::uint32_t id = 0;
    WidgetType type = WidgetType::Clock;
    WidgetSize size;
    std::string assetRef;
    int refreshSeconds = 60;
};

struct RecentActivity {
    std::uint64_t titleId = 0;
    std::string title;
    std::int64_t launchedAt = 0;
    std::int64_t sessionMeasuredAt = 0;
    std::uint64_t recentSeconds = 0;
    std::uint64_t totalSeconds = 0;
};

inline constexpr std::uint64_t kWidgetTitleIdPrefix = 0xF200000000000000ULL;

inline constexpr std::uint64_t widgetTitleId(std::uint32_t widgetId) {
    return kWidgetTitleIdPrefix | static_cast<std::uint64_t>(widgetId);
}

inline constexpr bool isWidgetTitleId(std::uint64_t value) {
    return (value & 0xFFFFFFFF00000000ULL) == kWidgetTitleIdPrefix;
}

inline constexpr std::uint32_t widgetIdFromTitleId(std::uint64_t value) {
    return isWidgetTitleId(value) ? static_cast<std::uint32_t>(value) : 0;
}

const char* typeKey(WidgetType type);
const char* fallbackName(WidgetType type);
bool parseType(const std::string& value, WidgetType& type);
std::vector<WidgetSize> supportedSizes(WidgetType type, AppLayoutMode layout);
WidgetSize validatedSize(WidgetType type, WidgetSize requested, AppLayoutMode layout);

class WidgetStore final {
public:
    static constexpr const char* kPath = "sdmc:/config/SwitchU/widgets/widgets.json";
    static constexpr const char* kTempPath = "sdmc:/config/SwitchU/widgets/widgets.tmp";
    static constexpr const char* kBackupPath = "sdmc:/config/SwitchU/widgets/widgets.bak";
    static constexpr const char* kAssetRoot = "sdmc:/config/SwitchU/widgets/assets";

    bool load();
    bool save() const;

    const std::vector<Widget>& all() const { return m_widgets; }
    Widget* find(std::uint32_t id);
    const Widget* find(std::uint32_t id) const;
    std::uint32_t create(WidgetType type, WidgetSize size,
                         std::string assetRef = {});
    bool remove(std::uint32_t id);
    bool setSize(std::uint32_t id, WidgetSize size);

    const RecentActivity& recentActivity() const { return m_recent; }
    void recordLaunch(std::uint64_t titleId, std::string title,
                      std::int64_t launchedAt);
    void updateRecentDuration(std::int64_t now);
    void setTotalSeconds(std::uint64_t seconds) { m_recent.totalSeconds = seconds; }

private:
    std::vector<Widget> m_widgets;
    RecentActivity m_recent;
    std::uint32_t m_nextId = 1;
};

} // namespace switchu::widgets
