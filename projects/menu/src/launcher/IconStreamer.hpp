#pragma once

#include <chrono>
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <future>
#include <atomic>

class GlossyIcon;

// Streams icon textures on demand based on the currently visible page.
// Loaded textures are kept for a small window around the current page, so
// nearby navigation is instant without letting GPU memory grow unbounded.
class IconStreamer {
public:
    using IconDataLoader = std::function<std::vector<uint8_t>(uint64_t titleId)>;

    struct DecodedIcon {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
    };

    // CPU-only helper. Safe on a ThreadPool worker; upload stays on the UI thread.
    static DecodedIcon decodeIconData(const std::vector<uint8_t>& data);

    // Prepare icon metadata for all apps after the app list has been fetched.
    void init(int appCount);
    void setIconDataLoader(IconDataLoader loader);
    // Prefer tall Poster files vs square icons. When the preference flips,
    // compressed bytes are dropped and loaded GPU textures are marked stale so
    // the next onPageChanged() can swap art in place without blanking tiles.
    // Returns true when the preference actually changed.
    bool setPreferPosterArt(bool preferPoster);
    bool preferPosterArt() const { return m_preferPosterArt; }
    void setThreadPool(nxui::ThreadPool* pool) { m_threadPool = pool; }
    void setTitleId(int appIndex, uint64_t titleId);
    void setIconData(int appIndex, std::vector<uint8_t> compressed);
    void resize(int appCount);
    void setPinnedIndex(int appIndex);
    void clearPinnedIndex();

    // Call when the visible page changes (or on first display).
    // Decodes + uploads textures for the new visible range and evicts
    // textures that are no longer needed.  Updates GlossyIcon texture
    // pointers directly.
    void onPageChanged(int currentPage, int iconsPerPage,
                       nxui::GpuDevice& gpu, nxui::Renderer& ren,
                       const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Force-reload textures for the current page (e.g. after a theme change
    // that resets the GPU pool).
    void forceReload(int currentPage, int iconsPerPage,
                     nxui::GpuDevice& gpu, nxui::Renderer& ren,
                     const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Reload one title in place (for an icon override) without destroying the
    // complete GPU pool or stalling on unrelated textures.
    void reloadTitle(uint64_t titleId, int currentPage, int iconsPerPage,
                     nxui::GpuDevice& gpu, nxui::Renderer& ren,
                     const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Temporarily allow more decodes and uploads per frame, e.g. right after a
    // fast HOME return so the visible page fills in at once instead of one icon
    // per frame. Reverts to the normal limits by itself.
    void startBurst(float seconds);

    // Off-screen art prefetch for layout transitions: decode destination art
    // (poster or icon) by titleId without touching textures already shown on
    // tiles. pumpShadowPrefetch() uploads a few GPU textures per frame so the
    // loader animation keeps running; installShadowPrefetch() only attaches.
    void cancelShadowPrefetch();
    void startShadowPrefetch(bool preferPoster,
                             const std::vector<std::uint64_t>& titleIds,
                             IconDataLoader loader);
    void pumpShadowPrefetch(nxui::GpuDevice& gpu, nxui::Renderer& ren);
    bool shadowPrefetchReady() const;
    void installShadowPrefetch(
        const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Release everything (textures + compressed data).
    void clear();
    void cancelPending();
    // Drop in-flight decodes except one title (keep focused art during morph).
    void cancelPendingExcept(std::uint64_t keepTitleId);

    // Keep internal compressed data and loaded-slot mappings aligned when
    // app entries are swapped in the grid model.
    bool swapIndices(int a, int b);

    // Update indices after a catalogue refresh while retaining textures for
    // titles that still exist. This avoids tearing down the complete GPU icon
    // cache just because one application was installed or removed.
    void reconcileTitleIds(const std::vector<uint64_t>& titleIds);
    void reconcileCatalog(IconStreamer&& catalog);

    int  iconCount()         const { return (int)m_appToSlot.size(); }
    bool hasData(int index)  const;
    bool needsVisibleLoads(int currentPage, int iconsPerPage) const;

private:
    struct DecodeState {
        DecodedIcon decoded;
        bool failed = false;
        std::atomic<bool> cancelled{false};
    };

    struct PendingDecode {
        uint64_t titleId = 0;
        std::shared_ptr<DecodeState> state;
        std::future<void> future;
    };

    // Transient compressed JPEG/PNG bytes, normally only used for prefetched
    // data. App icons are otherwise fetched on demand and released after upload.
    std::vector<std::vector<uint8_t>> m_compressed;
    std::vector<uint64_t> m_titleIds;
    // Per-app: GPU slot still shows the previous art kind and needs a swap-in.
    std::vector<uint8_t> m_artStale;
    bool m_preferPosterArt = false;
    IconDataLoader m_iconLoader;
    nxui::ThreadPool* m_threadPool = nullptr;
    std::vector<PendingDecode> m_pendingDecodes;
    std::vector<uint64_t> m_failedTitleIds;

    struct ShadowPrefetchEntry {
        std::uint64_t titleId = 0;
        DecodedIcon decoded;
        std::unique_ptr<nxui::Texture> texture;
        bool failed = false;
        bool decodedReady = false;
        bool uploaded = false;
        std::shared_ptr<DecodeState> state;
        std::future<void> future;
    };
    std::vector<ShadowPrefetchEntry> m_shadowPrefetch;
    bool m_shadowPreferPoster = false;
    static constexpr int kShadowUploadsPerPump = 1;

    void softInvalidateArt();
    bool isArtStale(int appIndex) const;

    // Pool of reusable GPU textures.
    struct TexSlot {
        nxui::Texture texture;
        int appIndex = -1;   // which app currently occupies this slot (-1 = free)
    };
    std::vector<std::unique_ptr<TexSlot>> m_pool;

    // Maps app index → pool slot index (-1 = not loaded).
    std::vector<int> m_appToSlot;

    // Indices of free pool slots.
    std::vector<int> m_freeSlots;

    int m_lastPage = -1;
    int m_lastIconsPerPage = -1;
    int m_pinnedIndex = -1;

    static constexpr int kPageCacheRadius = 4;
    static constexpr int kGridPageCacheRadius = 1;
    // Base decode cap per side; portraits may use up to 2× / 3× (see decodeIconData).
    static constexpr int kIconSize        = 160;
    static constexpr int kUploadsPerFrame = 1;
    static constexpr int kMaxPendingDecodes = 4;
    static constexpr int kBurstUploadsPerFrame = 4;
    static constexpr int kBurstMaxPendingDecodes = 8;
    std::chrono::steady_clock::time_point m_burstUntil{};
    bool inBurst() const { return std::chrono::steady_clock::now() < m_burstUntil; }
    static constexpr std::uint64_t kGpuImageReserve = 4u * 1024u * 1024u;
};
