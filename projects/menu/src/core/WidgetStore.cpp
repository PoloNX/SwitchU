#include "WidgetStore.hpp"

#include "DebugLog.hpp"
#include <switchu/sd_commit.hpp>
#include <switch.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace switchu::widgets {
namespace {

bool isSafeAssetReference(const std::string& value) {
    if (value.empty()) return true;
    if (value.find("..") != std::string::npos || value.find('\\') != std::string::npos)
        return false;
    return value.rfind("theme:", 0) == 0 || value.rfind("widget:", 0) == 0;
}

} // namespace

bool WidgetStore::load() {
    m_widgets.clear();
    m_recent = {};
    m_nextId = 1;

    std::ifstream file(kPath);
    if (!file.is_open()) {
        DebugLog::log("[widgets] no store yet");
        return true;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& error) {
        DebugLog::log("[widgets] parse failed: %s", error.what());
        return false;
    }

    const auto items = root.find("widgets");
    if (items != root.end() && items->is_array()) {
        for (const auto& item : *items) {
            if (!item.is_object()) continue;
            Widget widget;
            WidgetType type;
            try {
                widget.id = item.value("id", 0u);
                if (!parseType(item.value("type", std::string()), type)) continue;
                widget.type = type;
                widget.size.columns = item.value("columns", 1);
                widget.size.rows = item.value("rows", 1);
                widget.assetRef = item.value("asset", std::string());
                widget.refreshSeconds = std::clamp(item.value("refreshSeconds", 60), 10, 86400);
            } catch (...) {
                continue;
            }
            if (widget.id == 0 || find(widget.id) || !isSafeAssetReference(widget.assetRef))
                continue;
            widget.size = validatedSize(widget.type, widget.size, AppLayoutMode::Grid);
            if (widget.type == WidgetType::ImagePin && widget.assetRef.empty())
                continue;
            m_nextId = std::max(m_nextId, widget.id + 1);
            m_widgets.push_back(std::move(widget));
        }
    }

    const auto activity = root.find("recentActivity");
    if (activity != root.end() && activity->is_object()) {
        try {
            m_recent.titleId = activity->value("titleId", std::uint64_t{0});
            m_recent.title = activity->value("title", std::string());
            m_recent.launchedAt = activity->value("launchedAt", std::int64_t{0});
            m_recent.sessionMeasuredAt = activity->value(
                "sessionMeasuredAt", m_recent.launchedAt);
            m_recent.recentSeconds = activity->value("recentSeconds", std::uint64_t{0});
            m_recent.totalSeconds = activity->value("totalSeconds", std::uint64_t{0});
        } catch (...) {
            m_recent = {};
        }
    }

    DebugLog::log("[widgets] loaded count=%d", static_cast<int>(m_widgets.size()));
    return true;
}

bool WidgetStore::save() const {
    std::error_code ec;
    std::filesystem::create_directories("sdmc:/config/SwitchU/widgets", ec);
    ec.clear();
    std::filesystem::create_directories(kAssetRoot, ec);

    nlohmann::json root;
    root["version"] = 1;
    root["widgets"] = nlohmann::json::array();
    for (const auto& widget : m_widgets) {
        root["widgets"].push_back({
            {"id", widget.id},
            {"type", typeKey(widget.type)},
            {"columns", widget.size.columns},
            {"rows", widget.size.rows},
            {"asset", widget.assetRef},
            {"refreshSeconds", widget.refreshSeconds},
        });
    }
    root["recentActivity"] = {
        {"titleId", m_recent.titleId},
        {"title", m_recent.title},
        {"launchedAt", m_recent.launchedAt},
        {"sessionMeasuredAt", m_recent.sessionMeasuredAt},
        {"recentSeconds", m_recent.recentSeconds},
        {"totalSeconds", m_recent.totalSeconds},
    };

    {
        std::ofstream file(kTempPath, std::ios::trunc);
        if (!file.is_open()) return false;
        file << root.dump(2) << '\n';
        file.flush();
        if (!file.good()) return false;
    }
    if (!switchu::commitSdCard("widgets temp")) return false;

    const bool hadOriginal = std::filesystem::is_regular_file(kPath, ec) && !ec;
    ec.clear();
    if (hadOriginal) {
        std::filesystem::remove(kBackupPath, ec);
        ec.clear();
        std::filesystem::rename(kPath, kBackupPath, ec);
        if (ec) return false;
    }

    std::filesystem::rename(kTempPath, kPath, ec);
    if (ec) {
        if (hadOriginal) {
            std::error_code restoreError;
            std::filesystem::rename(kBackupPath, kPath, restoreError);
        }
        fsdevCommitDevice("sdmc");
        return false;
    }
    if (!switchu::commitSdCard("widgets final")) {
        DebugLog::log("[widgets] final commit failed");
        std::filesystem::remove(kPath, ec);
        if (hadOriginal) {
            ec.clear();
            std::filesystem::rename(kBackupPath, kPath, ec);
        }
        fsdevCommitDevice("sdmc");
        return false;
    }
    if (hadOriginal) {
        std::filesystem::remove(kBackupPath, ec);
        fsdevCommitDevice("sdmc");
    }
    DebugLog::log("[widgets] saved count=%d", static_cast<int>(m_widgets.size()));
    return true;
}

Widget* WidgetStore::find(std::uint32_t id) {
    auto found = std::find_if(m_widgets.begin(), m_widgets.end(),
        [id](const Widget& widget) { return widget.id == id; });
    return found == m_widgets.end() ? nullptr : &*found;
}

const Widget* WidgetStore::find(std::uint32_t id) const {
    auto found = std::find_if(m_widgets.begin(), m_widgets.end(),
        [id](const Widget& widget) { return widget.id == id; });
    return found == m_widgets.end() ? nullptr : &*found;
}

std::uint32_t WidgetStore::create(WidgetType type, WidgetSize size,
                                  std::string assetRef) {
    if (m_widgets.size() >= 64 || !isSafeAssetReference(assetRef)) return 0;
    if (type == WidgetType::ImagePin && assetRef.empty()) return 0;
    Widget widget;
    widget.id = m_nextId++;
    widget.type = type;
    widget.size = validatedSize(type, size, AppLayoutMode::Grid);
    widget.assetRef = std::move(assetRef);
    m_widgets.push_back(std::move(widget));
    return m_widgets.back().id;
}

bool WidgetStore::remove(std::uint32_t id) {
    auto found = std::find_if(m_widgets.begin(), m_widgets.end(),
        [id](const Widget& widget) { return widget.id == id; });
    if (found == m_widgets.end()) return false;
    m_widgets.erase(found);
    return true;
}

bool WidgetStore::setSize(std::uint32_t id, WidgetSize size) {
    Widget* widget = find(id);
    if (!widget) return false;
    const WidgetSize valid = validatedSize(widget->type, size, AppLayoutMode::Grid);
    if (widget->size == valid) return false;
    widget->size = valid;
    return true;
}

void WidgetStore::recordLaunch(std::uint64_t titleId, std::string title,
                               std::int64_t launchedAt) {
    if (titleId == 0) return;
    if (m_recent.titleId != titleId)
        m_recent.totalSeconds = 0;
    m_recent.titleId = titleId;
    m_recent.title = std::move(title);
    m_recent.launchedAt = launchedAt;
    m_recent.sessionMeasuredAt = launchedAt;
    m_recent.recentSeconds = 0;
}

void WidgetStore::updateRecentDuration(std::int64_t now) {
    if (m_recent.sessionMeasuredAt <= 0 || now <= m_recent.sessionMeasuredAt) return;
    constexpr std::uint64_t kMaximumSessionSeconds = 7u * 24u * 60u * 60u;
    const std::uint64_t elapsed = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(now - m_recent.sessionMeasuredAt),
        kMaximumSessionSeconds);
    m_recent.recentSeconds += elapsed;
    m_recent.totalSeconds += elapsed;
    // A session is measured once when SwitchU regains control. Resuming or
    // relaunching records a new measurement start.
    m_recent.sessionMeasuredAt = 0;
}

} // namespace switchu::widgets
