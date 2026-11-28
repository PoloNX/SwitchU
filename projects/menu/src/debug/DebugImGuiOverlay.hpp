#pragma once

#ifdef SWITCHU_DEBUG_UI

#include <nxui/core/Renderer.hpp>
#include <nxui/core/Input.hpp>
#include <imgui.h>
#include <deko3d.hpp>
#include <array>

class DebugImGuiOverlay {
public:
    ~DebugImGuiOverlay();

    bool initialize(nxui::GpuDevice& gpu, nxui::Renderer& ren);
    void shutdown(nxui::GpuDevice& gpu);

    void setDeltaTime(float dt) { m_deltaTime = dt; }
    void setFrameHealth(float avgMs, float lastMs, int hitchInWindow,
                        bool soft, bool stressed) {
        m_frameAvgMs = avgMs;
        m_frameLastMs = lastMs;
        m_frameHitchInWindow = hitchInWindow;
        m_frameSoft = soft;
        m_frameStressed = stressed;
    }
    bool wantsTouchCapture() const { return m_wantsTouchCapture; }

    void render(nxui::Renderer& ren, nxui::Input& input, bool& open);

private:
    enum class Panel {
        Logs,
        BlurSettings,
    };

    struct VertUbo {
        float projection[16];
    };

    bool loadShaders(nxui::GpuDevice& gpu);
    bool createFontTexture(nxui::GpuDevice& gpu, nxui::Renderer& ren);
    bool ensureDrawBuffers(nxui::GpuDevice& gpu, int slot, size_t vertexBytes, size_t indexBytes);
    void updateInput(nxui::GpuDevice& gpu, const nxui::Input& input);
    void drawWindow(nxui::Renderer& ren, bool& open);
    void renderDrawData(nxui::Renderer& ren, ImDrawData* drawData);

    bool m_initialized = false;
    bool m_autoAnimateTime = true;
    bool m_wantsTouchCapture = false;
    bool m_wasOpen = false;
    float m_deltaTime = 1.0f / 60.0f;
    float m_frameAvgMs = 0.f;
    float m_frameLastMs = 0.f;
    int m_frameHitchInWindow = 0;
    bool m_frameSoft = false;
    bool m_frameStressed = false;
    Panel m_activePanel = Panel::Logs;

    dk::Shader m_vertexShader;
    dk::Shader m_fragmentShader;

    dk::UniqueMemBlock m_fontMemBlock;
    dk::Image m_fontImage;
    int m_fontTextureSlot = -1;

    dk::UniqueMemBlock m_uboMemBlock;
    std::array<dk::UniqueMemBlock, nxui::GpuDevice::NUM_FB> m_vertexMemBlock;
    std::array<dk::UniqueMemBlock, nxui::GpuDevice::NUM_FB> m_indexMemBlock;
    std::array<size_t, nxui::GpuDevice::NUM_FB> m_vertexCapacity {};
    std::array<size_t, nxui::GpuDevice::NUM_FB> m_indexCapacity {};
};

#endif