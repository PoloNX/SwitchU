#pragma once
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>

class GlossyIcon;

// Streams icon textures on demand based on the currently visible page.
// Only icons in the visible range (current page +- kPageMargin) are uploaded
// to the GPU. Compressed JPEG/PNG data is fetched on demand so the menu does
// not keep every title icon resident while idle.
class IconStreamer {
public:
    using IconDataLoader = std::function<std::vector<uint8_t>(uint64_t titleId)>;

    // Prepare icon metadata for all apps after the app list has been fetched.
    void init(int appCount);
    void setIconDataLoader(IconDataLoader loader);
    void setTitleId(int appIndex, uint64_t titleId);
    void setIconData(int appIndex, std::vector<uint8_t> compressed);

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

    // Release uploaded GPU textures while keeping title metadata, so the
    // current page can be loaded again when rendering resumes.
    void releaseLoadedTextures(const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Release everything (textures + compressed data).
    void clear();

    // Keep internal compressed data and loaded-slot mappings aligned when
    // app entries are swapped in the grid model.
    bool swapIndices(int a, int b);

    int  iconCount()         const { return (int)m_appToSlot.size(); }
    bool hasData(int index)  const;

private:
    struct DecodedIcon {
        uint8_t* rgba = nullptr;
        int w = 0, h = 0;
        bool scaledWithMalloc = false;
    };

    DecodedIcon decodeAndScale(const std::vector<uint8_t>& data) const;

    // Transient compressed JPEG/PNG bytes, normally only used for prefetched
    // data. App icons are otherwise fetched on demand and released after upload.
    std::vector<std::vector<uint8_t>> m_compressed;
    std::vector<uint64_t> m_titleIds;
    IconDataLoader m_iconLoader;

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

    // How many pages around the current one to keep loaded.
    static constexpr int kPageMargin = 0;
    static constexpr int kIconSize   = 160;
};
