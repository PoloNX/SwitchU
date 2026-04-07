#include "IconStreamer.hpp"
#include "widgets/GlossyIcon.hpp"
#include "core/DebugLog.hpp"
#include <nxui/third_party/stb/stb_image.h>
#include <atomic>
#include <thread>
#include <algorithm>
#include <cstdlib>
#include <cstring>


void IconStreamer::init(int appCount) {
    clear();
    m_compressed.resize(appCount);
    m_appToSlot.assign(appCount, -1);
}

void IconStreamer::setIconData(int appIndex, std::vector<uint8_t> compressed) {
    if (appIndex >= 0 && appIndex < (int)m_compressed.size())
        m_compressed[appIndex] = std::move(compressed);
}

void IconStreamer::clear() {
    m_pool.clear();
    m_compressed.clear();
    m_appToSlot.clear();
    m_freeSlots.clear();
    m_lastPage = -1;
}

// ---------------------------------------------------------------------------
// Decode a single compressed icon to RGBA, downscaling to kIconSize if needed.
// ---------------------------------------------------------------------------
IconStreamer::DecodedIcon IconStreamer::decodeAndScale(int appIndex) const {
    DecodedIcon out{};
    const auto& data = m_compressed[appIndex];
    if (data.empty()) return out;

    int w, h, ch;
    uint8_t* full = stbi_load_from_memory(data.data(), (int)data.size(),
                                           &w, &h, &ch, 4);
    if (!full) return out;

    if (w > kIconSize || h > kIconSize) {
        int dstW = kIconSize, dstH = kIconSize;
        uint8_t* scaled = (uint8_t*)std::malloc((size_t)dstW * dstH * 4);
        if (scaled) {
            float scaleX = (float)w / dstW;
            float scaleY = (float)h / dstH;
            for (int y = 0; y < dstH; ++y) {
                float srcYf = (y + 0.5f) * scaleY - 0.5f;
                int y0 = (int)srcYf; if (y0 < 0) y0 = 0;
                int y1 = y0 + 1;     if (y1 >= h) y1 = h - 1;
                float fy = srcYf - y0;
                for (int x = 0; x < dstW; ++x) {
                    float srcXf = (x + 0.5f) * scaleX - 0.5f;
                    int x0 = (int)srcXf; if (x0 < 0) x0 = 0;
                    int x1 = x0 + 1;     if (x1 >= w) x1 = w - 1;
                    float fx = srcXf - x0;
                    const uint8_t* p00 = full + ((size_t)y0 * w + x0) * 4;
                    const uint8_t* p10 = full + ((size_t)y0 * w + x1) * 4;
                    const uint8_t* p01 = full + ((size_t)y1 * w + x0) * 4;
                    const uint8_t* p11 = full + ((size_t)y1 * w + x1) * 4;
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
            out.rgba = scaled;
            out.w = dstW;
            out.h = dstH;
            out.scaledWithMalloc = true;
        } else {
            out.rgba = full;
            out.w = w;
            out.h = h;
        }
    } else {
        out.rgba = full;
        out.w = w;
        out.h = h;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Core streaming logic.
// ---------------------------------------------------------------------------
void IconStreamer::onPageChanged(int currentPage, int iconsPerPage,
                                 nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                 const std::vector<std::shared_ptr<GlossyIcon>>& allIcons)
{
    if (currentPage == m_lastPage) return;
    m_lastPage = currentPage;

    int totalApps  = (int)m_compressed.size();
    if (totalApps == 0 || iconsPerPage == 0) return;
    int totalPages = (totalApps + iconsPerPage - 1) / iconsPerPage;

    int startPage = std::max(0, currentPage - kPageMargin);
    int endPage   = std::min(totalPages - 1, currentPage + kPageMargin);
    int startApp  = startPage * iconsPerPage;
    int endApp    = std::min(totalApps, (endPage + 1) * iconsPerPage);

    // 1. Evict textures outside the new visible range.
    for (int i = 0; i < (int)m_pool.size(); ++i) {
        int app = m_pool[i]->appIndex;
        if (app >= 0 && (app < startApp || app >= endApp)) {
            if (app < (int)allIcons.size())
                allIcons[app]->setTexture(nullptr);
            m_appToSlot[app] = -1;
            m_pool[i]->appIndex = -1;
            m_freeSlots.push_back(i);
        }
    }

    // 2. Collect apps that need loading.
    std::vector<int> toLoad;
    for (int i = startApp; i < endApp; ++i) {
        if (m_appToSlot[i] < 0 && !m_compressed[i].empty())
            toLoad.push_back(i);
    }

    if (toLoad.empty()) return;

    DebugLog::log("[streamer] page %d: loading %d icons [%d..%d)",
                  currentPage, (int)toLoad.size(), startApp, endApp);

    // 3. Decode icons in parallel (CPU-bound work).
    struct Decoded {
        int appIndex;
        uint8_t* rgba = nullptr;
        int w = 0, h = 0;
        bool scaledWithMalloc = false;
    };
    std::vector<Decoded> decoded(toLoad.size());
    std::atomic<int> nextJob{0};

    auto workerFn = [&]() {
        for (;;) {
            int idx = nextJob.fetch_add(1, std::memory_order_relaxed);
            if (idx >= (int)toLoad.size()) break;
            auto result = decodeAndScale(toLoad[idx]);
            decoded[idx] = {toLoad[idx], result.rgba, result.w, result.h, result.scaledWithMalloc};
        }
    };

    constexpr int NUM_WORKERS = 3;
    std::thread workers[NUM_WORKERS];
    for (int t = 0; t < NUM_WORKERS; ++t) workers[t] = std::thread(workerFn);
    for (int t = 0; t < NUM_WORKERS; ++t) workers[t].join();

    // 4. Upload to GPU (must happen on the main/render thread) and
    //    wire the texture pointers on the corresponding GlossyIcons.

    // Pre-reserve pool capacity so emplace_back() never reallocates.
    // Reallocation would invalidate texture pointers already handed out
    // to GlossyIcon widgets earlier in this loop.
    {
        int newSlots = 0;
        int freeAvail = (int)m_freeSlots.size();
        for (auto& d : decoded) {
            if (!d.rgba) continue;
            if (freeAvail > 0) --freeAvail;
            else ++newSlots;
        }
        m_pool.reserve(m_pool.size() + newSlots);
    }

    for (auto& d : decoded) {
        if (!d.rgba) continue;

        // Acquire a pool slot.
        int poolIdx;
        if (!m_freeSlots.empty()) {
            poolIdx = m_freeSlots.back();
            m_freeSlots.pop_back();
        } else {
            poolIdx = (int)m_pool.size();
            m_pool.emplace_back(std::make_unique<TexSlot>());
        }

        auto& slot = *m_pool[poolIdx];
        if (slot.texture.loadFromPixels(gpu, ren, d.rgba, d.w, d.h)) {
            slot.appIndex = d.appIndex;
            m_appToSlot[d.appIndex] = poolIdx;
            if (d.appIndex < (int)allIcons.size())
                allIcons[d.appIndex]->setTexture(&slot.texture);
        }

        if (d.scaledWithMalloc) std::free(d.rgba);
        else stbi_image_free(d.rgba);
    }
}

void IconStreamer::forceReload(int currentPage, int iconsPerPage,
                                nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::vector<std::shared_ptr<GlossyIcon>>& allIcons)
{
    // Throw away all loaded state so onPageChanged re-does everything.
    for (auto& slot : m_pool) slot->appIndex = -1;
    m_freeSlots.clear();
    for (int i = 0; i < (int)m_pool.size(); ++i) m_freeSlots.push_back(i);
    std::fill(m_appToSlot.begin(), m_appToSlot.end(), -1);

    // Clear the Texture objects themselves (GPU memory + descriptor slots)
    // because a forceReload typically follows a full GPU reset.
    m_pool.clear();
    m_freeSlots.clear();

    m_lastPage = -1;
    onPageChanged(currentPage, iconsPerPage, gpu, ren, allIcons);
}
