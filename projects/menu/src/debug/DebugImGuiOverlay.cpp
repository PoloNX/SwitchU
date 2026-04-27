#include "DebugImGuiOverlay.hpp"
#include "settings/SettingsGlassTuning.hpp"

#ifdef SWITCHU_DEBUG_UI

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" int execvp(const char*, char* const[]) {
    return -1;
}

extern "C" int waitpid(int, int*, int) {
    return -1;
}

namespace {

static uint32_t alignUp(uint32_t size, uint32_t alignment) {
    return (size + alignment - 1u) & ~(alignment - 1u);
}

static void ortho(float* m, float width, float height) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / width;
    m[5] = -2.0f / height;
    m[10] = -1.0f;
    m[12] = -1.0f;
    m[13] = 1.0f;
    m[15] = 1.0f;
}

static bool loadShader(nxui::GpuDevice& gpu, dk::Shader& shader, const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        std::fprintf(stderr, "[DebugImGuiOverlay] failed to open shader %s\n", path);
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    uint32_t offset = gpu.codePool().alloc((uint32_t)size, DK_SHADER_CODE_ALIGNMENT);
    if (offset == UINT32_MAX) {
        std::fprintf(stderr, "[DebugImGuiOverlay] code pool allocation failed for %s\n", path);
        std::fclose(file);
        return false;
    }

    std::fread(gpu.codePool().cpuAddr(offset), 1, (size_t)size, file);
    std::fclose(file);

    dk::ShaderMaker{gpu.codePool().block, offset}.initialize(shader);
    return true;
}

} // namespace

DebugImGuiOverlay::~DebugImGuiOverlay() = default;

bool DebugImGuiOverlay::initialize(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    if (m_initialized) return true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendRendererName = "switchu_imgui_deko3d";
    io.BackendPlatformName = "switchu_touch";
    io.DisplaySize = ImVec2((float)gpu.width(), (float)gpu.height());

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 14.0f;
    style.FrameRounding = 10.0f;
    style.GrabRounding = 10.0f;
    style.ScrollbarRounding = 10.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    if (!loadShaders(gpu)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!createFontTexture(gpu, ren)) {
        ImGui::DestroyContext();
        return false;
    }

    uint32_t uboSize = alignUp(sizeof(VertUbo), DK_MEMBLOCK_ALIGNMENT);
    m_uboMemBlock = dk::MemBlockMaker(gpu.device(), uboSize)
        .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
        .create();

    m_initialized = (bool)m_uboMemBlock;
    return m_initialized;
}

void DebugImGuiOverlay::shutdown(nxui::GpuDevice& gpu) {
    if (!m_initialized) return;

    gpu.waitIdle();
    m_vertexMemBlock = {};
    m_indexMemBlock = {};
    m_vertexCapacity = {};
    m_indexCapacity = {};
    m_uboMemBlock = {};
    m_fontMemBlock = {};
    m_fontImage = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_fontTextureSlot = -1;
    m_wantsTouchCapture = false;

    if (ImGui::GetCurrentContext()) {
        ImGui::DestroyContext();
    }

    m_initialized = false;
}

bool DebugImGuiOverlay::loadShaders(nxui::GpuDevice& gpu) {
    const std::string& shaderBase = nxui::Renderer::shaderBasePath();
    const std::string vertPath = shaderBase + "imgui_vsh.dksh";
    const std::string fragPath = shaderBase + "imgui_fsh.dksh";

    std::fprintf(stderr, "[DebugImGuiOverlay] shader base: %s\n", shaderBase.c_str());
    return loadShader(gpu, m_vertexShader, vertPath.c_str()) &&
           loadShader(gpu, m_fragmentShader, fragPath.c_str());
}

bool DebugImGuiOverlay::createFontTexture(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0) {
        return false;
    }

    dk::ImageLayout layout;
    dk::ImageLayoutMaker{gpu.device()}
        .setFlags(0)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions((uint32_t)width, (uint32_t)height)
        .initialize(layout);

    m_fontMemBlock = gpu.allocImageMemory(layout.getSize());
    if (!m_fontMemBlock) {
        return false;
    }

    m_fontImage.initialize(layout, m_fontMemBlock, 0);
    if (!gpu.uploadTexture(m_fontImage, pixels, (uint32_t)(width * height * 4),
                           (uint32_t)width, (uint32_t)height)) {
        return false;
    }

    dk::ImageView view{m_fontImage};
    m_fontTextureSlot = ren.registerTexture(view);
    if (m_fontTextureSlot < 0) {
        return false;
    }

    io.Fonts->SetTexID((ImTextureID)(intptr_t)m_fontTextureSlot);
    return true;
}

bool DebugImGuiOverlay::ensureDrawBuffers(nxui::GpuDevice& gpu, int slot,
                                          size_t vertexBytes, size_t indexBytes) {
    size_t minVertexBytes = std::max(vertexBytes, (size_t)DK_MEMBLOCK_ALIGNMENT * 16u);
    size_t minIndexBytes = std::max(indexBytes, (size_t)DK_MEMBLOCK_ALIGNMENT * 16u);

    if (!m_vertexMemBlock[slot] || m_vertexCapacity[slot] < minVertexBytes) {
        size_t allocBytes = alignUp((uint32_t)(minVertexBytes * 2u), DK_MEMBLOCK_ALIGNMENT);
        m_vertexMemBlock[slot] = dk::MemBlockMaker(gpu.device(), allocBytes)
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
        m_vertexCapacity[slot] = m_vertexMemBlock[slot] ? allocBytes : 0;
    }

    if (!m_indexMemBlock[slot] || m_indexCapacity[slot] < minIndexBytes) {
        size_t allocBytes = alignUp((uint32_t)(minIndexBytes * 2u), DK_MEMBLOCK_ALIGNMENT);
        m_indexMemBlock[slot] = dk::MemBlockMaker(gpu.device(), allocBytes)
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
        m_indexCapacity[slot] = m_indexMemBlock[slot] ? allocBytes : 0;
    }

    return (bool)m_vertexMemBlock[slot] && (bool)m_indexMemBlock[slot];
}

void DebugImGuiOverlay::updateInput(nxui::GpuDevice& gpu, const nxui::Input& input) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gpu.width(), (float)gpu.height());
    io.DeltaTime = std::max(m_deltaTime, 1.0f / 240.0f);

    if (input.isTouching()) {
        io.AddMousePosEvent(input.touchX(), input.touchY());
    } else {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    if (input.touchDown()) io.AddMouseButtonEvent(0, true);
    if (input.touchUp()) io.AddMouseButtonEvent(0, false);
}

void DebugImGuiOverlay::drawWindow(nxui::Renderer& ren, nxui::Input& input, bool& open) {
    auto& lg = ren.liquidGlassSettings();
    auto& settingsGlass = settings::debug::settingsGlassTuning();
    bool rawBackdrop = ren.liquidGlassDebugRawBackdrop();

    ImGui::SetNextWindowSize(ImVec2(360.0f, 620.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Settings", &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Reset Defaults")) {
        ren.resetLiquidGlassSettings();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Raw Backdrop", &rawBackdrop)) {
        ren.setLiquidGlassDebugRawBackdrop(rawBackdrop);
    }

    ImGui::SeparatorText("Backdrop Capture");
    ImGui::TextUnformatted("Preview of offscreen texture 0");
    ImGui::Image((ImTextureID)(intptr_t)ren.offscreenDescSlot(0), ImVec2(240.0f, 135.0f));
    
    if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Power", &lg.powerFactor, 1.001f, 6.0f);
    }

    if (ImGui::CollapsingHeader("Blur & Noise", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Blur Radius", &lg.blurIntensity, 0.0f, 10.0f);
        ImGui::SliderFloat("Noise", &lg.noiseIntensity, 0.0f, 0.3f);
    }

    if (ImGui::CollapsingHeader("Refraction", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("f(x) = 1 - b (ce)^(-dx-a)");
        ImGui::SliderFloat("f(x) Power", &lg.fPower, -1.5f, 6.0f);
        ImGui::SliderFloat("a", &lg.refA, 0.0f, 5.0f);
        ImGui::SliderFloat("b", &lg.refB, 0.0f, 6.0f);
        ImGui::SliderFloat("c", &lg.refC, 0.0f, 6.0f);
        ImGui::SliderFloat("d", &lg.refD, 0.0f, 10.0f);
    }

    if (ImGui::CollapsingHeader("Glow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Glow weight", &lg.glowWeight, -1.0f, 1.0f);
        ImGui::SliderFloat("Glow bias", &lg.glowBias, -1.0f, 1.0f);
        ImGui::SliderFloat("Glow edge0", &lg.glowEdge0, -1.0f, 1.0f);
        ImGui::SliderFloat("Glow edge1", &lg.glowEdge1, -1.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Advanced")) {
        ImGui::SliderFloat("Refraction mix", &lg.refractionIntensity, 0.0f, 1.5f);
        ImGui::SliderFloat("Glow intensity", &lg.glowIntensity, 0.0f, 1.5f);
        ImGui::SliderFloat("Saturation", &lg.saturation, 0.0f, 2.0f);
        ImGui::SliderFloat("Reflection", &lg.opacityMultiplier, 0.0f, 1.5f);
        ImGui::SliderFloat("Roughness", &lg.roughness, 0.0f, 1.0f);
        ImGui::Checkbox("Auto Time", &m_autoAnimateTime);
        if (!m_autoAnimateTime) {
            ImGui::SliderFloat("Time", &lg.time, 0.0f, 120.0f);
        } else {
            ImGui::Text("Time: %.2f", lg.time);
        }
        ImGui::SliderFloat("Anim speed", &lg.animSpeed, 0.0f, 6.0f);
        ImGui::SliderFloat4("Tint RGBA", &lg.tintBoost.r, 0.0f, 2.0f);
    }

    ImGui::SeparatorText("Settings Panel");
    if (ImGui::Button("Reset Settings Panel")) {
        settings::debug::resetSettingsGlassTuning();
    }
    ImGui::TextUnformatted("Tune the cached backdrop blur first, then the glass pass.");
    ImGui::TextUnformatted("If the blur starts to smear, lower the shader blur before raising the pre-blur.");
    ImGui::SliderFloat("Settings Blur Radius", &settingsGlass.preBlurRadius, 0.0f, 10.0f);
    ImGui::SliderInt("Settings Blur Iters", &settingsGlass.blurIterations, 0, 20);
    ImGui::SliderFloat("Settings Shader Blur", &settingsGlass.shaderBlurIntensity, 0.0f, 10.0f);
    ImGui::SliderFloat("Settings Refraction", &settingsGlass.refractionIntensity, 0.0f, 1.0f);
    ImGui::SliderFloat("Settings Glow", &settingsGlass.glowIntensity, 0.0f, 1.5f);
    ImGui::SliderFloat("Settings Roughness", &settingsGlass.roughness, 0.0f, 0.10f);
    ImGui::SliderFloat("Settings Power", &settingsGlass.powerFactor, 1.001f, 32.0f);
    ImGui::SliderFloat("Settings Inset", &settingsGlass.inset, 0.0f, 20.0f);
    ImGui::SliderFloat("Settings Shade", &settingsGlass.shade, 0.0f, 1.0f);
    ImGui::SliderFloat("Settings Dark Tint", &settingsGlass.tintAlphaDark, 0.0f, 0.5f);
    ImGui::SliderFloat("Settings Light Tint", &settingsGlass.tintAlphaLight, 0.0f, 0.5f);
    ImGui::SliderFloat("Settings Saturation", &settingsGlass.saturation, 0.0f, 2.0f);

    ImGui::SeparatorText("Gyro Pointer");
    float gyroSensitivity = input.virtualPointerSensitivity();
    if (ImGui::SliderFloat("Gyro Sensitivity", &gyroSensitivity, 200.0f, 10000.0f, "%.0f")) {
        input.setVirtualPointerSensitivity(gyroSensitivity);
    }
    ImGui::Text("Pointer: %s", input.virtualPointerEnabled() ? "On" : "Off");

    ImGui::End();
}

void DebugImGuiOverlay::render(nxui::Renderer& ren, nxui::Input& input, bool& open) {
    if (!m_initialized || !open) {
        m_wantsTouchCapture = false;
        return;
    }

    auto& lg = ren.liquidGlassSettings();
    if (m_autoAnimateTime) {
        lg.time += std::max(m_deltaTime, 1.0f / 240.0f);
    }

    updateInput(ren.gpu(), input);
    ImGui::NewFrame();
    drawWindow(ren, input, open);
    ImGui::Render();

    m_wantsTouchCapture = ImGui::GetIO().WantCaptureMouse;

    ren.flush();
    renderDrawData(ren, ImGui::GetDrawData());
}

void DebugImGuiOverlay::renderDrawData(nxui::Renderer& ren, ImDrawData* drawData) {
    if (!drawData || drawData->CmdListsCount <= 0 || drawData->TotalVtxCount <= 0) {
        return;
    }

    nxui::GpuDevice& gpu = ren.gpu();
    int slot = gpu.slot();
    size_t totalVertexBytes = (size_t)drawData->TotalVtxCount * sizeof(ImDrawVert);
    size_t totalIndexBytes = (size_t)drawData->TotalIdxCount * sizeof(ImDrawIdx);

    if (!ensureDrawBuffers(gpu, slot, totalVertexBytes, totalIndexBytes)) {
        return;
    }

    auto cmd = gpu.cmdBuf();
    dk::ImageView colorTarget{gpu.fbImage(slot)};
    dk::ImageView dsTarget{gpu.dsImage()};
    cmd.bindRenderTargets(&colorTarget, &dsTarget);
    cmd.setViewports(0, DkViewport{0.0f, 0.0f, (float)gpu.width(), (float)gpu.height(), 0.0f, 1.0f});
    cmd.setScissors(0, DkScissor{0, 0, (uint32_t)gpu.width(), (uint32_t)gpu.height()});
    cmd.bindShaders(DkStageFlag_GraphicsMask, {&m_vertexShader, &m_fragmentShader});
    cmd.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
    cmd.bindColorState(dk::ColorState{}.setBlendEnable(0, true));
    DkBlendState blendState = dk::BlendState{}
        .setFactors(DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha,
                    DkBlendFactor_One, DkBlendFactor_InvSrcAlpha);
    cmd.bindBlendStates(0, blendState);
    cmd.bindDepthStencilState(dk::DepthStencilState{}.setDepthTestEnable(false));

    VertUbo ubo {};
    ortho(ubo.projection, drawData->DisplaySize.x, drawData->DisplaySize.y);
    std::memcpy(m_uboMemBlock.getCpuAddr(), &ubo, sizeof(ubo));
    size_t uboSize = alignUp(sizeof(ubo), DK_UNIFORM_BUF_ALIGNMENT);
    cmd.bindUniformBuffer(DkStage_Vertex, 0, m_uboMemBlock.getGpuAddr(), uboSize);
    cmd.pushConstants(m_uboMemBlock.getGpuAddr(), uboSize, 0, sizeof(ubo), &ubo);

    cmd.bindVtxAttribState({
        DkVtxAttribState{0, 0, offsetof(ImDrawVert, pos), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(ImDrawVert, uv),  DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(ImDrawVert, col), DkVtxAttribSize_4x8,  DkVtxAttribType_Unorm, 0},
    });
    cmd.bindVtxBufferState(DkVtxBufferState{sizeof(ImDrawVert), 0});
    cmd.bindVtxBuffer(0, m_vertexMemBlock[slot].getGpuAddr(), m_vertexCapacity[slot]);

    static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t), "ImGui draw index type must stay 16-bit");
    cmd.bindIdxBuffer(DkIdxFormat_Uint16, m_indexMemBlock[slot].getGpuAddr());

    cmd.bindImageDescriptorSet(gpu.imgDescGpuAddr(), nxui::GpuDevice::MAX_TEXTURES);
    cmd.bindSamplerDescriptorSet(gpu.samDescGpuAddr(), nxui::GpuDevice::MAX_SAMPLERS);
    cmd.barrier(DkBarrier_None, DkInvalidateFlags_Descriptors);

    size_t vertexOffsetBytes = 0;
    size_t indexOffsetBytes = 0;
    auto* vertexCpu = static_cast<uint8_t*>(m_vertexMemBlock[slot].getCpuAddr());
    auto* indexCpu = static_cast<uint8_t*>(m_indexMemBlock[slot].getCpuAddr());

    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList& cmdList = *drawData->CmdLists[listIndex];
        size_t vertexBytes = (size_t)cmdList.VtxBuffer.Size * sizeof(ImDrawVert);
        size_t indexBytes = (size_t)cmdList.IdxBuffer.Size * sizeof(ImDrawIdx);

        std::memcpy(vertexCpu + vertexOffsetBytes, cmdList.VtxBuffer.Data, vertexBytes);
        std::memcpy(indexCpu + indexOffsetBytes, cmdList.IdxBuffer.Data, indexBytes);

        for (const ImDrawCmd& drawCmd : cmdList.CmdBuffer) {
            if (drawCmd.UserCallback == ImDrawCallback_ResetRenderState) {
                cmd.bindShaders(DkStageFlag_GraphicsMask, {&m_vertexShader, &m_fragmentShader});
                continue;
            }
            if (drawCmd.UserCallback) {
                drawCmd.UserCallback(&cmdList, &drawCmd);
                continue;
            }

            ImVec4 clip = drawCmd.ClipRect;
            float clipX = std::max(0.0f, clip.x - drawData->DisplayPos.x);
            float clipY = std::max(0.0f, clip.y - drawData->DisplayPos.y);
            float clipZ = std::min(drawData->DisplaySize.x, clip.z - drawData->DisplayPos.x);
            float clipW = std::min(drawData->DisplaySize.y, clip.w - drawData->DisplayPos.y);
            if (clipZ <= clipX || clipW <= clipY) {
                continue;
            }

            int texSlot = (int)(intptr_t)drawCmd.GetTexID();
            cmd.setScissors(0, DkScissor{(uint32_t)clipX, (uint32_t)clipY,
                                         (uint32_t)(clipZ - clipX), (uint32_t)(clipW - clipY)});
            cmd.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(texSlot, 0));
            cmd.drawIndexed(DkPrimitive_Triangles,
                            drawCmd.ElemCount,
                            1,
                            drawCmd.IdxOffset + indexOffsetBytes / sizeof(ImDrawIdx),
                            drawCmd.VtxOffset + vertexOffsetBytes / sizeof(ImDrawVert),
                            0);
        }

        vertexOffsetBytes += vertexBytes;
        indexOffsetBytes += indexBytes;
    }
}

#endif