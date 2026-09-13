#include <nxui/core/GpuDevice.hpp>
#include <SDL2/SDL.h>
#include <cstdio>

namespace nxui {

bool GpuDevice::initialize() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::fprintf(stderr, "[GpuDevice-SDL2] SDL_Init(VIDEO) failed: %s\n", SDL_GetError());
        return false;
    }

    m_sdlWindow = SDL_CreateWindow("SwitchU",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        FB_WIDTH, FB_HEIGHT, SDL_WINDOW_FULLSCREEN);
    if (!m_sdlWindow) {
        std::fprintf(stderr, "[GpuDevice-SDL2] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_sdlRenderer) {
        std::fprintf(stderr, "[GpuDevice-SDL2] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    std::printf("[GpuDevice-SDL2] Initialized %dx%d\n", FB_WIDTH, FB_HEIGHT);
    return true;
}

void GpuDevice::shutdown() {
    if (m_sdlRenderer) { SDL_DestroyRenderer(m_sdlRenderer); m_sdlRenderer = nullptr; }
    if (m_sdlWindow)   { SDL_DestroyWindow(m_sdlWindow);     m_sdlWindow = nullptr; }
}

int GpuDevice::beginFrame() {
    m_slot = (m_slot + 1) % NUM_FB;
    return m_slot;
}

void GpuDevice::endFrame() {
    SDL_RenderPresent(m_sdlRenderer);
}

void GpuDevice::waitIdle() {
    // No-op for SDL2 — rendering is synchronous
}

bool GpuDevice::downloadFramebufferRgba(std::vector<uint8_t>& outRgba,
                                        int& outW, int& outH,
                                        bool halfRes) {
    (void)outRgba;
    (void)outW;
    (void)outH;
    (void)halfRes;
    return false;
}

} // namespace nxui
