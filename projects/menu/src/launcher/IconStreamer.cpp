#include "IconStreamer.hpp"
#include "widgets/GlossyIcon.hpp"
#include "core/DebugLog.hpp"
#include "steamgriddb/ArtworkCache.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"
#include <nxui/third_party/stb/stb_image.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <unordered_map>

namespace {

constexpr int kPosterFitMaxW = 320;
constexpr int kPosterFitMaxH = 480;

IconStreamer::DecodedIcon decodePosterFromCache(std::uint64_t titleId) {
    IconStreamer::DecodedIcon out{};
    if (titleId == 0 || !SteamGridDbManager::hasPoster(titleId))
        return out;
    auto decoded = steamgriddb::artwork::decodeFit(
        SteamGridDbManager::posterPath(titleId), kPosterFitMaxW, kPosterFitMaxH);
    if (decoded.rgba.empty())
        return out;
    out.rgba = std::move(decoded.rgba);
    out.w = decoded.width;
    out.h = decoded.height;
    return out;
}

} // namespace


void IconStreamer::init(int appCount) {
    clear();
    m_compressed.resize(appCount);
    m_titleIds.assign(appCount, 0);
    m_appToSlot.assign(appCount, -1);
    m_artStale.assign(static_cast<std::size_t>(appCount), 0);
}

void IconStreamer::setIconDataLoader(IconDataLoader loader) {
    m_iconLoader = std::move(loader);
}

void IconStreamer::softInvalidateArt() {
    for (auto& bytes : m_compressed)
        bytes.clear();
    cancelPending();
    m_pendingDecodes.clear();
    m_failedTitleIds.clear();
    m_artStale.assign(m_appToSlot.size(), 1);
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

bool IconStreamer::setPreferPosterArt(bool preferPoster) {
    if (m_preferPosterArt == preferPoster)
        return false;
    m_preferPosterArt = preferPoster;
    softInvalidateArt();
    return true;
}

bool IconStreamer::isArtStale(int appIndex) const {
    return appIndex >= 0
        && appIndex < static_cast<int>(m_artStale.size())
        && m_artStale[static_cast<std::size_t>(appIndex)] != 0;
}

void IconStreamer::setTitleId(int appIndex, uint64_t titleId) {
    if (appIndex >= 0 && appIndex < (int)m_titleIds.size())
        m_titleIds[appIndex] = titleId;
}

void IconStreamer::setIconData(int appIndex, std::vector<uint8_t> compressed) {
    if (appIndex >= 0 && appIndex < (int)m_compressed.size())
        m_compressed[appIndex] = std::move(compressed);
}

void IconStreamer::resize(int appCount) {
    if (appCount < 0)
        appCount = 0;

    for (auto& slot : m_pool) {
        if (slot && slot->appIndex >= appCount)
            slot->appIndex = -1;
    }

    m_compressed.resize(appCount);
    m_titleIds.resize(appCount, 0);
    m_appToSlot.resize(appCount, -1);
    m_artStale.resize(static_cast<std::size_t>(appCount), 0);
    if (m_pinnedIndex >= appCount)
        m_pinnedIndex = -1;
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::setPinnedIndex(int appIndex) {
    m_pinnedIndex = (appIndex >= 0 && appIndex < (int)m_appToSlot.size()) ? appIndex : -1;
}

void IconStreamer::clearPinnedIndex() {
    m_pinnedIndex = -1;
}

void IconStreamer::clear() {
    cancelShadowPrefetch();
    cancelPending();
    m_pool.clear();
    m_compressed.clear();
    m_titleIds.clear();
    m_appToSlot.clear();
    m_artStale.clear();
    m_freeSlots.clear();
    m_pendingDecodes.clear();
    m_failedTitleIds.clear();
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    m_pinnedIndex = -1;
    m_preferPosterArt = false;
}

void IconStreamer::cancelPending() {
    for (auto& pending : m_pendingDecodes) {
        if (pending.state)
            pending.state->cancelled.store(true);
    }
}

void IconStreamer::cancelPendingExcept(std::uint64_t keepTitleId) {
    for (auto& pending : m_pendingDecodes) {
        if (pending.titleId == keepTitleId)
            continue;
        if (pending.state)
            pending.state->cancelled.store(true);
    }
}

bool IconStreamer::swapIndices(int a, int b) {
    if (a < 0 || b < 0 || a >= (int)m_appToSlot.size() || b >= (int)m_appToSlot.size())
        return false;
    if (a == b)
        return true;

    if (a < (int)m_compressed.size() && b < (int)m_compressed.size())
        std::swap(m_compressed[a], m_compressed[b]);
    if (a < (int)m_titleIds.size() && b < (int)m_titleIds.size())
        std::swap(m_titleIds[a], m_titleIds[b]);

    if (a < (int)m_appToSlot.size() && b < (int)m_appToSlot.size()) {
        int slotA = m_appToSlot[a];
        int slotB = m_appToSlot[b];
        std::swap(m_appToSlot[a], m_appToSlot[b]);

        if (slotA >= 0 && slotA < (int)m_pool.size())
            m_pool[slotA]->appIndex = b;
        if (slotB >= 0 && slotB < (int)m_pool.size())
            m_pool[slotB]->appIndex = a;
    }

    if (m_pinnedIndex == a)
        m_pinnedIndex = b;
    else if (m_pinnedIndex == b)
        m_pinnedIndex = a;

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    return true;
}

void IconStreamer::reconcileTitleIds(const std::vector<uint64_t>& titleIds) {
    const std::vector<uint64_t> oldTitleIds = m_titleIds;
    const int oldPinnedIndex = m_pinnedIndex;
    const uint64_t pinnedTitle = oldPinnedIndex >= 0 && oldPinnedIndex < (int)oldTitleIds.size()
        ? oldTitleIds[(size_t)oldPinnedIndex] : 0;

    std::unordered_map<uint64_t, int> newIndexByTitle;
    newIndexByTitle.reserve(titleIds.size());
    for (int i = 0; i < (int)titleIds.size(); ++i) {
        if (titleIds[(size_t)i] != 0)
            newIndexByTitle.emplace(titleIds[(size_t)i], i);
    }

    std::vector<int> newAppToSlot(titleIds.size(), -1);
    m_freeSlots.clear();
    for (int slotIndex = 0; slotIndex < (int)m_pool.size(); ++slotIndex) {
        auto& slot = m_pool[(size_t)slotIndex];
        if (!slot || slot->appIndex < 0 || slot->appIndex >= (int)oldTitleIds.size()) {
            if (slot)
                slot->appIndex = -1;
            m_freeSlots.push_back(slotIndex);
            continue;
        }

        const uint64_t titleId = oldTitleIds[(size_t)slot->appIndex];
        auto next = newIndexByTitle.find(titleId);
        if (titleId == 0 || next == newIndexByTitle.end() ||
            newAppToSlot[(size_t)next->second] >= 0) {
            slot->appIndex = -1;
            m_freeSlots.push_back(slotIndex);
            continue;
        }

        slot->appIndex = next->second;
        newAppToSlot[(size_t)next->second] = slotIndex;
    }

    std::vector<std::vector<uint8_t>> newCompressed(titleIds.size());
    for (int oldIndex = 0; oldIndex < (int)oldTitleIds.size(); ++oldIndex) {
        auto next = newIndexByTitle.find(oldTitleIds[(size_t)oldIndex]);
        if (next != newIndexByTitle.end() && oldIndex < (int)m_compressed.size())
            newCompressed[(size_t)next->second] = std::move(m_compressed[(size_t)oldIndex]);
    }

    m_titleIds = titleIds;
    m_compressed = std::move(newCompressed);
    m_appToSlot = std::move(newAppToSlot);
    m_artStale.assign(titleIds.size(), 0);
    m_pinnedIndex = -1;
    if (pinnedTitle != 0) {
        auto next = newIndexByTitle.find(pinnedTitle);
        if (next != newIndexByTitle.end())
            m_pinnedIndex = next->second;
    }

    m_failedTitleIds.clear();
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::reconcileCatalog(IconStreamer&& catalog) {
    reconcileTitleIds(catalog.m_titleIds);
    const size_t count = std::min(m_compressed.size(), catalog.m_compressed.size());
    for (size_t i = 0; i < count; ++i) {
        if (m_compressed[i].empty() && !catalog.m_compressed[i].empty())
            m_compressed[i] = std::move(catalog.m_compressed[i]);
    }
}

bool IconStreamer::hasData(int index) const {
    if (index < 0 || index >= (int)m_appToSlot.size())
        return false;
    if (index < (int)m_compressed.size() && !m_compressed[index].empty())
        return true;
    return index < (int)m_titleIds.size() && m_titleIds[index] != 0 && (bool)m_iconLoader;
}

bool IconStreamer::needsVisibleLoads(int currentPage, int iconsPerPage) const {
    if (iconsPerPage <= 0 || m_appToSlot.empty())
        return false;
    const int totalApps = static_cast<int>(m_appToSlot.size());
    const int totalPages = (totalApps + iconsPerPage - 1) / iconsPerPage;
    currentPage = std::clamp(currentPage, 0, std::max(0, totalPages - 1));
    int begin = currentPage * iconsPerPage;
    int end = std::min(totalApps, begin + iconsPerPage);
    // In single-row mode callers use one icon per logical page. Keep nearby
    // icons hot (radius shrinks during soft-landing after HOME return).
    if (iconsPerPage == 1) {
        const int radius = pageCacheRadius();
        begin = std::max(0, currentPage - radius);
        end = std::min(totalApps, currentPage + radius + 1);
    }
    for (int i = begin; i < end; ++i) {
        const uint64_t titleId = i < (int)m_titleIds.size() ? m_titleIds[(size_t)i] : 0;
        const bool failed = std::find(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId)
            != m_failedTitleIds.end();
        const bool pending = std::any_of(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [titleId](const PendingDecode& decode) { return decode.titleId == titleId; });
        const bool needsSlot = m_appToSlot[i] < 0 || isArtStale(i);
        if (needsSlot && !failed && (pending || hasData(i)))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Decode a single compressed icon to RGBA, downscaling to kIconSize if needed.
// ---------------------------------------------------------------------------
IconStreamer::DecodedIcon IconStreamer::decodeIconData(const std::vector<uint8_t>& data) {
    DecodedIcon out{};
    if (data.empty()) return out;

    int w, h, ch;
    uint8_t* raw = stbi_load_from_memory(data.data(), (int)data.size(),
                                         &w, &h, &ch, 4);
    if (!raw) return out;
    std::unique_ptr<uint8_t, decltype(&stbi_image_free)> full(raw, &stbi_image_free);

    // Preserve aspect. Room for ~2× single-row Posters (300×450 focused).
    constexpr int kMaxW = kIconSize * 2;       // 320
    constexpr int kMaxH = kIconSize * 3;       // 480
    if (w > kMaxW || h > kMaxH) {
        const float scale = std::min((float)kMaxW / (float)w, (float)kMaxH / (float)h);
        const int dstW = std::max(1, (int)((float)w * scale + 0.5f));
        const int dstH = std::max(1, (int)((float)h * scale + 0.5f));
        std::vector<uint8_t> scaled((size_t)dstW * dstH * 4);
        if (!scaled.empty()) {
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
                    const uint8_t* p00 = full.get() + ((size_t)y0 * w + x0) * 4;
                    const uint8_t* p10 = full.get() + ((size_t)y0 * w + x1) * 4;
                    const uint8_t* p01 = full.get() + ((size_t)y1 * w + x0) * 4;
                    const uint8_t* p11 = full.get() + ((size_t)y1 * w + x1) * 4;
                    uint8_t* dst = scaled.data() + ((size_t)y * dstW + x) * 4;
                    for (int c = 0; c < 4; ++c) {
                        dst[c] = (uint8_t)(
                            p00[c] * (1 - fx) * (1 - fy) +
                            p10[c] * fx       * (1 - fy) +
                            p01[c] * (1 - fx) * fy       +
                            p11[c] * fx       * fy       + 0.5f);
                    }
                }
            }
            out.rgba = std::move(scaled);
            out.w = dstW;
            out.h = dstH;
        }
    } else {
        out.rgba.assign(full.get(), full.get() + (size_t)w * h * 4);
        out.w = w;
        out.h = h;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Core streaming logic.
// ---------------------------------------------------------------------------
void IconStreamer::startBurst(float seconds) {
    m_burstUntil = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(seconds * 1000.f));
}

void IconStreamer::startSoftLanding(float tightSeconds, int tightRadius,
                                    float widenSeconds, int widenRadius) {
    const auto now = std::chrono::steady_clock::now();
    m_softLandingRadius = std::max(0, tightRadius);
    m_softLandingWidenRadius = std::max(0, widenRadius);
    m_softLandingUntil = now + std::chrono::milliseconds(
        static_cast<int>(std::max(0.f, tightSeconds) * 1000.f));
    m_softLandingWidenUntil = m_softLandingUntil + std::chrono::milliseconds(
        static_cast<int>(std::max(0.f, widenSeconds) * 1000.f));
    DebugLog::log("[streamer] soft-landing radius=%d for %.2fs then %d for %.2fs",
                  m_softLandingRadius, tightSeconds,
                  m_softLandingWidenRadius, widenSeconds);
}

void IconStreamer::onPageChanged(int currentPage, int iconsPerPage,
                                 nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                 const std::vector<std::shared_ptr<GlossyIcon>>& allIcons)
{
    int totalApps  = (int)m_appToSlot.size();
    if (totalApps == 0 || iconsPerPage <= 0) {
        m_lastPage = currentPage;
        m_lastIconsPerPage = iconsPerPage;
        return;
    }

    int totalPages = (totalApps + iconsPerPage - 1) / iconsPerPage;
    currentPage = std::clamp(currentPage, 0, totalPages - 1);

    m_lastPage = currentPage;
    m_lastIconsPerPage = iconsPerPage;

    int visibleStartApp = currentPage * iconsPerPage;
    int visibleEndApp   = std::min(totalApps, visibleStartApp + iconsPerPage);
    // Soft-landing can temporarily shrink the single-row radius so post-HOME
    // SD work does not stack on the first interactive seconds. Grid stays ±1.
    const int cacheRadius = iconsPerPage == 1
        ? pageCacheRadius() : kGridPageCacheRadius;
    int cacheStartPage  = std::max(0, currentPage - cacheRadius);
    int cacheEndPage    = std::min(totalPages - 1, currentPage + cacheRadius);
    int cacheStartApp   = cacheStartPage * iconsPerPage;
    int cacheEndApp     = std::min(totalApps, (cacheEndPage + 1) * iconsPerPage);

    // 1. Evict textures outside the local page window. This keeps GPU memory
    //    bounded while preserving quick navigation to nearby pages.
    for (int i = 0; i < (int)m_pool.size(); ++i) {
        if (!m_pool[i])
            continue;

        int app = m_pool[i]->appIndex;
        if (app < 0 || app == m_pinnedIndex)
            continue;

        if (app < cacheStartApp || app >= cacheEndApp) {
            if (app < (int)allIcons.size())
                allIcons[app]->setTexture(nullptr);
            if (app < (int)m_appToSlot.size())
                m_appToSlot[app] = -1;
            m_pool[i]->appIndex = -1;
            m_freeSlots.push_back(i);
        }
    }

    // 2. Re-attach already-loaded slots across the cache window. After a
    //    folder/model rebuild GlossyIcons are new widgets; only rebinding the
    //    focused tile left neighbours blank until they gained focus. Cover the
    //    whole hot window so single-row neighbours keep their art.
    for (int i = cacheStartApp; i < cacheEndApp; ++i) {
        int slotIdx = m_appToSlot[i];
        if (slotIdx < 0)
            continue;

        bool validSlot = slotIdx < (int)m_pool.size() &&
                         m_pool[slotIdx] &&
                         m_pool[slotIdx]->appIndex == i &&
                         m_pool[slotIdx]->texture.valid();
        if (validSlot) {
            if (i < (int)allIcons.size())
                allIcons[i]->setTexture(&m_pool[slotIdx]->texture);
        } else {
            m_appToSlot[i] = -1;
            if (i < (int)allIcons.size())
                allIcons[i]->setTexture(nullptr);
        }
    }

    // 3. Consume completed background decodes without ever waiting for them.
    // GPU uploads remain on this thread, but are capped to one per frame so a
    // page cannot monopolise rendering or starve audio updates. Keep the
    // established synchronous upload handoff here: presenting a frame while a
    // newly registered descriptor was still being filled produced a black
    // screen on hardware even though the menu process stayed alive.
    int uploads = 0;
    for (size_t pendingIndex = 0;
         pendingIndex < m_pendingDecodes.size() &&
         uploads < (inBurst() ? kBurstUploadsPerFrame : kUploadsPerFrame);) {
        auto& pending = m_pendingDecodes[pendingIndex];
        if (pending.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++pendingIndex;
            continue;
        }

        // Text uses the same image-memory pool. Leave this completed decode
        // queued until enough room exists instead of triggering Font's
        // emergency cache eviction and making every label disappear.
        const bool hasReusableTexture = std::any_of(
            m_freeSlots.begin(), m_freeSlots.end(),
            [this](int slotIndex) {
                return slotIndex >= 0 && slotIndex < static_cast<int>(m_pool.size()) &&
                       m_pool[static_cast<std::size_t>(slotIndex)] &&
                       m_pool[static_cast<std::size_t>(slotIndex)]->texture.valid();
            });
        if (!hasReusableTexture &&
            gpu.imageMemoryAvailable() <= kGpuImageReserve +
                                              (kIconSize * 2) * (kIconSize * 3) * 4u) {
            ++pendingIndex;
            continue;
        }

        try {
            pending.future.get();
        } catch (...) {
            pending.state->failed = true;
        }

        const uint64_t titleId = pending.titleId;
        auto titleIt = std::find(m_titleIds.begin(), m_titleIds.end(), titleId);
        const int appIndex = titleIt == m_titleIds.end()
            ? -1 : static_cast<int>(titleIt - m_titleIds.begin());
        const bool stillWanted = appIndex >= cacheStartApp && appIndex < cacheEndApp;
        auto state = std::move(pending.state);
        m_pendingDecodes[pendingIndex] = std::move(m_pendingDecodes.back());
        m_pendingDecodes.pop_back();

        if (state->cancelled.load())
            continue;
        if (state->failed || state->decoded.rgba.empty()) {
            m_failedTitleIds.push_back(titleId);
            DebugLog::log("[streamer] decode failed title=0x%016lX",
                          static_cast<unsigned long>(titleId));
            continue;
        }
        const bool stale = isArtStale(appIndex);
        // Keep showing the previous art kind until a swap-in upload lands.
        if (!stillWanted || (m_appToSlot[(size_t)appIndex] >= 0 && !stale))
            continue;

        int poolIdx = m_appToSlot[(size_t)appIndex];
        if (poolIdx < 0 || poolIdx >= (int)m_pool.size() || !m_pool[(size_t)poolIdx]) {
            if (!m_freeSlots.empty()) {
                poolIdx = m_freeSlots.back();
                m_freeSlots.pop_back();
            } else {
                poolIdx = static_cast<int>(m_pool.size());
                m_pool.emplace_back(std::make_unique<TexSlot>());
            }
        }

        auto& slot = *m_pool[(size_t)poolIdx];
        auto& decoded = state->decoded;
        // In-place swap-in keeps the icon pointer on this texture so the morph
        // never blanks. Only detach foreign consumers of a recycled slot.
        if (slot.appIndex != appIndex) {
            for (const auto& icon : allIcons) {
                if (icon && icon->texture() == &slot.texture)
                    icon->setTexture(nullptr);
            }
        }
        if (slot.texture.loadFromPixels(gpu, ren, decoded.rgba.data(), decoded.w, decoded.h)) {
            slot.appIndex = appIndex;
            m_appToSlot[(size_t)appIndex] = poolIdx;
            if (appIndex < (int)m_artStale.size())
                m_artStale[(size_t)appIndex] = 0;
            if (appIndex < (int)allIcons.size())
                allIcons[(size_t)appIndex]->setTexture(&slot.texture);
        } else {
            slot.appIndex = -1;
            m_appToSlot[(size_t)appIndex] = -1;
            m_freeSlots.push_back(poolIdx);
            m_failedTitleIds.push_back(titleId);
            DebugLog::log("[streamer] upload failed title=0x%016lX app=%d",
                          static_cast<unsigned long>(titleId), appIndex);
        }
        ++uploads;
    }

    // 4. Fill the bounded decode queue for visible icons. In single-row mode,
    // include the nearest neighbours in cursor-distance order. Both the SD read and
    // image conversion happen on a worker; this call returns immediately.
    std::vector<int> loadOrder;
    if (iconsPerPage == 1) {
        loadOrder.push_back(visibleStartApp);
        for (int distance = 1; distance <= cacheRadius; ++distance) {
            if (visibleStartApp + distance < cacheEndApp)
                loadOrder.push_back(visibleStartApp + distance);
            if (visibleStartApp - distance >= cacheStartApp)
                loadOrder.push_back(visibleStartApp - distance);
        }
    } else {
        for (int appIndex = visibleStartApp; appIndex < visibleEndApp; ++appIndex)
            loadOrder.push_back(appIndex);
    }
    for (int appIndex : loadOrder) {
        if ((int)m_pendingDecodes.size() >= (inBurst() ? kBurstMaxPendingDecodes : kMaxPendingDecodes))
            break;
        const bool stale = isArtStale(appIndex);
        if ((m_appToSlot[(size_t)appIndex] >= 0 && !stale) || !hasData(appIndex))
            continue;

        const uint64_t titleId = m_titleIds[(size_t)appIndex];
        const bool failed = std::find(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId)
            != m_failedTitleIds.end();
        const bool pending = std::any_of(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [titleId](const PendingDecode& decode) { return decode.titleId == titleId; });
        if (titleId == 0 || failed || pending)
            continue;

        // Stale art must re-read from the loader; cached bytes are the wrong kind.
        std::vector<uint8_t> compressed;
        if (!stale && appIndex < (int)m_compressed.size())
            compressed = std::move(m_compressed[(size_t)appIndex]);

        auto state = std::make_shared<DecodeState>();
        IconDataLoader loader = m_iconLoader;
        const bool preferPoster = m_preferPosterArt;
        auto work = [state, titleId, loader, preferPoster,
                     compressed = std::move(compressed)]() mutable {
            if (state->cancelled.load())
                return;
            if (preferPoster) {
                state->decoded = decodePosterFromCache(titleId);
                if (!state->decoded.rgba.empty())
                    return;
            }
            if (compressed.empty() && loader)
                compressed = loader(titleId);
            if (state->cancelled.load())
                return;
            if (compressed.empty()) {
                state->failed = true;
                return;
            }
            state->decoded = IconStreamer::decodeIconData(compressed);
            state->failed = state->decoded.rgba.empty();
        };

        PendingDecode decode;
        decode.titleId = titleId;
        decode.state = state;
        if (m_threadPool) {
            decode.future = m_threadPool->submit(std::move(work));
        } else {
            std::promise<void> completed;
            decode.future = completed.get_future();
            try {
                work();
                completed.set_value();
            } catch (...) {
                completed.set_exception(std::current_exception());
            }
        }
        m_pendingDecodes.push_back(std::move(decode));
        DebugLog::log("[streamer] scheduled title=0x%016lX app=%d pending=%d",
                      static_cast<unsigned long>(titleId), appIndex,
                      static_cast<int>(m_pendingDecodes.size()));
    }
}

void IconStreamer::forceReload(int currentPage, int iconsPerPage,
                                nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::vector<std::shared_ptr<GlossyIcon>>& allIcons)
{
    // Widgets keep non-owning pointers into m_pool. Detach every pointer before
    // destroying the pool; otherwise the next frame can submit a descriptor
    // backed by a freed MemBlock and put deko3d into a permanent page-fault
    // state. This is especially visible when an overridden icon is applied
    // while edit/move mode still owns a ghost of the focused application.
    for (const auto& icon : allIcons) {
        if (icon)
            icon->setTexture(nullptr);
    }

    // Throw away all loaded state so onPageChanged re-does everything.
    for (auto& slot : m_pool) slot->appIndex = -1;
    m_freeSlots.clear();
    for (int i = 0; i < (int)m_pool.size(); ++i) m_freeSlots.push_back(i);
    std::fill(m_appToSlot.begin(), m_appToSlot.end(), -1);

    // Clear the Texture objects themselves (GPU memory + descriptor slots)
    // because a forceReload typically follows a full GPU reset.
    m_pool.clear();
    m_freeSlots.clear();
    cancelPending();
    m_pendingDecodes.clear();
    m_failedTitleIds.clear();
    // Drop cached JPEG/PNG bytes too — otherwise a Poster loaded for single-row
    // would keep showing after switching back to the square grid.
    for (auto& bytes : m_compressed)
        bytes.clear();
    m_artStale.assign(m_appToSlot.size(), 0);

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    onPageChanged(currentPage, iconsPerPage, gpu, ren, allIcons);
}

void IconStreamer::cancelShadowPrefetch() {
    for (auto& entry : m_shadowPrefetch) {
        if (entry.state)
            entry.state->cancelled.store(true);
    }
    m_shadowPrefetch.clear();
}

void IconStreamer::startShadowPrefetch(bool preferPoster,
                                       const std::vector<std::uint64_t>& titleIds,
                                       IconDataLoader loader) {
    cancelShadowPrefetch();
    m_shadowPreferPoster = preferPoster;
    if (!loader)
        return;

    std::unordered_map<std::uint64_t, bool> seen;
    m_shadowPrefetch.reserve(titleIds.size());
    for (const std::uint64_t titleId : titleIds) {
        if (titleId == 0 || seen[titleId])
            continue;
        seen[titleId] = true;

        ShadowPrefetchEntry entry;
        entry.titleId = titleId;
        entry.state = std::make_shared<DecodeState>();
        auto state = entry.state;
        auto work = [state, titleId, loader, preferPoster]() {
            const auto started = std::chrono::steady_clock::now();
            if (state->cancelled.load())
                return;
            const char* source = "jpeg";
            if (preferPoster) {
                state->decoded = decodePosterFromCache(titleId);
                if (!state->decoded.rgba.empty()) {
                    source = "fit-cache";
                    DebugLog::log("[layout-prefetch] shadow decode title=%016lX "
                                  "source=%s %dx%d in %ldms",
                                  static_cast<unsigned long>(titleId), source,
                                  state->decoded.w, state->decoded.h,
                                  static_cast<long>(std::chrono::duration_cast<
                                      std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - started)
                                                        .count()));
                    return;
                }
            }
            auto compressed = loader(titleId);
            if (state->cancelled.load())
                return;
            if (compressed.empty()) {
                state->failed = true;
                DebugLog::log("[layout-prefetch] shadow decode title=%016lX FAILED empty",
                              static_cast<unsigned long>(titleId));
                return;
            }
            state->decoded = IconStreamer::decodeIconData(compressed);
            state->failed = state->decoded.rgba.empty();
            DebugLog::log("[layout-prefetch] shadow decode title=%016lX source=%s "
                          "%dx%d failed=%d in %ldms",
                          static_cast<unsigned long>(titleId), source,
                          state->decoded.w, state->decoded.h,
                          state->failed ? 1 : 0,
                          static_cast<long>(std::chrono::duration_cast<
                              std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started).count()));
        };
        if (m_threadPool) {
            entry.future = m_threadPool->submit(std::move(work));
        } else {
            std::promise<void> completed;
            entry.future = completed.get_future();
            try {
                work();
                completed.set_value();
            } catch (...) {
                completed.set_exception(std::current_exception());
            }
        }
        m_shadowPrefetch.push_back(std::move(entry));
    }
}

void IconStreamer::pumpShadowPrefetch(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    for (auto& entry : m_shadowPrefetch) {
        if (entry.decodedReady || !entry.future.valid())
            continue;
        if (entry.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            continue;
        try {
            entry.future.get();
        } catch (...) {
            if (entry.state)
                entry.state->failed = true;
        }
        if (!entry.state || entry.state->cancelled.load()) {
            entry.failed = true;
            entry.decodedReady = true;
            entry.uploaded = true;
            continue;
        }
        entry.failed = entry.state->failed || entry.state->decoded.rgba.empty();
        if (!entry.failed)
            entry.decoded = std::move(entry.state->decoded);
        entry.decodedReady = true;
        if (entry.failed)
            entry.uploaded = true;
        entry.state.reset();
    }

    // Spread GPU uploads across frames so HOME stays responsive during prefetch.
    int uploads = 0;
    for (auto& entry : m_shadowPrefetch) {
        if (uploads >= kShadowUploadsPerPump)
            break;
        if (!entry.decodedReady || entry.failed || entry.uploaded)
            continue;
        if (entry.decoded.rgba.empty()) {
            entry.failed = true;
            entry.uploaded = true;
            continue;
        }
        auto tex = std::make_unique<nxui::Texture>();
        if (!tex->loadFromPixels(gpu, ren, entry.decoded.rgba.data(),
                                 entry.decoded.w, entry.decoded.h)) {
            entry.failed = true;
            entry.uploaded = true;
            continue;
        }
        entry.decoded.rgba.clear();
        entry.decoded.rgba.shrink_to_fit();
        entry.texture = std::move(tex);
        entry.uploaded = true;
        DebugLog::log("[layout-prefetch] shadow upload title=%016lX ok",
                      static_cast<unsigned long>(entry.titleId));
        ++uploads;
    }
}

bool IconStreamer::shadowPrefetchReady() const {
    if (m_shadowPrefetch.empty())
        return true;
    for (const auto& entry : m_shadowPrefetch) {
        if (!entry.decodedReady || !entry.uploaded)
            return false;
    }
    return true;
}

void IconStreamer::installShadowPrefetch(
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons) {
    if (m_shadowPrefetch.empty())
        return;

    std::unordered_map<std::uint64_t, ShadowPrefetchEntry*> byTitle;
    byTitle.reserve(m_shadowPrefetch.size());
    for (auto& entry : m_shadowPrefetch) {
        if (entry.uploaded && !entry.failed && entry.texture && entry.texture->valid())
            byTitle.emplace(entry.titleId, &entry);
    }

    // Destination preference is now live; drop stale bytes so we never re-bind
    // the previous art kind for these titles.
    m_preferPosterArt = m_shadowPreferPoster;
    for (auto& bytes : m_compressed)
        bytes.clear();

    for (int appIndex = 0; appIndex < (int)allIcons.size(); ++appIndex) {
        if (!allIcons[(size_t)appIndex])
            continue;
        const uint64_t titleId = appIndex < (int)m_titleIds.size()
            ? m_titleIds[(size_t)appIndex] : 0;
        if (titleId == 0)
            continue;
        auto found = byTitle.find(titleId);
        if (found == byTitle.end() || !found->second->texture)
            continue;

        int poolIdx = appIndex < (int)m_appToSlot.size() ? m_appToSlot[(size_t)appIndex] : -1;
        if (poolIdx < 0 || poolIdx >= (int)m_pool.size() || !m_pool[(size_t)poolIdx]) {
            if (!m_freeSlots.empty()) {
                poolIdx = m_freeSlots.back();
                m_freeSlots.pop_back();
            } else {
                poolIdx = static_cast<int>(m_pool.size());
                m_pool.emplace_back(std::make_unique<TexSlot>());
            }
        }

        auto& slot = *m_pool[(size_t)poolIdx];
        if (slot.appIndex != appIndex) {
            for (const auto& icon : allIcons) {
                if (icon && icon->texture() == &slot.texture)
                    icon->setTexture(nullptr);
            }
        }
        // Steal the already-uploaded texture — no GPU stall on the morph frame.
        slot.texture = std::move(*found->second->texture);
        found->second->texture.reset();
        slot.appIndex = appIndex;
        if (appIndex < (int)m_appToSlot.size())
            m_appToSlot[(size_t)appIndex] = poolIdx;
        if (appIndex < (int)m_artStale.size())
            m_artStale[(size_t)appIndex] = 0;
        allIcons[(size_t)appIndex]->setTexture(&slot.texture);
    }

    cancelShadowPrefetch();
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::reloadTitle(
    uint64_t titleId, int currentPage, int iconsPerPage,
    nxui::GpuDevice& gpu, nxui::Renderer& ren,
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons) {
    const auto found = std::find(m_titleIds.begin(), m_titleIds.end(), titleId);
    if (found == m_titleIds.end()) return;
    const int appIndex = static_cast<int>(found - m_titleIds.begin());

    for (auto& pending : m_pendingDecodes) {
        if (pending.titleId == titleId && pending.state)
            pending.state->cancelled.store(true);
    }
    m_failedTitleIds.erase(
        std::remove(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId),
        m_failedTitleIds.end());
    if (appIndex < static_cast<int>(m_compressed.size()))
        m_compressed[(size_t)appIndex].clear();

    const int slotIndex = appIndex < static_cast<int>(m_appToSlot.size())
        ? m_appToSlot[(size_t)appIndex] : -1;
    if (appIndex < static_cast<int>(allIcons.size()) && allIcons[(size_t)appIndex])
        allIcons[(size_t)appIndex]->setTexture(nullptr);
    if (slotIndex >= 0 && slotIndex < static_cast<int>(m_pool.size()) && m_pool[(size_t)slotIndex]) {
        m_pool[(size_t)slotIndex]->appIndex = -1;
        m_appToSlot[(size_t)appIndex] = -1;
        if (std::find(m_freeSlots.begin(), m_freeSlots.end(), slotIndex) == m_freeSlots.end())
            m_freeSlots.push_back(slotIndex);
    }

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    onPageChanged(currentPage, iconsPerPage, gpu, ren, allIcons);
}
