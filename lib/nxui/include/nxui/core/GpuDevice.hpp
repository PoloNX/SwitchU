#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>

#ifdef NXUI_BACKEND_DEKO3D
#include <deko3d.hpp>
#include <switch.h>
#else
// SDL2 backend — forward declare what we need
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
#include <switch.h>
#endif

namespace nxui {

#ifdef NXUI_BACKEND_DEKO3D
inline constexpr uint32_t kGpuAlign = DK_MEMBLOCK_ALIGNMENT;   // 0x1000

struct GpuPool {
    dk::UniqueMemBlock block;
    uint32_t size  = 0;
    uint32_t used  = 0;
    void*    cpuBase = nullptr;
    DkGpuAddr gpuBase = 0;

    bool create(dk::Device dev, uint32_t sz, uint32_t flags) {
        size = (sz + kGpuAlign - 1) & ~(kGpuAlign - 1);
        block = dk::MemBlockMaker{dev, size}.setFlags(flags).create();
        if (flags & DkMemBlockFlags_CpuUncached)
            cpuBase = block.getCpuAddr();
        gpuBase = block.getGpuAddr();
        used = 0;
        return true;
    }

    uint32_t alloc(uint32_t bytes, uint32_t align = 256) {
        uint32_t off = (used + align - 1) & ~(align - 1);
        if (off + bytes > size) return UINT32_MAX;
        used = off + bytes;
        return off;
    }

    DkGpuAddr gpuAddr(uint32_t off = 0) const { return gpuBase + off; }
    void*     cpuAddr(uint32_t off = 0) const { return (uint8_t*)cpuBase + off; }
};
#endif // NXUI_BACKEND_DEKO3D

class GpuDevice {
public:
    static constexpr int FB_WIDTH  = 1280;
    static constexpr int FB_HEIGHT = 720;
    static constexpr int NUM_FB    = 2;

    static constexpr int MAX_TEXTURES   = 2048;
    static constexpr int MAX_SAMPLERS   = 16;
    static constexpr int MAX_VERTICES   = 65536;
    static constexpr int VTX_BUF_SIZE   = MAX_VERTICES * 32;
    static constexpr int IDX_BUF_SIZE   = 256;
    static constexpr int VS_UBO_SIZE    = 256;
    static constexpr int FS_UBO_SIZE    = 256;
    static constexpr int CMD_BUF_SIZE   = 256 * 1024;
    static constexpr int CODE_POOL_SIZE = 256 * 1024;

    bool initialize();
    void shutdown();

    int  beginFrame();
    void endFrame();
    void waitIdle();

    int  width()  const { return FB_WIDTH; }
    int  height() const { return FB_HEIGHT; }

#ifdef NXUI_BACKEND_DEKO3D
    // deko3d-specific accessors
    dk::Device   device()  const { return m_dev; }
    dk::Queue    queue()   const { return m_queue; }
    dk::CmdBuf  cmdBuf()  const { return m_cmdbuf[m_slot]; }
    int          slot()    const { return m_slot; }

    GpuPool& codePool()  { return m_codePool; }
    GpuPool& dataPool()  { return m_dataPool; }
    GpuPool& imagePool() { return m_imagePool; }

    DkGpuAddr imgDescGpuAddr()  const { return m_dataPool.gpuAddr(m_imgDescOff); }
    DkGpuAddr samDescGpuAddr()  const { return m_dataPool.gpuAddr(m_samDescOff); }
    void*     imgDescCpuAddr()  const { return m_dataPool.cpuAddr(m_imgDescOff); }
    void*     samDescCpuAddr()  const { return m_dataPool.cpuAddr(m_samDescOff); }

    DkGpuAddr vtxGpuAddr(int frame)   const { return m_dataPool.gpuAddr(m_vtxOff[frame]); }
    void*     vtxCpuAddr(int frame)   const { return m_dataPool.cpuAddr(m_vtxOff[frame]); }
    DkGpuAddr idxGpuAddr(int frame)   const { return m_dataPool.gpuAddr(m_idxOff[frame]); }
    void*     idxCpuAddr(int frame)   const { return m_dataPool.cpuAddr(m_idxOff[frame]); }
    DkGpuAddr vsUboGpuAddr(int frame) const { return m_dataPool.gpuAddr(m_vsUboOff[frame]); }
    void*     vsUboCpuAddr(int frame) const { return m_dataPool.cpuAddr(m_vsUboOff[frame]); }
    DkGpuAddr fsUboGpuAddr(int frame) const { return m_dataPool.gpuAddr(m_fsUboOff[frame]); }
    void*     fsUboCpuAddr(int frame) const { return m_dataPool.cpuAddr(m_fsUboOff[frame]); }

    struct ImageAlloc {
        dk::MemBlock block;
        uint32_t     offset = 0;
        bool valid() const { return (bool)block; }
    };

    ImageAlloc allocImageFromPool(uint32_t size, uint32_t alignment = 0);
    void resetImagePool();
    dk::UniqueMemBlock allocImageMemory(uint32_t size);
    void freeImageMemory(uint32_t size);

    static constexpr uint64_t kDefaultImageBudget = 32u * 1024u * 1024u;
    uint64_t imageMemoryUsed() const { return m_imageMemUsed; }

    bool uploadTexture(dk::Image& dst, const void* pixels, uint32_t size, uint32_t width, uint32_t height);

    static constexpr int NUM_OFFSCREEN = 2;
    dk::Image&       offscreenImage(int i)       { return m_offImages[i]; }
    const dk::Image& offscreenImage(int i) const { return m_offImages[i]; }
    bool offscreenReady() const { return m_offscreenReady; }

    dk::Image&       fbImage(int i)       { return m_fbImages[i]; }
    const dk::Image& fbImage(int i) const { return m_fbImages[i]; }
    dk::Image&       dsImage()            { return m_dsImage; }
    const dk::Image& dsImage() const      { return m_dsImage; }
#else
    // SDL2-specific accessors
    SDL_Renderer* sdlRenderer() const { return m_sdlRenderer; }
    SDL_Window*   sdlWindow()   const { return m_sdlWindow; }
    int           slot()        const { return m_slot; }

    // Stubs for pool-based allocation (SDL2 uses SDL_CreateTexture directly)
    void resetImagePool() {}
    static constexpr int NUM_OFFSCREEN = 2;
    bool offscreenReady() const { return false; }
#endif

private:
#ifdef NXUI_BACKEND_DEKO3D
    void createFramebuffers();
    void createDepthStencil();
    void createOffscreenTargets();

    dk::UniqueDevice    m_dev;
    dk::UniqueQueue     m_queue;
    dk::UniqueCmdBuf    m_cmdbuf[NUM_FB];
    dk::UniqueCmdBuf    m_uploadCmdbuf;
    dk::UniqueSwapchain m_swapchain;

    GpuPool m_fbPool;
    GpuPool m_dsPool;
    GpuPool m_cmdPool[NUM_FB];
    GpuPool m_uploadCmdPool;
    GpuPool m_codePool;
    GpuPool m_dataPool;
    GpuPool m_imagePool;
    GpuPool m_stagingPool;

    dk::Image  m_fbImages[NUM_FB];
    dk::Image  m_dsImage;
    dk::Image  m_offImages[NUM_OFFSCREEN];
    GpuPool    m_offPool;
    bool       m_offscreenReady = false;

    // Per-slot fences: signalled in endFrame, waited in beginFrame, so that
    // the CPU never overwrites command/vertex memory the GPU is still reading.
    dk::Fence  m_frameFences[NUM_FB];

    uint32_t m_vtxOff[NUM_FB] {};
    uint32_t m_idxOff[NUM_FB] {};
    uint32_t m_vsUboOff[NUM_FB] {};
    uint32_t m_fsUboOff[NUM_FB] {};
    uint32_t m_imgDescOff = 0;
    uint32_t m_samDescOff = 0;

    uint64_t m_imageMemUsed = 0;
    uint64_t m_poolMemUsed  = 0;

    static constexpr uint32_t kImageChunkSize = 2u * 1024u * 1024u;
    struct ImagePoolChunk {
        dk::UniqueMemBlock block;
        uint32_t size = 0;
        uint32_t used = 0;
    };
    std::vector<ImagePoolChunk> m_imageChunks;
#else
    SDL_Window*   m_sdlWindow   = nullptr;
    SDL_Renderer* m_sdlRenderer = nullptr;
#endif
    int m_slot = -1;
};

} // namespace nxui
