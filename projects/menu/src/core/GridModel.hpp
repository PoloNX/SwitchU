#pragma once
#include <nxui/core/Types.hpp>
#include <switchu/ns_ext.hpp>
#include <string>
#include <vector>
#include <utility>


struct AppEntry {
    std::string id;
    std::string title;
    uint64_t    titleId = 0;
    int         iconTexIndex = -1;
    nxui::Color       tint = nxui::Color::white();
    uint32_t    viewFlags = 0;

    bool isGameCard() const {
        return switchu::ns::viewHasFlag(viewFlags, switchu::ns::AppViewFlag_IsGameCard);
    }
    bool isGameCardInserted() const {
        return isGameCard() && switchu::ns::viewHasFlag(viewFlags, switchu::ns::AppViewFlag_IsGameCardInserted);
    }
    bool isGameCardNotInserted() const {
        return isGameCard() && !switchu::ns::viewHasFlag(viewFlags, switchu::ns::AppViewFlag_IsGameCardInserted);
    }
    bool needsVerify() const {
        return switchu::ns::viewHasFlag(viewFlags, switchu::ns::AppViewFlag_NeedsVerify);
    }
    bool canLaunch() const {
        return switchu::ns::viewHasFlag(viewFlags, switchu::ns::AppViewFlag_CanLaunch);
    }
    bool hasContents() const {
        return switchu::ns::viewHasFlag(viewFlags, switchu::ns::AppViewFlag_HasMainContents);
    }
    bool needsUpdate() const {
        return switchu::ns::viewHasFlag(viewFlags, switchu::ns::AppViewFlag_NeedsUpdate);
    }
    bool isLaunchable() const {
        if (viewFlags == 0) return true;
        return canLaunch();
    }

    bool isNotLaunchableGameCard() const {
        return isGameCardNotInserted();
    }
};

class GridModel {
public:
    void clear() { m_entries.clear(); }
    void addEntry(AppEntry e) { m_entries.push_back(std::move(e)); }
    int  count() const { return (int)m_entries.size(); }
    const AppEntry& at(int i) const { return m_entries[i]; }
    AppEntry& at(int i) { return m_entries[i]; }
    const std::vector<AppEntry>& entries() const { return m_entries; }

    bool swapEntries(int a, int b) {
        if (a < 0 || b < 0 || a >= (int)m_entries.size() || b >= (int)m_entries.size())
            return false;
        if (a == b)
            return true;
        std::swap(m_entries[a], m_entries[b]);
        return true;
    }

    const AppEntry* findByTitleId(uint64_t titleId) const {
        for (const auto& e : m_entries)
            if (e.titleId == titleId) return &e;
        return nullptr;
    }

    bool updateViewFlags(uint64_t titleId, uint32_t newFlags) {
        for (auto& e : m_entries) {
            if (e.titleId == titleId) {
                e.viewFlags = newFlags;
                return true;
            }
        }
        return false;
    }

private:
    std::vector<AppEntry> m_entries;
};

