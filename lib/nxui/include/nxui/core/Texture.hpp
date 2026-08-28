#pragma once
#include <nxui/core/Types.hpp>
#include <nxui/core/GpuDevice.hpp>
#ifdef NXUI_BACKEND_DEKO3D
#include <deko3d.hpp>
#endif
#include <string>
#include <memory>

struct SDL_Texture;

namespace nxui {

class Renderer;

class Texture {
public:
    Texture() = default;
    ~Texture();

#ifdef NXUI_BACKEND_DEKO3D
    // deko3d move semantics
    Texture(Texture&& o) noexcept;
    Texture& operator=(Texture&& o) noexcept;
#else
    // SDL2 move semantics
    Texture(Texture&& o) noexcept;
    Texture& operator=(Texture&& o) noexcept;
#endif
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Load from RGBA pixel data already in memory
    bool loadFromPixels(GpuDevice& gpu, Renderer& ren,
                        const uint8_t* rgba, int w, int h);

    // Load from RGBA pixel data into a shared pool MemBlock (no per-texture allocation).
    bool loadFromPixelsPooled(GpuDevice& gpu, Renderer& ren,
                              const uint8_t* rgba, int w, int h);

    // Load from image file (PNG/JPG via stb_image).
    // maxSide <= 0 keeps the source resolution.
    bool loadFromFile(GpuDevice& gpu, Renderer& ren, const std::string& path, int maxSide = 128);

    // Load from in-memory image data (JPEG/PNG via stb_image)
    bool loadFromMemory(GpuDevice& gpu, Renderer& ren,
                        const uint8_t* data, size_t dataSize, int maxSide = 0);

    // Load from SDL_Surface-style data (RGBA8, row pitch may differ)
    bool loadFromSurface(GpuDevice& gpu, Renderer& ren,
                         const uint8_t* data, int w, int h, int pitch);

    int  width()  const { return m_width; }
    int  height() const { return m_height; }
    bool valid()  const { return m_valid; }
    std::size_t allocationSize() const {
#ifdef NXUI_BACKEND_DEKO3D
        return (static_cast<std::size_t>(m_allocSize) + kGpuAlign - 1u) &
               ~(static_cast<std::size_t>(kGpuAlign) - 1u);
#else
        return static_cast<std::size_t>(m_width > 0 ? m_width : 0) *
               static_cast<std::size_t>(m_height > 0 ? m_height : 0) * 4u;
#endif
    }

    // Descriptor slot in the renderer's image descriptor set
    int  descriptorSlot() const { return m_slot; }

#ifdef NXUI_BACKEND_DEKO3D
    dk::Image&       image()       { return m_image; }
    const dk::Image& image() const { return m_image; }
#else
    SDL_Texture* sdlTexture() const { return m_sdlTex; }
#endif

private:
#ifdef NXUI_BACKEND_DEKO3D
    dk::Image          m_image;
    dk::UniqueMemBlock m_mem;
    uint32_t m_allocSize = 0;
    GpuDevice* m_gpu = nullptr;
    Renderer* m_renderer = nullptr;
#else
    SDL_Texture* m_sdlTex = nullptr;
    GpuDevice*   m_gpu = nullptr;
#endif
    int m_width  = 0;
    int m_height = 0;
    int m_slot   = -1;
    bool m_valid = false;
};

} // namespace nxui
