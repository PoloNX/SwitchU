#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace nxui {

Texture::Texture(Texture&& o) noexcept
    : m_image(o.m_image)
    , m_mem(static_cast<dk::MemBlock&&>(o.m_mem))
    , m_width(o.m_width), m_height(o.m_height)
    , m_slot(o.m_slot), m_valid(o.m_valid)
    , m_allocSize(o.m_allocSize)
    , m_gpu(o.m_gpu), m_renderer(o.m_renderer) {
    o.m_width = o.m_height = 0;
    o.m_slot = -1;
    o.m_valid = false;
    o.m_allocSize = 0;
    o.m_gpu = nullptr;
    o.m_renderer = nullptr;
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this == &o) return *this;
    if (m_renderer && m_slot >= 0)
        m_renderer->releaseTextureSlot(m_slot);
    if (m_gpu && m_mem && m_allocSize > 0)
        m_gpu->freeImageMemory(m_allocSize);
    m_mem = nullptr;
    m_image = o.m_image;
    m_mem = static_cast<dk::MemBlock&&>(o.m_mem);
    m_width = o.m_width;
    m_height = o.m_height;
    m_slot = o.m_slot;
    m_valid = o.m_valid;
    m_allocSize = o.m_allocSize;
    m_gpu = o.m_gpu;
    m_renderer = o.m_renderer;
    o.m_width = o.m_height = 0;
    o.m_slot = -1;
    o.m_valid = false;
    o.m_allocSize = 0;
    o.m_gpu = nullptr;
    o.m_renderer = nullptr;
    return *this;
}

Texture::~Texture() {
    if (m_renderer && m_slot >= 0)
        m_renderer->releaseTextureSlot(m_slot);
    if (m_gpu && m_mem && m_allocSize > 0)
        m_gpu->freeImageMemory(m_allocSize);
}

bool Texture::loadFromPixels(GpuDevice& gpu, Renderer& ren,
                             const uint8_t* rgba, int w, int h)
{
    m_gpu = &gpu;
    m_renderer = &ren;
    int oldSlot = m_slot;
    uint32_t oldAllocSize = m_allocSize;

    m_valid = false;
    m_slot  = -1;
    m_allocSize = 0;
    m_width  = w;
    m_height = h;

    dk::ImageLayout layout;
    dk::ImageLayoutMaker{gpu.device()}
        .setFlags(0)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(w, h)
        .initialize(layout);

    uint32_t needed = layout.getSize();
    uint32_t alignedNeeded = (needed + kGpuAlign - 1) & ~(kGpuAlign - 1);
    uint32_t alignedOld    = (oldAllocSize + kGpuAlign - 1) & ~(kGpuAlign - 1);

    if (m_mem && alignedOld >= alignedNeeded) {
        // Reuse existing MemBlock — avoids kernel free+alloc round-trip.
        // Keep the original allocSize so the budget stays accurate.
        m_allocSize = oldAllocSize;
    } else {
        // Need a bigger block — free the old one first.
        if (m_mem && oldAllocSize > 0)
            gpu.freeImageMemory(oldAllocSize);

        m_mem = gpu.allocImageMemory(needed);
        if (!m_mem) {
            if (oldSlot >= 0)
                ren.releaseTextureSlot(oldSlot);
            std::printf("[Texture] allocImageMemory FAILED (%dx%d) — GPU budget exhausted\n", w, h);
            return false;
        }
        m_allocSize = needed;
    }

    m_image.initialize(layout, m_mem, 0);

    if (!gpu.uploadTexture(m_image, rgba, w * h * 4, w, h)) {
        m_slot = oldSlot;
        std::printf("[Texture] uploadTexture FAILED (%dx%d)\n", w, h);
        return false;
    }

    dk::ImageView view{m_image};
    if (oldSlot >= 0) {
        // Reuse the same descriptor slot — avoids slot exhaustion
        ren.updateTexture(oldSlot, view);
        m_slot = oldSlot;
    } else {
        m_slot = ren.registerTexture(view);
        if (m_slot < 0) {
            std::printf("[Texture] registerTexture FAILED (%dx%d) — descriptor pool full\n", w, h);
            return false;
        }
    }
    m_valid = true;
    return true;
}

bool Texture::loadFromPixelsPooled(GpuDevice& gpu, Renderer& ren,
                                    const uint8_t* rgba, int w, int h)
{
    m_gpu = &gpu;
    m_renderer = &ren;
    m_valid = false;
    m_slot  = -1;
    m_width  = w;
    m_height = h;

    dk::ImageLayout layout;
    dk::ImageLayoutMaker{gpu.device()}
        .setFlags(0)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(w, h)
        .initialize(layout);

    auto alloc = gpu.allocImageFromPool(layout.getSize(), layout.getAlignment());
    if (!alloc.valid()) {
        std::printf("[Texture] pool alloc FAILED (%dx%d) — budget exhausted\n", w, h);
        return false;
    }
    // m_mem stays empty — pool owns the memory.
    m_image.initialize(layout, alloc.block, alloc.offset);

    if (!gpu.uploadTexture(m_image, rgba, w * h * 4, w, h)) {
        std::printf("[Texture] uploadTexture FAILED (%dx%d)\n", w, h);
        return false;
    }

    dk::ImageView view{m_image};
    m_slot = ren.registerTexture(view);
    if (m_slot < 0) {
        std::printf("[Texture] registerTexture FAILED (%dx%d) — descriptor pool full\n", w, h);
        return false;
    }
    m_valid = true;
    return true;
}

bool Texture::loadFromFile(GpuDevice& gpu, Renderer& ren, const std::string& path, int maxSide) {
    int w, h, ch;
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::printf("[Texture] stbi_load FAILED: %s\n", path.c_str());
        return false;
    }
    if (maxSide > 0 && (w > maxSide || h > maxSide)) {
        float scale = std::min((float)maxSide / w, (float)maxSide / h);
        int dw = std::max(1, (int)(w * scale));
        int dh = std::max(1, (int)(h * scale));
        uint8_t* scaled = (uint8_t*)std::malloc((size_t)dw * dh * 4);
        if (scaled) {
            for (int y = 0; y < dh; ++y) {
                int sy = y * h / dh;
                for (int x = 0; x < dw; ++x) {
                    int sx = x * w / dw;
                    std::memcpy(scaled + ((size_t)y * dw + x) * 4,
                                data   + ((size_t)sy * w + sx) * 4, 4);
                }
            }
            stbi_image_free(data);
            data = scaled;
            w = dw;
            h = dh;
            // after this, data is malloc'd not stbi
        }
    }
    bool ok = loadFromPixels(gpu, ren, data, w, h);
    // stbi_image_free uses free(), and malloc'd data also uses free()
    std::free(data);
    return ok;
}

bool Texture::loadFromMemory(GpuDevice& gpu, Renderer& ren,
                             const uint8_t* data, size_t dataSize, int maxSide)
{
    int w, h, ch;
    uint8_t* pixels = stbi_load_from_memory(data, (int)dataSize, &w, &h, &ch, 4);
    if (!pixels) return false;

    if (maxSide > 0 && (w > maxSide || h > maxSide)) {
        float scale = std::min((float)maxSide / w, (float)maxSide / h);
        int dw = std::max(1, (int)(w * scale));
        int dh = std::max(1, (int)(h * scale));
        uint8_t* scaled = (uint8_t*)std::malloc((size_t)dw * dh * 4);
        if (scaled) {
            for (int y = 0; y < dh; ++y) {
                int sy = y * h / dh;
                for (int x = 0; x < dw; ++x) {
                    int sx = x * w / dw;
                    std::memcpy(scaled + ((size_t)y * dw + x) * 4,
                                pixels + ((size_t)sy * w + sx) * 4, 4);
                }
            }
            stbi_image_free(pixels);
            pixels = scaled;
            w = dw;
            h = dh;
        }
    }

    bool ok = loadFromPixels(gpu, ren, pixels, w, h);
    std::free(pixels);
    return ok;
}

bool Texture::loadFromSurface(GpuDevice& gpu, Renderer& ren,
                              const uint8_t* data, int w, int h, int pitch)
{
    // Convert to tightly packed RGBA if pitch != w*4
    if (pitch == w * 4) {
        return loadFromPixels(gpu, ren, data, w, h);
    }
    std::vector<uint8_t> tight(w * h * 4);
    for (int y = 0; y < h; ++y)
        std::memcpy(tight.data() + y * w * 4, data + y * pitch, w * 4);
    return loadFromPixels(gpu, ren, tight.data(), w, h);
}

} // namespace nxui
