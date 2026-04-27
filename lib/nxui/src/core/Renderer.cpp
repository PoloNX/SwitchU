#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Font.hpp>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace nxui {

static void ortho(float* m, float w, float h) {
    std::memset(m, 0, 16 * sizeof(float));
    m[ 0] =  2.f / w;
    m[ 5] = -2.f / h;
    m[10] = -1.f;
    m[12] = -1.f;
    m[13] =  1.f;
    m[15] =  1.f;
}

Renderer::Renderer(GpuDevice& gpu) : m_gpu(gpu) {
    m_clipStack.reserve(8);
    resetLiquidGlassSettings();
}
Renderer::~Renderer() {}

void Renderer::resetLiquidGlassSettings() {
    m_liquidGlassSettings = LiquidGlassSettings{};
}

bool Renderer::initialize() {
    std::printf("[Renderer] Loading shaders...\n");
    loadShaders();
    std::printf("[Renderer] Setting up sampler...\n");
    setupSampler();

    std::printf("[Renderer] Creating 1x1 white texture...\n");
    {
        dk::ImageLayout layout;
        dk::ImageLayoutMaker{m_gpu.device()}
            .setFlags(0)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(1, 1)
            .initialize(layout);

        m_whiteMemBlock = m_gpu.allocImageMemory(layout.getSize());
        m_whiteImage.initialize(layout, m_whiteMemBlock, 0);

        uint32_t white = 0xFFFFFFFF;
        m_gpu.uploadTexture(m_whiteImage, &white, 4, 1, 1);

        dk::ImageView view{m_whiteImage};
        int slot = registerTexture(view);
        std::printf("[Renderer] White texture registered at slot %d\n", slot);
    }

    if (m_gpu.offscreenReady()) {
        for (int i = 0; i < GpuDevice::NUM_OFFSCREEN; ++i) {
            dk::ImageView view{m_gpu.offscreenImage(i)};
            m_offDescSlot[i] = registerTexture(view);
            std::printf("[Renderer] Offscreen %d registered at slot %d\n", i, m_offDescSlot[i]);
        }
    }

    std::printf("[Renderer] Init complete\n");
    return true;
}

void Renderer::loadShaders() {
    auto loadDksh = [&](dk::Shader& out, const char* path) {
        std::printf("[Renderer] Loading shader: %s\n", path);
        FILE* f = std::fopen(path, "rb");
        if (!f) {
            std::printf("[Renderer] FAILED to open shader: %s\n", path);
            return;
        }
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);

        uint32_t off = m_gpu.codePool().alloc(sz, DK_SHADER_CODE_ALIGNMENT);
        if (off == UINT32_MAX) {
            std::printf("[Renderer] Code pool alloc FAILED for %ld bytes\n", sz);
            std::fclose(f);
            return;
        }
        void* dst = m_gpu.codePool().cpuAddr(off);
        std::fread(dst, 1, sz, f);
        std::fclose(f);

        dk::ShaderMaker{m_gpu.codePool().block, off}.initialize(out);
    };

    loadDksh(m_vertShaders[(int)ShaderProgram::Basic], (s_shaderBasePath + "basic_vsh.dksh").c_str());
    loadDksh(m_fragShaders[(int)ShaderProgram::Basic], (s_shaderBasePath + "basic_fsh.dksh").c_str());

    loadDksh(m_vertShaders[(int)ShaderProgram::Backdrop], (s_shaderBasePath + "basic_vsh.dksh").c_str());
    loadDksh(m_fragShaders[(int)ShaderProgram::Backdrop], (s_shaderBasePath + "backdrop_fsh.dksh").c_str());

    loadDksh(m_vertShaders[(int)ShaderProgram::BlurH], (s_shaderBasePath + "blur_pass_vsh.dksh").c_str());
    loadDksh(m_fragShaders[(int)ShaderProgram::BlurH], (s_shaderBasePath + "blur_h_fsh.dksh").c_str());

    loadDksh(m_vertShaders[(int)ShaderProgram::BlurV], (s_shaderBasePath + "blur_pass_vsh.dksh").c_str());
    loadDksh(m_fragShaders[(int)ShaderProgram::BlurV], (s_shaderBasePath + "blur_v_fsh.dksh").c_str());

    loadDksh(m_vertShaders[(int)ShaderProgram::Wave], (s_shaderBasePath + "pass_vsh.dksh").c_str());
    loadDksh(m_fragShaders[(int)ShaderProgram::Wave], (s_shaderBasePath + "wave_fsh.dksh").c_str());

    loadDksh(m_vertShaders[(int)ShaderProgram::LiquidGlass], (s_shaderBasePath + "pass_vsh.dksh").c_str());
    loadDksh(m_fragShaders[(int)ShaderProgram::LiquidGlass], (s_shaderBasePath + "liquid_glass_fsh.dksh").c_str());

    loadDksh(m_vertShaders[(int)ShaderProgram::Gradient], (s_shaderBasePath + "basic_vsh.dksh").c_str());
    loadDksh(m_fragShaders[(int)ShaderProgram::Gradient], (s_shaderBasePath + "gradient_fsh.dksh").c_str());
}

void Renderer::setupSampler() {
    dk::SamplerDescriptor samDesc;
    samDesc.initialize(
        dk::Sampler{}
            .setFilter(DkFilter_Linear, DkFilter_Linear)
            .setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge)
    );
    std::memcpy(m_gpu.samDescCpuAddr(), &samDesc, sizeof(samDesc));
}

int Renderer::registerTexture(const dk::ImageView& view) {
    if (m_nextDescSlot >= GpuDevice::MAX_TEXTURES) {
        std::fprintf(stderr, "[Renderer] texture descriptor overflow: slot=%d max=%d\n",
                     m_nextDescSlot, GpuDevice::MAX_TEXTURES);
        return -1;
    }
    int slot = m_nextDescSlot++;
    dk::ImageDescriptor desc;
    desc.initialize(view);
    auto* base = static_cast<dk::ImageDescriptor*>(m_gpu.imgDescCpuAddr());
    base[slot] = desc;
    m_descDirty = true;   // GPU descriptor cache must be invalidated before next draw
    return slot;
}

void Renderer::updateTexture(int slot, const dk::ImageView& view) {
    dk::ImageDescriptor desc;
    desc.initialize(view);
    auto* base = static_cast<dk::ImageDescriptor*>(m_gpu.imgDescCpuAddr());
    base[slot] = desc;
    m_descDirty = true;   // GPU descriptor cache must be invalidated before next draw
}

void Renderer::resetTextureSlots() {
    int firstFree = 1;
    if (m_gpu.offscreenReady())
        firstFree = 1 + GpuDevice::NUM_OFFSCREEN;
    m_nextDescSlot = firstFree;
    m_curTexSlot = -1;

    if (m_gpu.offscreenReady()) {
        for (int i = 0; i < GpuDevice::NUM_OFFSCREEN; ++i) {
            dk::ImageView view{m_gpu.offscreenImage(i)};
            dk::ImageDescriptor d;
            d.initialize(view);
            auto* base = static_cast<dk::ImageDescriptor*>(m_gpu.imgDescCpuAddr());
            base[m_offDescSlot[i]] = d;
        }
    }
}

void Renderer::bindTexture(int slot) {
    if (slot != m_curTexSlot) {
        bool newTexturing = (slot >= 0);
        if (newTexturing != m_texturing || slot != m_curTexSlot)
            flush();
        m_curTexSlot = slot;
        m_texturing = newTexturing;
    }
}

void Renderer::useShader(ShaderProgram prog) {
    if (prog == m_curShader) return;
    flush();
    m_curShader = prog;
    int idx = (int)prog;
    auto cmd = m_gpu.cmdBuf();
    cmd.bindShaders(DkStageFlag_GraphicsMask, {&m_vertShaders[idx], &m_fragShaders[idx]});
}

void Renderer::pushFsUniforms(const FsUniforms& fs) {
    flush();
    int slot = m_gpu.slot();
    auto cmd = m_gpu.cmdBuf();
    auto fsUboAddr = m_gpu.fsUboGpuAddr(slot);
    cmd.pushConstants(fsUboAddr, GpuDevice::FS_UBO_SIZE, 0, sizeof(fs), &fs);
    cmd.bindUniformBuffer(DkStage_Fragment, 1, fsUboAddr, GpuDevice::FS_UBO_SIZE);
}

void Renderer::beginFrame() {
    int slot = m_gpu.slot();
    m_vtxBase  = static_cast<Vertex2D*>(m_gpu.vtxCpuAddr(slot));
    m_vtxCount = 0;
    m_vtxBatchStart = 0;
    m_curTexSlot = -1;
    m_texturing  = false;
    m_curShader  = ShaderProgram::Basic;
    m_clipStack.clear();
    m_reusableOffscreenCaptureValid = false;

    auto cmd = m_gpu.cmdBuf();

    dk::ImageView colorTarget{m_gpu.fbImage(slot)};
    dk::ImageView dsTarget{m_gpu.dsImage()};
    cmd.bindRenderTargets(&colorTarget, &dsTarget);

    cmd.setViewports(0, DkViewport{0.f, 0.f,
        (float)m_gpu.width(), (float)m_gpu.height(), 0.f, 1.f});
    cmd.setScissors(0, DkScissor{0, 0, (uint32_t)m_gpu.width(), (uint32_t)m_gpu.height()});

    cmd.clearColor(0, DkColorMask_RGBA, 0.05f, 0.08f, 0.15f, 1.f);
    cmd.clearDepthStencil(false, 0.f, 0xFF, 0);

    int idx = (int)ShaderProgram::Basic;
    cmd.bindShaders(DkStageFlag_GraphicsMask, {&m_vertShaders[idx], &m_fragShaders[idx]});

    cmd.bindColorState(dk::ColorState{}.setBlendEnable(0, true));
    dk::BlendState blendState;
    blendState.setFactors(DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha,
                          DkBlendFactor_One, DkBlendFactor_InvSrcAlpha);
    DkBlendState rawBlendState = blendState;
    cmd.bindBlendStates(0, rawBlendState);

    cmd.bindDepthStencilState(dk::DepthStencilState{}.setDepthTestEnable(false));
    cmd.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));

    static const std::array<DkVtxAttribState, 3> attribs = {{
        DkVtxAttribState{0, 0, offsetof(Vertex2D, x), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex2D, u), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex2D, r), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
    }};
    static const DkVtxBufferState bufState = {sizeof(Vertex2D), 0};
    cmd.bindVtxAttribState(attribs);
    cmd.bindVtxBufferState(bufState);

    cmd.bindImageDescriptorSet(m_gpu.imgDescGpuAddr(), GpuDevice::MAX_TEXTURES);
    cmd.bindSamplerDescriptorSet(m_gpu.samDescGpuAddr(), GpuDevice::MAX_SAMPLERS);

    // Invalidate the GPU's descriptor cache so it re-reads all descriptors
    // that were updated via CPU memcpy since the last frame.
    cmd.barrier(DkBarrier_None, DkInvalidateFlags_Descriptors);
    m_descDirty = false;

    updateProjection();
}

void Renderer::endFrame() {
    flush();
}

void Renderer::updateProjection() {
    int slot = m_gpu.slot();
    auto* ubo = static_cast<uint8_t*>(m_gpu.vsUboCpuAddr(slot));
    VsUniforms vs;
    ortho(vs.projection, (float)m_gpu.width(), (float)m_gpu.height());
    std::memcpy(ubo, &vs, sizeof(vs));

    auto cmd = m_gpu.cmdBuf();
    cmd.bindUniformBuffer(DkStage_Vertex, 0, m_gpu.vsUboGpuAddr(slot), GpuDevice::VS_UBO_SIZE);
}

void Renderer::flush() {
    uint32_t batchVerts = m_vtxCount - m_vtxBatchStart;
    if (batchVerts == 0) return;

    auto cmd = m_gpu.cmdBuf();
    int slot = m_gpu.slot();

    // If any image descriptor was written via CPU memcpy since the last
    // barrier, invalidate the GPU's descriptor cache NOW, before the
    // draw call that may reference the updated descriptor.
    if (m_descDirty) {
        cmd.barrier(DkBarrier_None, DkInvalidateFlags_Descriptors);
        m_descDirty = false;
    }

    if (m_curShader == ShaderProgram::Basic) {
        FsUniforms fs = {};
        fs.useTexture = m_texturing ? 1 : 0;
        auto fsUboAddr = m_gpu.fsUboGpuAddr(slot);
        cmd.pushConstants(fsUboAddr, GpuDevice::FS_UBO_SIZE,
                          0, sizeof(int32_t) * 4, &fs);
        cmd.bindUniformBuffer(DkStage_Fragment, 1, fsUboAddr, GpuDevice::FS_UBO_SIZE);
    }

    int texSlot = (m_texturing && m_curTexSlot >= 0) ? m_curTexSlot : WHITE_TEX_SLOT;
    cmd.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(texSlot, 0));

    DkGpuAddr vtxAddr = m_gpu.vtxGpuAddr(slot) + m_vtxBatchStart * sizeof(Vertex2D);
    cmd.bindVtxBuffer(0, vtxAddr, batchVerts * sizeof(Vertex2D));

    cmd.draw(DkPrimitive_Triangles, batchVerts, 1, 0, 0);

    m_vtxBatchStart = m_vtxCount;
}

// Render target switching

void Renderer::bindRenderTarget(int offscreenIdx) {
    flush();
    auto cmd = m_gpu.cmdBuf();
    dk::ImageView colorTarget{m_gpu.offscreenImage(offscreenIdx)};
    cmd.bindRenderTargets(&colorTarget);

    constexpr uint32_t offW = GpuDevice::FB_WIDTH / 2;
    constexpr uint32_t offH = GpuDevice::FB_HEIGHT / 2;
    cmd.setViewports(0, DkViewport{0.f, 0.f, (float)offW, (float)offH, 0.f, 1.f});
    cmd.setScissors(0, DkScissor{0, 0, offW, offH});

    int slot = m_gpu.slot();
    VsUniforms vs;
    ortho(vs.projection, (float)offW, (float)offH);
    auto* ubo = static_cast<uint8_t*>(m_gpu.vsUboCpuAddr(slot));
    std::memcpy(ubo, &vs, sizeof(vs));
    cmd.bindUniformBuffer(DkStage_Vertex, 0, m_gpu.vsUboGpuAddr(slot), GpuDevice::VS_UBO_SIZE);
}

void Renderer::restoreRenderTarget() {
    flush();
    auto cmd = m_gpu.cmdBuf();
    int slot = m_gpu.slot();
    dk::ImageView colorTarget{m_gpu.fbImage(slot)};
    dk::ImageView dsTarget{m_gpu.dsImage()};
    cmd.bindRenderTargets(&colorTarget, &dsTarget);

    cmd.setViewports(0, DkViewport{0.f, 0.f,
        (float)m_gpu.width(), (float)m_gpu.height(), 0.f, 1.f});
    if (m_clipStack.empty()) {
        cmd.setScissors(0, DkScissor{0, 0, (uint32_t)m_gpu.width(), (uint32_t)m_gpu.height()});
    } else {
        auto& r = m_clipStack.back();
        cmd.setScissors(0, DkScissor{(uint32_t)std::max(0.f, r.x), (uint32_t)std::max(0.f, r.y),
                                     (uint32_t)std::max(0.f, r.width), (uint32_t)std::max(0.f, r.height)});
    }

    updateProjection();
}

void Renderer::captureToOffscreen(bool reuseIfValid) {
    if (reuseIfValid && m_reusableOffscreenCaptureValid) {
        return;
    }

    flush();
    auto cmd = m_gpu.cmdBuf();
    int slot = m_gpu.slot();

    // Ensure all current framebuffer writes are visible to the 2D blit engine
    // before we capture the scene behind the glass widget.
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);

    dk::ImageView src{m_gpu.fbImage(slot)};
    dk::ImageView dst{m_gpu.offscreenImage(0)};
    DkImageRect srcRect{
        0,
        0,
        0,
        (uint32_t)m_gpu.width(),
        (uint32_t)m_gpu.height(),
        1,
    };
    DkImageRect dstRect{
        0,
        0,
        0,
        (uint32_t)m_gpu.width() / 2,
        (uint32_t)m_gpu.height() / 2,
        1,
    };
    cmd.blitImage(src, srcRect, dst, dstRect, 0);

    // Barrier: ensure the blit completes before subsequent reads of offscreen 0
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);

    m_reusableOffscreenCaptureValid = reuseIfValid;
}

void Renderer::copyOffscreen(int srcTarget, int dstTarget) {
    if (!m_gpu.offscreenReady()) return;
    if (srcTarget < 0 || srcTarget >= GpuDevice::NUM_OFFSCREEN) return;
    if (dstTarget < 0 || dstTarget >= GpuDevice::NUM_OFFSCREEN) return;
    if (srcTarget == dstTarget) return;

    flush();

    auto cmd = m_gpu.cmdBuf();
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);

    dk::ImageView src{m_gpu.offscreenImage(srcTarget)};
    dk::ImageView dst{m_gpu.offscreenImage(dstTarget)};
    DkImageRect rect{
        0,
        0,
        0,
        (uint32_t)m_gpu.width() / 2,
        (uint32_t)m_gpu.height() / 2,
        1,
    };
    cmd.blitImage(src, rect, dst, rect, 0);
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);
}

void Renderer::drawOffscreen(int target, const Rect& dest, const Color& tint) {
    useShader(ShaderProgram::Backdrop);
    bindTexture(m_offDescSlot[target]);
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(), 0, 0, 1, 1, tint);
    flush();
    useShader(ShaderProgram::Basic);
}

void Renderer::drawOffscreenRounded(int target, const Rect& dest, float radius, const Color& tint) {
    if (target < 0 || target >= GpuDevice::NUM_OFFSCREEN) return;
    useShader(ShaderProgram::Backdrop);
    if (radius <= 0.f) {
        float u0 = dest.x / (float)m_gpu.width();
        float v0 = dest.y / (float)m_gpu.height();
        float u1 = dest.right() / (float)m_gpu.width();
        float v1 = dest.bottom() / (float)m_gpu.height();
        bindTexture(m_offDescSlot[target]);
        addQuad(dest.x, dest.y, dest.right(), dest.bottom(), u0, v0, u1, v1, tint);
        flush();
        useShader(ShaderProgram::Basic);
        return;
    }

    float rad = std::min(radius, std::min(dest.width, dest.height) * 0.5f);
    bindTexture(m_offDescSlot[target]);

    float cx = dest.x + dest.width * 0.5f;
    float cy = dest.y + dest.height * 0.5f;

    auto toUV = [&](float px, float py) -> Vec2 {
        return {
            px / (float)m_gpu.width(),
            py / (float)m_gpu.height(),
        };
    };

    constexpr int segs = 8;
    constexpr int maxPts = (segs + 1) * 4;
    const float pi2 = 3.14159265f * 0.5f;

    struct { float cx, cy; float a0; } corners[4] = {
        {dest.right() - rad, dest.y + rad,          0},
        {dest.x + rad,       dest.y + rad,          pi2},
        {dest.x + rad,       dest.bottom() - rad,   pi2 * 2},
        {dest.right() - rad, dest.bottom() - rad,   pi2 * 3},
    };

    Vec2 pts[maxPts];
    int ptCount = 0;
    for (auto& cn : corners) {
        for (int i = 0; i <= segs; ++i) {
            float a = cn.a0 + pi2 * i / segs;
            pts[ptCount++] = {cn.cx + std::cos(a) * rad,
                              cn.cy - std::sin(a) * rad};
        }
    }

    Vec2 cuv = toUV(cx, cy);
    for (int i = 0; i < ptCount; ++i) {
        auto& p0 = pts[i];
        auto& p1 = pts[(i + 1) % ptCount];
        Vec2 uv0 = toUV(p0.x, p0.y);
        Vec2 uv1 = toUV(p1.x, p1.y);
        addVertex(cx,  cy,  cuv.x, cuv.y, tint);
        addVertex(p0.x, p0.y, uv0.x, uv0.y, tint);
        addVertex(p1.x, p1.y, uv1.x, uv1.y, tint);
    }

    flush();
    useShader(ShaderProgram::Basic);
}

void Renderer::drawLiquidGlass(int target, const Rect& panelRect, float radius,
                               const Color& tint, float opacity, float shade) {
    if (opacity <= 0.01f || target < 0 || target >= GpuDevice::NUM_OFFSCREEN) return;
    if (!m_gpu.offscreenReady()) {
        drawRoundedRect(panelRect, tint.withAlpha(tint.a * opacity), radius);
        return;
    }

    useShader(ShaderProgram::LiquidGlass);

    const auto& lg = m_liquidGlassSettings;
    auto clamp01 = [](float value) {
        return std::clamp(value, 0.0f, 1.0f);
    };

    FsUniforms fs = {};
    fs.useTexture = 1;
    fs.param1 = lg.refractionIntensity;
    fs.param2 = lg.blurIntensity;
    fs.param3 = lg.noiseIntensity;

    fs.extra[0] = lg.glowIntensity;
    fs.extra[1] = lg.saturation;
    fs.extra[2] = clamp01(lg.opacityMultiplier);
    fs.extra[3] = lg.roughness;

    fs.extra[4] = lg.animSpeed;
    fs.extra[5] = lg.time;
    fs.extra[6] = lg.powerFactor;
    fs.extra[7] = lg.fPower;

    fs.extra[8] = lg.refA;
    fs.extra[9] = lg.refB;
    fs.extra[10] = lg.refC;
    fs.extra[11] = lg.refD;

    fs.extra[12] = lg.glowWeight;
    fs.extra[13] = lg.glowBias;
    fs.extra[14] = lg.glowEdge0;
    fs.extra[15] = lg.glowEdge1;

    fs.extra[16] = tint.r * lg.tintBoost.r;
    fs.extra[17] = tint.g * lg.tintBoost.g;
    fs.extra[18] = tint.b * lg.tintBoost.b;
    fs.extra[19] = tint.a * lg.tintBoost.a;

    fs.extra[20] = panelRect.x;
    fs.extra[21] = panelRect.y;
    fs.extra[22] = panelRect.width;
    fs.extra[23] = panelRect.height;

    fs.extra[24] = (float)m_gpu.width();
    fs.extra[25] = (float)m_gpu.height();
    fs.extra[26] = clamp01(shade);
    fs.extra[27] = 0.0f;

    pushFsUniforms(fs);
    bindTexture(m_offDescSlot[target]);
    addQuad(panelRect.x, panelRect.y, panelRect.right(), panelRect.bottom(), 0, 0, 1, 1,
            Color::white().withAlpha(opacity));
    flush();

    useShader(ShaderProgram::Basic);
}

void Renderer::applyBlur(float radius, int passes) {
    if (!m_gpu.offscreenReady()) return;

    m_reusableOffscreenCaptureValid = false;

    constexpr float offW = (float)(GpuDevice::FB_WIDTH / 2);
    constexpr float offH = (float)(GpuDevice::FB_HEIGHT / 2);

    for (int p = 0; p < passes; ++p) {
        // H blur: off0 -> off1
        bindRenderTarget(1);
        m_gpu.cmdBuf().clearColor(0, DkColorMask_RGBA, 0.f, 0.f, 0.f, 0.f);
        useShader(ShaderProgram::BlurH);
        FsUniforms fs = {};
        fs.useTexture = 1;
        fs.param1 = radius;
        fs.param2 = 1.f / offW;
        fs.param3 = 1.f / offH;
        pushFsUniforms(fs);
        bindTexture(m_offDescSlot[0]);
        addQuad(-1.f, -1.f, 1.f, 1.f, 0, 0, 1, 1, Color::white());
        flush();
        m_gpu.cmdBuf().barrier(DkBarrier_Full, DkInvalidateFlags_Image);

        // V blur: off1 -> off0
        bindRenderTarget(0);
        m_gpu.cmdBuf().clearColor(0, DkColorMask_RGBA, 0.f, 0.f, 0.f, 0.f);
        useShader(ShaderProgram::BlurV);
        pushFsUniforms(fs);
        bindTexture(m_offDescSlot[1]);
        addQuad(-1.f, -1.f, 1.f, 1.f, 0, 0, 1, 1, Color::white());
        flush();
        m_gpu.cmdBuf().barrier(DkBarrier_Full, DkInvalidateFlags_Image);
    }

    restoreRenderTarget();
    useShader(ShaderProgram::Basic);
}

void Renderer::applyWave(float time, float amplitude, float frequency) {
    if (!m_gpu.offscreenReady()) return;

    useShader(ShaderProgram::Wave);
    FsUniforms fs = {};
    fs.useTexture = 1;
    fs.param1 = time;
    fs.param2 = amplitude;
    fs.param3 = frequency;
    pushFsUniforms(fs);

    Rect dest = {0, 0, (float)m_gpu.width(), (float)m_gpu.height()};
    bindTexture(m_offDescSlot[0]);
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(), 0, 0, 1, 1, Color::white());
    flush();

    useShader(ShaderProgram::Basic);
}


// Geometry emission

void Renderer::addVertex(float x, float y, float u, float v, const Color& c) {
    if (m_vtxCount >= GpuDevice::MAX_VERTICES) {
        std::printf("[Renderer] WARN: vertex buffer full (%u)\n", m_vtxCount);
        return;
    }
    auto& vtx  = m_vtxBase[m_vtxCount++];
    vtx.x = x; vtx.y = y;
    vtx.u = u; vtx.v = v;
    vtx.r = c.r; vtx.g = c.g; vtx.b = c.b; vtx.a = c.a;
}

void Renderer::addQuad(float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1,
                        const Color& c)
{
    if (m_vtxCount + 6 > GpuDevice::MAX_VERTICES) flush();
    addVertex(x0, y0, u0, v0, c);
    addVertex(x1, y0, u1, v0, c);
    addVertex(x1, y1, u1, v1, c);
    addVertex(x0, y0, u0, v0, c);
    addVertex(x1, y1, u1, v1, c);
    addVertex(x0, y1, u0, v1, c);
}

void Renderer::addQuadGrad(float x0, float y0, float x1, float y1,
                            float u0, float v0, float u1, float v1,
                            const Color& cTop, const Color& cBot)
{
    if (m_vtxCount + 6 > GpuDevice::MAX_VERTICES) flush();
    addVertex(x0, y0, u0, v0, cTop);
    addVertex(x1, y0, u1, v0, cTop);
    addVertex(x1, y1, u1, v1, cBot);
    addVertex(x0, y0, u0, v0, cTop);
    addVertex(x1, y1, u1, v1, cBot);
    addVertex(x0, y1, u0, v1, cBot);
}

void Renderer::drawRect(const Rect& r, const Color& c) {
    bindTexture(-1);
    addQuad(r.x, r.y, r.right(), r.bottom(), 0, 0, 1, 1, c);
}

void Renderer::drawRectOutline(const Rect& r, const Color& c, float t) {
    drawRect({r.x, r.y, r.width, t}, c);
    drawRect({r.x, r.bottom() - t, r.width, t}, c);
    drawRect({r.x, r.y + t, t, r.height - 2*t}, c);
    drawRect({r.right()-t, r.y + t, t, r.height - 2*t}, c);
}

void Renderer::drawGradientRect(const Rect& r, const Color& top, const Color& bottom) {
    bindTexture(-1);
    addQuadGrad(r.x, r.y, r.right(), r.bottom(), 0, 0, 1, 1, top, bottom);
}

void Renderer::drawRoundedRect(const Rect& r, const Color& c, float radius) {
    if (radius <= 0.f) { drawRect(r, c); return; }
    float rad = std::min(radius, std::min(r.width, r.height) * 0.5f);

    bindTexture(-1);

    auto cx = r.x + r.width * 0.5f;
    auto cy = r.y + r.height * 0.5f;

    constexpr int segs = 8;
    constexpr int maxPts = (segs + 1) * 4;
    const float pi2 = 3.14159265f * 0.5f;

    struct {float cx, cy; float a0;} corners[4] = {
        {r.right() - rad, r.y + rad,          0},
        {r.x + rad,       r.y + rad,          pi2},
        {r.x + rad,       r.bottom() - rad,   pi2*2},
        {r.right() - rad, r.bottom() - rad,   pi2*3},
    };

    Vec2 pts[maxPts];
    int ptCount = 0;
    for (auto& cn : corners) {
        for (int i = 0; i <= segs; ++i) {
            float a = cn.a0 + pi2 * i / segs;
            pts[ptCount++] = {cn.cx + std::cos(a) * rad, cn.cy - std::sin(a) * rad};
        }
    }

    for (int i = 0; i < ptCount; ++i) {
        auto& p0 = pts[i];
        auto& p1 = pts[(i + 1) % ptCount];
        addVertex(cx, cy, 0, 0, c);
        addVertex(p0.x, p0.y, 0, 0, c);
        addVertex(p1.x, p1.y, 0, 0, c);
    }
}

void Renderer::drawRoundedRectOutline(const Rect& r, const Color& c, float radius, float t) {
    if (radius <= 0.f) { drawRectOutline(r, c, t); return; }
    float rad = std::min(radius, std::min(r.width, r.height) * 0.5f);

    bindTexture(-1);

    constexpr int segs = 8;
    constexpr int maxPts = (segs + 1) * 4;
    const float pi2 = 3.14159265f * 0.5f;

    struct {float cx, cy; float a0;} corners[4] = {
        {r.right() - rad, r.y + rad,          0},
        {r.x + rad,       r.y + rad,          pi2},
        {r.x + rad,       r.bottom() - rad,   pi2*2},
        {r.right() - rad, r.bottom() - rad,   pi2*3},
    };

    Vec2 outer[maxPts], inner[maxPts];
    int ptCount = 0;
    for (auto& cn : corners) {
        for (int i = 0; i <= segs; ++i) {
            float a = cn.a0 + pi2 * i / segs;
            float ca = std::cos(a), sa = std::sin(a);
            outer[ptCount] = {cn.cx + ca * rad,       cn.cy - sa * rad};
            inner[ptCount] = {cn.cx + ca * (rad - t), cn.cy - sa * (rad - t)};
            ptCount++;
        }
    }

    for (int i = 0; i < ptCount; ++i) {
        int j = (i + 1) % ptCount;
        addVertex(outer[i].x, outer[i].y, 0, 0, c);
        addVertex(inner[i].x, inner[i].y, 0, 0, c);
        addVertex(outer[j].x, outer[j].y, 0, 0, c);

        addVertex(inner[i].x, inner[i].y, 0, 0, c);
        addVertex(inner[j].x, inner[j].y, 0, 0, c);
        addVertex(outer[j].x, outer[j].y, 0, 0, c);
    }
}

void Renderer::drawCircle(const Vec2& center, float radius, const Color& c, int segments) {
    bindTexture(-1);
    const float pi2 = 3.14159265f * 2.f;
    for (int i = 0; i < segments; ++i) {
        float a0 = pi2 * i / segments;
        float a1 = pi2 * (i + 1) / segments;
        addVertex(center.x, center.y, 0, 0, c);
        addVertex(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, 0, 0, c);
        addVertex(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, 0, 0, c);
    }
}

void Renderer::drawTriangle(const Vec2& p1, const Vec2& p2, const Vec2& p3, const Color& c) {
    bindTexture(-1);
    addVertex(p1.x, p1.y, 0, 0, c);
    addVertex(p2.x, p2.y, 0, 0, c);
    addVertex(p3.x, p3.y, 0, 0, c);
}

void Renderer::drawLine(const Vec2& from, const Vec2& to, const Color& c, float thickness) {
    Vec2 d = (to - from).normalized();
    Vec2 n = {-d.y, d.x};
    float ht = thickness * 0.5f;
    Vec2 a = from + n * ht, b = from - n * ht;
    Vec2 cc = to + n * ht,  dd = to - n * ht;
    bindTexture(-1);
    addVertex(a.x, a.y, 0, 0, c);
    addVertex(b.x, b.y, 0, 0, c);
    addVertex(cc.x, cc.y, 0, 0, c);
    addVertex(b.x, b.y, 0, 0, c);
    addVertex(dd.x, dd.y, 0, 0, c);
    addVertex(cc.x, cc.y, 0, 0, c);
}

void Renderer::drawTexture(const Texture* tex, const Rect& dest, const Color& tint) {
    if (!tex) return;
    bindTexture(tex->descriptorSlot());
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(), 0, 0, 1, 1, tint);
}

void Renderer::drawTextureSub(const Texture* tex, const Rect& src, const Rect& dest, const Color& tint) {
    if (!tex) return;
    float tw = (float)tex->width(), th = (float)tex->height();
    float u0 = src.x / tw, v0 = src.y / th;
    float u1 = src.right() / tw, v1 = src.bottom() / th;
    bindTexture(tex->descriptorSlot());
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(), u0, v0, u1, v1, tint);
}

void Renderer::drawTextureRounded(const Texture* tex, const Rect& dest, float radius, const Color& tint) {
    if (!tex) { return; }
    if (radius <= 0) { drawTexture(tex, dest, tint); return; }
    float rad = std::min(radius, std::min(dest.width, dest.height) * 0.5f);

    bindTexture(tex->descriptorSlot());

    float fcx = dest.x + dest.width * 0.5f;
    float fcy = dest.y + dest.height * 0.5f;

    auto toUV = [&](float px, float py) -> Vec2 {
        return {(px - dest.x) / dest.width,
                (py - dest.y) / dest.height};
    };

    constexpr int segs = 8;
    constexpr int maxPts = (segs + 1) * 4;
    const float pi2 = 3.14159265f * 0.5f;

    struct { float cx, cy; float a0; } corners[4] = {
        {dest.right() - rad, dest.y + rad,          0},
        {dest.x + rad,       dest.y + rad,          pi2},
        {dest.x + rad,       dest.bottom() - rad,   pi2 * 2},
        {dest.right() - rad, dest.bottom() - rad,   pi2 * 3},
    };

    Vec2 pts[maxPts];
    int ptCount = 0;
    for (auto& cn : corners) {
        for (int i = 0; i <= segs; ++i) {
            float a = cn.a0 + pi2 * i / segs;
            pts[ptCount++] = {cn.cx + std::cos(a) * rad,
                              cn.cy - std::sin(a) * rad};
        }
    }

    Vec2 cuv = toUV(fcx, fcy);
    for (int i = 0; i < ptCount; ++i) {
        auto& p0 = pts[i];
        auto& p1 = pts[(i + 1) % ptCount];
        Vec2 uv0 = toUV(p0.x, p0.y);
        Vec2 uv1 = toUV(p1.x, p1.y);
        addVertex(fcx,  fcy,  cuv.x, cuv.y, tint);
        addVertex(p0.x, p0.y, uv0.x, uv0.y, tint);
        addVertex(p1.x, p1.y, uv1.x, uv1.y, tint);
    }
}

void Renderer::drawText(const std::string& text, const Vec2& pos, Font* font,
                         const Color& color, float scale) {
    if (!font || text.empty()) return;
    font->draw(*this, text, pos, color, scale);
}

void Renderer::pushClipRect(const Rect& r) {
    flush();
    Rect clip = r;
    if (!m_clipStack.empty()) {
        auto& prev = m_clipStack.back();
        float x0 = std::max(clip.x, prev.x);
        float y0 = std::max(clip.y, prev.y);
        float x1 = std::min(clip.right(), prev.right());
        float y1 = std::min(clip.bottom(), prev.bottom());
        clip = {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
    }
    m_clipStack.push_back(clip);
    m_gpu.cmdBuf().setScissors(0, DkScissor{
        (uint32_t)std::max(0.f, clip.x), (uint32_t)std::max(0.f, clip.y),
        (uint32_t)std::max(0.f, clip.width), (uint32_t)std::max(0.f, clip.height)});
}

void Renderer::popClipRect() {
    flush();
    if (!m_clipStack.empty()) m_clipStack.pop_back();
    if (m_clipStack.empty()) {
        m_gpu.cmdBuf().setScissors(0, DkScissor{
            0, 0, (uint32_t)m_gpu.width(), (uint32_t)m_gpu.height()});
    } else {
        auto& r = m_clipStack.back();
        m_gpu.cmdBuf().setScissors(0, DkScissor{
            (uint32_t)std::max(0.f, r.x), (uint32_t)std::max(0.f, r.y),
            (uint32_t)std::max(0.f, r.width), (uint32_t)std::max(0.f, r.height)});
    }
}

} // namespace nxui
