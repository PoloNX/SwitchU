#include <nxui/core/GpuDevice.hpp>
#include <cstdio>
#include <cstring>
#include <array>

namespace nxui {

bool GpuDevice::initialize() {
    m_dev   = dk::DeviceMaker{}.create();
    m_queue = dk::QueueMaker{m_dev}.setFlags(DkQueueFlags_Graphics).create();

    for (int i = 0; i < NUM_FB; ++i) {
        m_cmdPool[i].create(m_dev, CMD_BUF_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        m_cmdbuf[i] = dk::CmdBufMaker{m_dev}.create();
        m_cmdbuf[i].addMemory(m_cmdPool[i].block, 0, CMD_BUF_SIZE);
    }

    m_uploadCmdPool.create(m_dev, 64 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    m_uploadCmdbuf = dk::CmdBufMaker{m_dev}.create();
    m_uploadCmdbuf.addMemory(m_uploadCmdPool.block, 0, 64 * 1024);

    m_codePool.create(m_dev, CODE_POOL_SIZE,
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);

    uint32_t dataSize = 0;
    for (int i = 0; i < NUM_FB; ++i) {
        dataSize += VTX_BUF_SIZE + 256;
        dataSize += IDX_BUF_SIZE + 256;
        dataSize += VS_UBO_SIZE + 256;
        dataSize += FS_UBO_SIZE + 256;
    }
    dataSize += MAX_TEXTURES * sizeof(DkImageDescriptor) + DK_IMAGE_DESCRIPTOR_ALIGNMENT;
    dataSize += MAX_SAMPLERS * sizeof(DkSamplerDescriptor) + DK_SAMPLER_DESCRIPTOR_ALIGNMENT;
    dataSize = (dataSize + kGpuAlign - 1) & ~(kGpuAlign - 1);
    m_dataPool.create(m_dev, dataSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

    for (int i = 0; i < NUM_FB; ++i) {
        m_vtxOff[i]   = m_dataPool.alloc(VTX_BUF_SIZE, 256);
        m_idxOff[i]   = m_dataPool.alloc(IDX_BUF_SIZE, 256);
        m_vsUboOff[i] = m_dataPool.alloc(VS_UBO_SIZE, DK_UNIFORM_BUF_ALIGNMENT);
        m_fsUboOff[i] = m_dataPool.alloc(FS_UBO_SIZE, DK_UNIFORM_BUF_ALIGNMENT);
    }
    m_imgDescOff = m_dataPool.alloc(MAX_TEXTURES * sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT);
    m_samDescOff = m_dataPool.alloc(MAX_SAMPLERS * sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT);

    m_stagingPool.create(m_dev, 256 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

    createFramebuffers();
    createDepthStencil();
    createOffscreenTargets();

    std::array<DkImage const*, NUM_FB> fbArray;
    for (int i = 0; i < NUM_FB; ++i) fbArray[i] = &m_fbImages[i];
    m_swapchain = dk::SwapchainMaker{m_dev, nwindowGetDefault(), fbArray}.create();

    return true;
}

void GpuDevice::createFramebuffers() {
    dk::ImageLayout fbLayout;
    dk::ImageLayoutMaker{m_dev}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
                  DkImageFlags_Usage2DEngine | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(FB_WIDTH, FB_HEIGHT)
        .initialize(fbLayout);

    uint64_t fbSize  = fbLayout.getSize();
    uint64_t fbAlign = fbLayout.getAlignment();
    uint32_t totalFb = 0;
    for (int i = 0; i < NUM_FB; ++i) {
        totalFb = (totalFb + fbAlign - 1) & ~(fbAlign - 1);
        totalFb += fbSize;
    }
    m_fbPool.create(m_dev, totalFb, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);

    uint32_t off = 0;
    for (int i = 0; i < NUM_FB; ++i) {
        off = (off + fbAlign - 1) & ~(fbAlign - 1);
        m_fbImages[i].initialize(fbLayout, m_fbPool.block, off);
        off += fbSize;
    }
}

void GpuDevice::createDepthStencil() {
    dk::ImageLayout dsLayout;
    dk::ImageLayoutMaker{m_dev}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_S8)
        .setDimensions(FB_WIDTH, FB_HEIGHT)
        .initialize(dsLayout);

    uint32_t dsSize = dsLayout.getSize();
    dsSize = (dsSize + kGpuAlign - 1) & ~(kGpuAlign - 1);
    m_dsPool.create(m_dev, dsSize, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    m_dsImage.initialize(dsLayout, m_dsPool.block, 0);
}

void GpuDevice::createOffscreenTargets() {
    // Half-resolution offscreen targets for blur (640x360)
    constexpr uint32_t offW = FB_WIDTH / 2;
    constexpr uint32_t offH = FB_HEIGHT / 2;

    dk::ImageLayout offLayout;
    dk::ImageLayoutMaker{m_dev}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(offW, offH)
        .initialize(offLayout);

    uint64_t offSize  = offLayout.getSize();
    uint64_t offAlign = offLayout.getAlignment();
    uint32_t totalOff = 0;
    for (int i = 0; i < NUM_OFFSCREEN; ++i) {
        totalOff = (totalOff + offAlign - 1) & ~(offAlign - 1);
        totalOff += offSize;
    }
    m_offPool.create(m_dev, totalOff, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);

    uint32_t off = 0;
    for (int i = 0; i < NUM_OFFSCREEN; ++i) {
        off = (off + offAlign - 1) & ~(offAlign - 1);
        m_offImages[i].initialize(offLayout, m_offPool.block, off);
        off += offSize;
    }
    m_offscreenReady = true;
}

int GpuDevice::beginFrame() {
    m_slot = m_queue.acquireImage(m_swapchain);

    // Wait for the GPU to finish the PREVIOUS frame that used this slot's
    // command memory / vertex buffer before we overwrite them.
    m_frameFences[m_slot].wait();

    // Reset command buffer and re-feed its memory (clear invalidates memory
    // tracking — following the deko3d sample framework pattern).
    m_cmdbuf[m_slot].clear();
    m_cmdbuf[m_slot].addMemory(m_cmdPool[m_slot].block, 0, CMD_BUF_SIZE);
    return m_slot;
}

void GpuDevice::endFrame() {
    // Signal the fence for this slot so the NEXT time beginFrame()
    // acquires the same slot, it can wait for completion.
    m_cmdbuf[m_slot].signalFence(m_frameFences[m_slot]);

    auto cmdList = m_cmdbuf[m_slot].finishList();
    m_queue.submitCommands(cmdList);
    m_queue.presentImage(m_swapchain, m_slot);
}

void GpuDevice::waitIdle() {
    if (m_queue) m_queue.waitIdle();
}

dk::UniqueMemBlock GpuDevice::allocImageMemory(uint32_t size) {
    size = (size + kGpuAlign - 1) & ~(kGpuAlign - 1);
    if (m_imageMemUsed + size > kDefaultImageBudget) {
        std::fprintf(stderr, "[GpuDevice] image budget exceeded (%llu + %u > %llu), skipping\n",
                     (unsigned long long)m_imageMemUsed, size,
                     (unsigned long long)kDefaultImageBudget);
        return {};  // return empty MemBlock — caller should check validity
    }
    auto blk = dk::MemBlockMaker{m_dev, size}
        .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        .create();
    m_imageMemUsed += size;
    return blk;
}

void GpuDevice::freeImageMemory(uint32_t size) {
    size = (size + kGpuAlign - 1) & ~(kGpuAlign - 1);
    if (size <= m_imageMemUsed)
        m_imageMemUsed -= size;
    else
        m_imageMemUsed = 0;
}

GpuDevice::ImageAlloc GpuDevice::allocImageFromPool(uint32_t size, uint32_t alignment) {
    if (alignment < kGpuAlign) alignment = kGpuAlign;
    size = (size + kGpuAlign - 1) & ~(kGpuAlign - 1);
    if (m_imageMemUsed + size > kDefaultImageBudget) {
        std::fprintf(stderr, "[GpuDevice] pool budget exceeded (%llu + %u > %llu)\n",
                     (unsigned long long)m_imageMemUsed, size,
                     (unsigned long long)kDefaultImageBudget);
        return {};
    }
    // Try to fit in an existing chunk
    for (auto& chunk : m_imageChunks) {
        uint32_t aligned = (chunk.used + alignment - 1) & ~(alignment - 1);
        if (aligned + size <= chunk.size) {
            chunk.used = aligned + size;
            m_imageMemUsed += size;
            m_poolMemUsed  += size;
            return {chunk.block, aligned};
        }
    }
    // Allocate a new chunk
    uint32_t chunkSize = std::max(kImageChunkSize, size);
    chunkSize = (chunkSize + kGpuAlign - 1) & ~(kGpuAlign - 1);
    auto blk = dk::MemBlockMaker{m_dev, chunkSize}
        .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        .create();
    m_imageChunks.push_back({std::move(blk), chunkSize, size});
    m_imageMemUsed += size;
    m_poolMemUsed  += size;
    return {m_imageChunks.back().block, 0};
}

void GpuDevice::resetImagePool() {
    m_imageChunks.clear();
    // Only subtract pool memory; individual allocations remain tracked.
    if (m_poolMemUsed <= m_imageMemUsed)
        m_imageMemUsed -= m_poolMemUsed;
    else
        m_imageMemUsed = 0;
    m_poolMemUsed = 0;
}

bool GpuDevice::uploadTexture(dk::Image& dst, const void* pixels, uint32_t size,
                              uint32_t w, uint32_t h)
{
    const void* srcCpu = nullptr;
    DkGpuAddr srcGpu = 0;
    dk::UniqueMemBlock tempStaging;

    if (size <= m_stagingPool.size) {
        srcCpu = m_stagingPool.cpuBase;
        srcGpu = m_stagingPool.block.getGpuAddr();
    } else {
        uint32_t allocSize = (size + kGpuAlign - 1) & ~(kGpuAlign - 1);
        tempStaging = dk::MemBlockMaker{m_dev, allocSize}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();

        srcCpu = tempStaging.getCpuAddr();
        srcGpu = tempStaging.getGpuAddr();

        if (!srcCpu || !srcGpu) {
            std::fprintf(stderr, "[GpuDevice] Failed temp staging allocation for texture (%u bytes)\n", size);
            return false;
        }
    }

    std::memcpy(const_cast<void*>(srcCpu), pixels, size);

    // Use dedicated upload command buffer
    m_uploadCmdbuf.clear();
    m_uploadCmdbuf.addMemory(m_uploadCmdPool.block, 0, 64 * 1024);

    dk::ImageView view{dst};
    m_uploadCmdbuf.copyBufferToImage(
        {srcGpu, 0, 0},
        view,
        {0, 0, 0, w, h, 1}
    );

    m_queue.submitCommands(m_uploadCmdbuf.finishList());
    m_queue.waitIdle();
    return true;
}

void GpuDevice::shutdown() {
    if (m_queue) m_queue.waitIdle();
    m_swapchain    = {};
    for (int i = 0; i < NUM_FB; ++i)
        m_cmdbuf[i] = {};
    m_uploadCmdbuf = {};
    m_queue        = {};
    m_imageChunks.clear();
    m_imageMemUsed = 0;
    m_poolMemUsed  = 0;
    m_fbPool      = {};
    m_dsPool      = {};
    m_offPool     = {};
    m_offscreenReady = false;
    for (int i = 0; i < NUM_FB; ++i)
        m_cmdPool[i] = {};
    m_uploadCmdPool = {};
    m_codePool    = {};
    m_dataPool  = {};
    m_imagePool = {};
    m_stagingPool = {};
    m_dev       = {};
}

} // namespace nxui
