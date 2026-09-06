#include "AppListLoader.hpp"
#include "core/DebugLog.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"
#include "smi_commands.hpp"
#include <switch.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <fstream>
#include <iterator>
#ifdef SWITCHU_MENU
#include <switchu/control_cache.hpp>
#include <switchu/ns_ext.hpp>
#endif

namespace {

bool requiresInteractiveUserSelection(uint8_t account, uint8_t option) {
    return account == 1 && option == 0;
}

bool isTitleIdFallback(const std::string& title, uint64_t titleId) {
    char expected[17]{};
    std::snprintf(expected, sizeof(expected), "%016lX",
                  static_cast<unsigned long>(titleId));
    return title == expected;
}

#ifdef SWITCHU_MENU
static constexpr s32 kMaxTrackedApplicationRecords = 1024;
static constexpr s32 kApplicationRecordChunkCount = 30;

bool listApplicationRecords(std::vector<switchu::ns::ExtApplicationRecord>& records) {
    records.clear();

    switchu::ns::ExtApplicationRecord chunk[kApplicationRecordChunkCount] = {};
    s32 offset = 0;
    while (offset < kMaxTrackedApplicationRecords) {
        s32 readCount = 0;
        Result rc = nsListApplicationRecord(
            reinterpret_cast<NsApplicationRecord*>(chunk),
            kApplicationRecordChunkCount,
            offset,
            &readCount);
        if (R_FAILED(rc)) {
            DebugLog::log("[loader] nsListApplicationRecord failed rc=0x%X offset=%d",
                          rc, offset);
            records.clear();
            return false;
        }
        if (readCount <= 0)
            break;

        const s32 remaining = kMaxTrackedApplicationRecords - offset;
        const s32 appendCount = readCount > remaining ? remaining : readCount;
        records.insert(records.end(), chunk, chunk + appendCount);
        offset += readCount;

        if (readCount < kApplicationRecordChunkCount)
            break;
    }

    std::sort(records.begin(), records.end(),
              [](const switchu::ns::ExtApplicationRecord& a,
                 const switchu::ns::ExtApplicationRecord& b) {
                  return a.id < b.id;
              });
    return true;
}

void queryApplicationViews(const std::vector<switchu::ns::ExtApplicationRecord>& records,
                           std::vector<switchu::ns::ExtApplicationView>& views) {
    views.clear();
    if (records.empty())
        return;

    std::vector<uint64_t> tids(records.size());
    for (size_t i = 0; i < records.size(); ++i)
        tids[i] = records[i].id;

    views.resize(records.size());
    Result rc = switchu::ns::queryApplicationViews(
        tids.data(),
        static_cast<int>(tids.size()),
        views.data());
    if (R_FAILED(rc)) {
        DebugLog::log("[loader] queryApplicationViews failed rc=0x%X", rc);
        std::fill(views.begin(), views.end(), switchu::ns::ExtApplicationView{});
    }
}
#endif

bool fetchDaemonCatalog(std::vector<PendingApp>& out, bool prefetchIcons) {
#ifdef SWITCHU_MENU
    std::vector<switchu::menu::smi_cmd::AppEntry> catalog;
    Result rc = switchu::menu::smi_cmd::getAppList(catalog, prefetchIcons);
    if (R_FAILED(rc) || catalog.empty()) {
        DebugLog::log("[loader] daemon catalog unavailable rc=0x%X count=%d",
                      rc, (int)catalog.size());
        return false;
    }

    char tidBuf[17];
    out.clear();
    out.reserve(catalog.size());
    for (auto& ent : catalog) {
        if (ent.titleId == 0 || ent.name.empty())
            continue;

        std::snprintf(tidBuf, sizeof(tidBuf), "%016lX", (unsigned long)ent.titleId);
        PendingApp a;
        a.id      = tidBuf;
        a.title   = std::move(ent.name);
        a.titleId = ent.titleId;
        a.viewFlags = ent.viewFlags;
        a.startupUserKnown = ent.startupUserKnown;
        a.startupUserAccount = ent.startupUserKnown ? ent.startupUserAccount : 1;
        a.startupUserAccountOption = ent.startupUserKnown ? ent.startupUserAccountOption : 0;
        a.userRequired = !ent.startupUserKnown ||
                         requiresInteractiveUserSelection(a.startupUserAccount,
                                                          a.startupUserAccountOption);
        if (prefetchIcons)
            a.iconData = std::move(ent.icon);

        // A freshly rebuilt daemon catalog already carries these fields. Older
        // on-disk catalogs can still contain the title-id fallback, so repair
        // only those incomplete entries without putting every metadata file
        // back on the fast HOME-return path.
        switchu::control_cache::Meta meta{};
        const bool needsMetadata = prefetchIcons || isTitleIdFallback(a.title, ent.titleId)
            || !a.startupUserKnown;
        if (switchu::control_cache::readMeta(ent.titleId, meta)) {
            a.englishTitle = meta.english_name;
            if (needsMetadata && meta.name[0] != '\0')
                a.title = meta.name;
            if (needsMetadata) {
                a.startupUserKnown = true;
                a.startupUserAccount = meta.startup_user_account;
                a.startupUserAccountOption = meta.startup_user_account_option;
                a.userRequired = requiresInteractiveUserSelection(a.startupUserAccount,
                                                                  a.startupUserAccountOption);
                if (prefetchIcons)
                    a.iconData = switchu::control_cache::readIcon(ent.titleId);
            }
        }
        if (a.englishTitle.empty()) a.englishTitle = a.title;

        if (!switchu::control_cache::isValidUtf8(a.title.c_str(), a.title.size() + 1)) {
            DebugLog::log("[loader] invalid UTF-8 title; using title id=%016lX",
                          static_cast<unsigned long>(ent.titleId));
            a.title = tidBuf;
        }

        out.push_back(std::move(a));
    }

    if (out.empty())
        return false;

    DebugLog::log("[loader] loaded %d apps from daemon catalog", (int)out.size());
    return true;
#else
    (void)out;
    (void)prefetchIcons;
    return false;
#endif
}

void registerEntries(std::vector<PendingApp>& apps,
                     GridModel& model,
                     IconStreamer& streamer) {
    streamer.init((int)apps.size());
    streamer.setIconDataLoader(AppListLoader::loadIconData);
    for (int i = 0; i < (int)apps.size(); ++i) {
        auto& p = apps[i];
        streamer.setTitleId(i, p.kind == GridEntryKind::Application ? p.titleId : 0);
        if (!p.iconData.empty())
            streamer.setIconData(i, std::move(p.iconData));
        AppEntry entry;
        entry.id           = std::move(p.id);
        entry.title        = std::move(p.title);
        entry.englishTitle = std::move(p.englishTitle);
        entry.titleId      = p.titleId;
        entry.iconTexIndex = -1;  // unused — IconStreamer handles textures
        entry.viewFlags    = p.viewFlags;
        entry.userRequired = p.userRequired;
        entry.startupUserKnown = p.startupUserKnown;
        entry.startupUserAccount = p.startupUserAccount;
        entry.startupUserAccountOption = p.startupUserAccountOption;
        entry.kind = p.kind;
        entry.folderId = p.folderId;
        entry.folderPreviewCount = p.folderPreviewCount;
        entry.folderColorIndex = p.folderColorIndex;
        entry.widgetId = p.widgetId;
        entry.widgetType = p.widgetType;
        entry.widgetColumns = p.widgetColumns;
        entry.widgetRows = p.widgetRows;
        entry.widgetAssetRef = std::move(p.widgetAssetRef);
        model.addEntry(std::move(entry));
    }
}

}

void AppListLoader::fetchApps(std::vector<PendingApp>& output, bool prefetchIcons) {
    char tidBuf[17];
    output.clear();

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
        a.userRequired = true;
        a.startupUserAccount = 1;
        a.startupUserAccountOption = 0;
        uint32_t flags = (1u << 0) | (1u << 1) | (1u << 8);
        if (i == 5)  flags |= (1u << 6) | (1u << 7);
        if (i == 10) flags = (1u << 6);
        if (i == 15) flags = (1u << 13);
        a.viewFlags = flags;
        output.push_back(std::move(a));
    }
    DebugLog::log("[loader] generated %d dummy apps", kDummyCount);

#else
    if (fetchDaemonCatalog(output, prefetchIcons)) {
        DebugLog::log("[loader] fetched %d apps via daemon catalog",
                      (int)output.size());
        return;
    }

#ifdef SWITCHU_MENU
    DebugLog::log("[loader] daemon catalog required; skipping menu-side app scan");
    return;
#endif

    std::vector<switchu::ns::ExtApplicationRecord> records;
    if (!listApplicationRecords(records))
        return;

    std::vector<switchu::ns::ExtApplicationView> views;
    queryApplicationViews(records, views);

    output.reserve(records.size());

    for (size_t i = 0; i < records.size(); ++i) {
        uint64_t tid = records[i].id;
        std::snprintf(tidBuf, sizeof(tidBuf), "%016lX", (unsigned long)tid);

        uint32_t vf = views[i].flags;

        switchu::control_cache::Meta meta{};
        if (switchu::control_cache::readMeta(tid, meta)) {
            PendingApp a;
            a.id      = tidBuf;
            a.title   = meta.name;
            a.englishTitle = meta.english_name;
            a.titleId = tid;
            a.viewFlags = vf;
            a.startupUserKnown = true;
            a.startupUserAccount = meta.startup_user_account;
            a.startupUserAccountOption = meta.startup_user_account_option;
            a.userRequired = requiresInteractiveUserSelection(a.startupUserAccount,
                                                              a.startupUserAccountOption);
            if (prefetchIcons)
                a.iconData = switchu::control_cache::readIcon(tid);
            output.push_back(std::move(a));
            continue;
        }

        PendingApp a;
        a.id      = tidBuf;
        a.title   = tidBuf;
        a.titleId = tid;
        a.viewFlags = vf;
        a.startupUserKnown = false;
        a.startupUserAccount = 1;
        a.startupUserAccountOption = 0;
        a.userRequired = requiresInteractiveUserSelection(a.startupUserAccount, a.startupUserAccountOption);
        output.push_back(std::move(a));
    }
#endif
    DebugLog::log("[loader] fetched %d apps", (int)output.size());
}


std::vector<uint8_t> AppListLoader::loadIconData(uint64_t titleId) {
    std::vector<uint8_t> iconData;
    if (titleId == 0)
        return iconData;

#ifdef SWITCHU_MENU
    {
        std::ifstream custom(SteamGridDbManager::iconPath(titleId), std::ios::binary);
        if (custom.is_open()) {
            iconData.assign(std::istreambuf_iterator<char>(custom),
                            std::istreambuf_iterator<char>());
            if (!iconData.empty())
                return iconData;
        }
    }
    iconData = switchu::control_cache::readIcon(titleId);
#endif

    return iconData;
}


void AppListLoader::load(GridModel& model, IconStreamer& streamer) {
    try {
        fetchApps(m_pending, m_prefetchIcons);
    } catch (const std::exception& ex) {
        DebugLog::log("[loader] synchronous load failed: %s", ex.what());
        m_pending.clear();
    } catch (...) {
        DebugLog::log("[loader] synchronous load failed: unknown exception");
        m_pending.clear();
    }
    if (m_pendingTransform)
        m_pendingTransform(m_pending);
    registerEntries(m_pending, model, streamer);
    m_pending.clear();
}


void AppListLoader::startAsync(nxui::ThreadPool& pool) {
    if (m_future.valid()) {
        try {
            m_future.get();
        } catch (...) {
            // The shared state below carries task errors. This catch also
            // handles a pool shutdown racing a rejected submission.
        }
    }

    auto state = std::make_shared<AsyncLoadState>();
    m_asyncState = state;
    const bool prefetchIcons = m_prefetchIcons;
    m_future = pool.submit([state, prefetchIcons]() {
        try {
            fetchApps(state->pending, prefetchIcons);
        } catch (...) {
            state->error = std::current_exception();
            state->pending.clear();
        }
    });
}

bool AppListLoader::isReady() const {
    return m_future.valid() &&
           m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool AppListLoader::finalize(GridModel& model, IconStreamer& streamer) {
    if (m_future.valid()) {
        try {
            m_future.get();
        } catch (const std::exception& ex) {
            DebugLog::log("[loader] async task failed: %s", ex.what());
            return false;
        } catch (...) {
            DebugLog::log("[loader] async task failed: unknown exception");
            return false;
        }
    }

    auto state = std::move(m_asyncState);
    if (!state)
        return false;
    if (state->error) {
        try {
            std::rethrow_exception(state->error);
        } catch (const std::exception& ex) {
            DebugLog::log("[loader] async load failed: %s", ex.what());
        } catch (...) {
            DebugLog::log("[loader] async load failed: unknown exception");
        }
        return false;
    }

    m_pending = std::move(state->pending);

    if (m_pendingTransform)
        m_pendingTransform(m_pending);
    registerEntries(m_pending, model, streamer);
    m_pending.clear();
    return true;
}
