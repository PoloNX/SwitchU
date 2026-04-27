#pragma once
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <vector>
#include <memory>
#include <cstdint>

class GlossyIcon;

// Streams icon textures on demand based on the currently visible page.
// Only icons in the visible range (current page +- kPageMargin) are uploaded
// to the GPU.  Compressed JPEG/PNG data is kept in CPU memory for all apps
// so textures can be decoded and uploaded when the user switches pages.
class IconStreamer {
public:
    // Store compressed icon data for all apps.  Called once after the app
    // list has been fetched.
    void init(int appCount);
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

    // Release everything (textures + compressed data).
    void clear();

    // Keep internal compressed data and loaded-slot mappings aligned when
    // app entries are swapped in the grid model.
    bool swapIndices(int a, int b);

    int  iconCount()         const { return (int)m_compressed.size(); }
    bool hasData(int index)  const { return index >= 0 && index < (int)m_compressed.size() && !m_compressed[index].empty(); }

private:
    struct DecodedIcon {
        uint8_t* rgba = nullptr;
        int w = 0, h = 0;
        bool scaledWithMalloc = false;
    };

    DecodedIcon decodeAndScale(int appIndex) const;

    // Per-app compressed JPEG/PNG bytes.
    std::vector<std::vector<uint8_t>> m_compressed;

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
    static constexpr int kPageMargin = 1;
    static constexpr int kIconSize   = 256;
};
