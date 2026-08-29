#pragma once
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <vector>
#include <string>
#include <cstddef>

class SidebarAnimation {
public:
    bool load(nxui::GpuDevice& gpu, nxui::Renderer& ren,
              const std::string& webpPath,
              int maximumSide = 0,
              std::size_t maximumGpuBytes = 0);

    void update(float dt, bool focused);

    nxui::Texture* currentFrame();

    void reset();

    // Drop the decoded/uploaded frames. reset() only rewinds playback and is
    // intentionally kept separate because sidebar animations use it when they
    // merely lose focus.
    void clear();

    // GPU upload half of the asynchronous widget-animation pipeline. Pixel
    // decoding happens on a worker; at most a few finished frames are appended
    // from the render thread on each update.
    bool appendFrame(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                     const std::uint8_t* rgba, int width, int height,
                     int durationMs);

    bool hasFrames() const { return !m_frames.empty(); }

    int  frameCount() const { return static_cast<int>(m_frames.size()); }

private:
    std::vector<nxui::Texture> m_frames;
    std::vector<int>           m_durationsMs;
    int                        m_frameIndex  = 0;
    float                      m_elapsedMs   = 0.f;
};
