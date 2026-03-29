#include "AppListLoader.hpp"
#include "core/DebugLog.hpp"
#include <nxui/third_party/stb/stb_image.h>
#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <thread>
#include <vector>
#include <algorithm>
#ifndef SWITCHU_HOMEBREW
#include <nxtc.h>
#include <switchu/ns_ext.hpp>
#endif

namespace {

constexpr int kIconUploadSize   = 256;
constexpr int kMaxIconTextures  = 100;

void decodeIcons(std::vector<PendingApp>& apps) {
    DebugLog::log("[loader] decoding %d icons on worker threads...", (int)apps.size());
    std::atomic<int> nextJob{0};
    auto workerFn = [&]() {
        for (;;) {
            int idx = nextJob.fetch_add(1, std::memory_order_relaxed);
            if (idx >= (int)apps.size()) break;
            auto& a = apps[idx];
            if (a.iconData.empty()) continue;
            int ch;
            uint8_t* full = stbi_load_from_memory(
                a.iconData.data(), (int)a.iconData.size(),
                &a.w, &a.h, &ch, 4);
            a.iconData.clear(); a.iconData.shrink_to_fit();
            if (!full) continue;
            if (a.w > kIconUploadSize || a.h > kIconUploadSize) {
                int dstW = kIconUploadSize, dstH = kIconUploadSize;
                uint8_t* scaled = (uint8_t*)std::malloc((size_t)dstW * dstH * 4);
                if (scaled) {
                    float scaleX = (float)a.w / dstW;
                    float scaleY = (float)a.h / dstH;
                    for (int y = 0; y < dstH; ++y) {
                        float srcYf = (y + 0.5f) * scaleY - 0.5f;
                        int y0 = (int)srcYf; if (y0 < 0) y0 = 0;
                        int y1 = y0 + 1; if (y1 >= a.h) y1 = a.h - 1;
                        float fy = srcYf - y0;
                        for (int x = 0; x < dstW; ++x) {
                            float srcXf = (x + 0.5f) * scaleX - 0.5f;
                            int x0 = (int)srcXf; if (x0 < 0) x0 = 0;
                            int x1 = x0 + 1; if (x1 >= a.w) x1 = a.w - 1;
                            float fx = srcXf - x0;
                            const uint8_t* p00 = full + ((size_t)y0 * a.w + x0) * 4;
                            const uint8_t* p10 = full + ((size_t)y0 * a.w + x1) * 4;
                            const uint8_t* p01 = full + ((size_t)y1 * a.w + x0) * 4;
                            const uint8_t* p11 = full + ((size_t)y1 * a.w + x1) * 4;
                            uint8_t* dst = scaled + ((size_t)y * dstW + x) * 4;
                            for (int c = 0; c < 4; ++c) {
                                dst[c] = (uint8_t)(
                                    p00[c] * (1 - fx) * (1 - fy) +
                                    p10[c] * fx       * (1 - fy) +
                                    p01[c] * (1 - fx) * fy       +
                                    p11[c] * fx       * fy       + 0.5f);
                            }
                        }
                    }
                    stbi_image_free(full);
                    a.rgba = scaled; a.w = dstW; a.h = dstH; a.scaledWithMalloc = true;
                } else {
                    a.rgba = full;
                }
            } else {
                a.rgba = full;
            }
        }
    };
    // plugging your memory leak <3 ~Arch
    workerFn();
    DebugLog::log("[loader] decode done");
}

void uploadAndRegister(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                       std::vector<PendingApp>& apps,
                       GridModel& model,
                       std::vector<nxui::Texture>& outTex) {
    outTex.reserve(std::min((int)apps.size(), kMaxIconTextures));
    for (auto& p : apps) {
        int texIdx = -1;
        if (p.rgba) {
            if ((int)outTex.size() < kMaxIconTextures) {
                nxui::Texture tex;
                if (tex.loadFromPixelsPooled(gpu, ren, p.rgba, p.w, p.h)) {
                    texIdx = (int)outTex.size();
                    outTex.push_back(std::move(tex));
                }
            }
            if (p.scaledWithMalloc) std::free(p.rgba);
            else stbi_image_free(p.rgba);
            p.rgba = nullptr;
        }
        AppEntry entry;
        entry.id           = std::move(p.id);
        entry.title        = std::move(p.title);
        entry.titleId      = p.titleId;
        entry.iconTexIndex = texIdx;
        entry.viewFlags    = p.viewFlags;
        model.addEntry(std::move(entry));
    }
}

}


void AppListLoader::fetchAndDecode() {
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

    decodeIcons(m_pending);
#endif
}


void AppListLoader::load(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                         GridModel& model, std::vector<nxui::Texture>& outTextures) {
    fetchAndDecode();
    uploadAndRegister(gpu, ren, m_pending, model, outTextures);
    m_pending.clear();
    m_pending.shrink_to_fit();
}


void AppListLoader::startAsync(nxui::ThreadPool& pool) {
    if (m_future.valid())
        m_future.get();

    m_future = pool.submit([this]() {
        fetchAndDecode();
    });
}

bool AppListLoader::isReady() const {
    return m_future.valid() &&
           m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void AppListLoader::finalize(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                              GridModel& model, std::vector<nxui::Texture>& outTextures) {
    if (m_future.valid())
        m_future.get();

    uploadAndRegister(gpu, ren, m_pending, model, outTextures);
    m_pending.clear();
    m_pending.shrink_to_fit();
}
