#pragma once
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

    // Prepare icon metadata for all apps after the app list has been fetched.
    void init(int appCount);
    void setIconDataLoader(IconDataLoader loader);
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

    // Release everything (textures + compressed data).
    void clear();
    void cancelPending();

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
    struct DecodedIcon {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
    };

    static DecodedIcon decodeAndScale(const std::vector<uint8_t>& data);

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
    IconDataLoader m_iconLoader;
    nxui::ThreadPool* m_threadPool = nullptr;
    std::vector<PendingDecode> m_pendingDecodes;
    std::vector<uint64_t> m_failedTitleIds;

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

    static constexpr int kPageCacheRadius = 2;
    static constexpr int kIconSize        = 160;
    static constexpr int kUploadsPerFrame = 1;
    static constexpr int kMaxPendingDecodes = 4;
};
