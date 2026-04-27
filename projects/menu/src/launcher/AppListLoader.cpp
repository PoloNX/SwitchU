#include "AppListLoader.hpp"
#include "core/DebugLog.hpp"
#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#ifndef SWITCHU_HOMEBREW
#include <nxtc.h>
#include <switchu/ns_ext.hpp>
#endif

namespace {

void registerEntries(std::vector<PendingApp>& apps,
                     GridModel& model,
                     IconStreamer& streamer) {
    streamer.init((int)apps.size());
    for (int i = 0; i < (int)apps.size(); ++i) {
        auto& p = apps[i];
        streamer.setIconData(i, std::move(p.iconData));
        AppEntry entry;
        entry.id           = std::move(p.id);
        entry.title        = std::move(p.title);
        entry.titleId      = p.titleId;
        entry.iconTexIndex = -1;  // unused — IconStreamer handles textures
        entry.viewFlags    = p.viewFlags;
        model.addEntry(std::move(entry));
    }
}

}


void AppListLoader::fetchApps() {
    char tidBuf[17];
    m_pending.clear();

#ifdef SWITCHU_HOMEBREW
    static const char* dummyNames[] = {
        "The Legend of Zelda: TotK",
        "Super Mario Odyssey",
        "Animal Crossing: NH",
        "Splatoon 3",
        "Mario Kart 8 Deluxe",
        "Super Smash Bros. Ultimate",
        "Pokemon Scarlet",
        "Fire Emblem Engage",
        "Xenoblade Chronicles 3",
        "Metroid Dread",
        "Kirby and the Forgotten Land",
        "Bayonetta 3",
        "Pikmin 4",
        "Luigi's Mansion 3",
        "Hollow Knight",
        "Celeste",
        "Stardew Valley",
        "Hades",
        "Undertale",
        "Minecraft",
    };
    constexpr int kDummyCount = sizeof(dummyNames) / sizeof(dummyNames[0]);
    for (int i = 0; i < kDummyCount; ++i) {
        uint64_t fakeTid = 0x0100000000010000ULL + (uint64_t)i;
        std::snprintf(tidBuf, sizeof(tidBuf), "%016lX", (unsigned long)fakeTid);
        PendingApp a;
        a.id      = tidBuf;
        a.title   = dummyNames[i];
        a.titleId = fakeTid;
        uint32_t flags = (1u << 0) | (1u << 1) | (1u << 8);
        if (i == 5)  flags |= (1u << 6) | (1u << 7);
        if (i == 10) flags = (1u << 6);
        if (i == 15) flags = (1u << 13);
        a.viewFlags = flags;
        m_pending.push_back(std::move(a));
    }
    DebugLog::log("[loader] generated %d dummy apps", kDummyCount);

#else
    NsApplicationRecord records[1024] = {};
    s32 recordCount = 0;
    nsListApplicationRecord(records, 1024, 0, &recordCount);

    static switchu::ns::ExtApplicationView views[1024] = {};
    {
        uint64_t tids[1024];
        for (int i = 0; i < recordCount && i < 1024; ++i)
            tids[i] = records[i].application_id;
        if (recordCount > 0)
            switchu::ns::queryApplicationViews(tids, recordCount, views);
    }

    static NsApplicationControlData controlData;

    m_pending.reserve(recordCount);

    for (int i = 0; i < recordCount; ++i) {
        uint64_t tid = records[i].application_id;
        std::snprintf(tidBuf, sizeof(tidBuf), "%016lX", (unsigned long)tid);

        uint32_t vf = views[i].flags;

        NxTitleCacheApplicationMetadata* meta = nxtcGetApplicationMetadataEntryById(tid);
        if (meta) {
            PendingApp a;
            a.id      = tidBuf;
            a.title   = meta->name ? meta->name : "";
            a.titleId = tid;
            a.viewFlags = vf;
            if (meta->icon_data && meta->icon_size > 0) {
                auto* ptr = static_cast<const uint8_t*>(meta->icon_data);
                a.iconData.assign(ptr, ptr + meta->icon_size);
            }
            m_pending.push_back(std::move(a));
            nxtcFreeApplicationMetadata(&meta);
            continue;
        }

        size_t controlSize = 0;
        Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, tid,
                                                &controlData, sizeof(controlData), &controlSize);
        if (R_FAILED(rc)) continue;

        NacpLanguageEntry* langEntry = nullptr;
        rc = nacpGetLanguageEntry(&controlData.nacp, &langEntry);
        if (R_FAILED(rc) || !langEntry || langEntry->name[0] == '\0') {
            langEntry = nullptr;
            for (int l = 0; l < 16; ++l) {
                NacpLanguageEntry* e = &controlData.nacp.lang[l];
                if (e->name[0] != '\0') { langEntry = e; break; }
            }
        }
        if (!langEntry || langEntry->name[0] == '\0') continue;

        size_t iconSize = controlSize - sizeof(NacpStruct);
        nxtcAddEntry(tid, &controlData.nacp, iconSize,
                     iconSize > 0 ? controlData.icon : nullptr, false);

        PendingApp a;
        a.id      = tidBuf;
        a.title   = langEntry->name;
        a.titleId = tid;
        a.viewFlags = vf;
        if (iconSize > 0)
            a.iconData.assign(controlData.icon, controlData.icon + iconSize);
        m_pending.push_back(std::move(a));
    }
    nxtcFlushCacheFile();
#endif
}


void AppListLoader::load(GridModel& model, IconStreamer& streamer) {
    fetchApps();
    if (m_pendingTransform)
        m_pendingTransform(m_pending);
    registerEntries(m_pending, model, streamer);
    m_pending.clear();
}


void AppListLoader::startAsync(nxui::ThreadPool& pool) {
    if (m_future.valid())
        m_future.get();

    m_future = pool.submit([this]() {
        fetchApps();
    });
}

bool AppListLoader::isReady() const {
    return m_future.valid() &&
           m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void AppListLoader::finalize(GridModel& model, IconStreamer& streamer) {
    if (m_future.valid())
        m_future.get();

    if (m_pendingTransform)
        m_pendingTransform(m_pending);
    registerEntries(m_pending, model, streamer);
    m_pending.clear();
}
