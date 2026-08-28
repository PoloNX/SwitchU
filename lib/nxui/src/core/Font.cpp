#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>

namespace nxui {

Font::~Font() {
    if (m_font) TTF_CloseFont(m_font);
}

bool Font::load(GpuDevice& gpu, Renderer& ren,
                const std::string& path, int ptSize)
{
    TTF_Font* newFont = TTF_OpenFont(path.c_str(), ptSize);
    if (!newFont) {
        std::fprintf(stderr, "[Font] TTF_OpenFont failed: %s\n", TTF_GetError());
        return false;
    }

    if (m_font)
        TTF_CloseFont(m_font);

    m_font = newFont;
    m_gpu = &gpu;
    m_ren = &ren;
    m_ptSize = ptSize;
    ++m_revision;
    clearCache();
    return true;
}

Vec2 Font::measure(const std::string& text) const {
    if (!m_font || text.empty()) return {0, 0};
    // Rendered strings already carry their exact dimensions. Avoid invoking
    // SDL_ttf again for every label on every frame.
    const auto cached = m_lruMap.find(text);
    if (cached != m_lruMap.end())
        return {static_cast<float>(cached->second->w),
                static_cast<float>(cached->second->h)};
    int w = 0, h = 0;
    TTF_SizeUTF8(m_font, text.c_str(), &w, &h);
    return {(float)w, (float)h};
}

void Font::clearCache() {
    m_lruList.clear();
    m_lruMap.clear();
    m_cacheBytes = 0;
    m_maintenanceRequested = false;
}

void Font::trimCache(std::size_t maxEntries, std::size_t maxBytes) {
    while (!m_lruList.empty() &&
           (m_lruList.size() > maxEntries || m_cacheBytes > maxBytes)) {
        auto victim = std::prev(m_lruList.end());
        const std::size_t bytes = victim->tex.allocationSize();
        m_lruMap.erase(victim->key);
        m_lruList.erase(victim);
        m_cacheBytes = bytes <= m_cacheBytes ? m_cacheBytes - bytes : 0;
    }
    m_maintenanceRequested = false;
}

Texture* Font::getOrRender(GpuDevice& gpu, Renderer& ren, const std::string& text) {
    // Cache hit: promote to the front.
    auto it = m_lruMap.find(text);
    if (it != m_lruMap.end()) {
        // Move to front (most-recently-used)
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
        return &it->second->tex;
    }

    // Render the string via SDL_ttf.
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(m_font, text.c_str(), white);
    if (!surface) return nullptr;

    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surface);
    if (!rgba) return nullptr;

    SDL_LockSurface(rgba);
    const uint8_t* pixels = static_cast<const uint8_t*>(rgba->pixels);
    int w = rgba->w, h = rgba->h, pitch = rgba->pitch;

    constexpr std::size_t kTextureAllocationAlignment = 4096u;
    const std::size_t estimatedBytes =
        (static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u +
         kTextureAllocationAlignment - 1u) &
        ~(kTextureAllocationAlignment - 1u);
    if (m_lruList.size() >= kMaxCacheEntries ||
        m_cacheBytes + estimatedBytes > kMaxCacheBytes ||
        gpu.imageMemoryAvailable() < estimatedBytes + kGpuHeadroom) {
        m_maintenanceRequested = true;
        SDL_UnlockSurface(rgba);
        SDL_FreeSurface(rgba);
        return nullptr;
    }

    m_lruList.emplace_front();
    auto& entry = m_lruList.front();
    entry.key = text;
    entry.w = w;
    entry.h = h;
    if (!entry.tex.loadFromSurface(gpu, ren, pixels, w, h, pitch)) {
        m_lruList.pop_front();
        m_maintenanceRequested = true;
        SDL_UnlockSurface(rgba);
        SDL_FreeSurface(rgba);
        return nullptr;
    }
    m_cacheBytes += entry.tex.allocationSize();

    SDL_UnlockSurface(rgba);
    SDL_FreeSurface(rgba);

    m_lruMap[text] = m_lruList.begin();
    return &m_lruList.front().tex;
}

void Font::draw(Renderer& ren, const std::string& text,
                const Vec2& pos, const Color& color, float scale) {
    if (!m_font || !m_gpu || text.empty()) return;

    Texture* tex = getOrRender(*m_gpu, ren, text);
    if (!tex || !tex->valid()) return;

    float w = tex->width()  * scale;
    float h = tex->height() * scale;
    ren.drawTexture(tex, {pos.x, pos.y, w, h}, color);
}

} // namespace nxui
