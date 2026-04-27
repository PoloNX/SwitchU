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
    m_gpu = &gpu;
    m_ren = &ren;
    m_ptSize = ptSize;

    m_font = TTF_OpenFont(path.c_str(), ptSize);
    if (!m_font) {
        std::fprintf(stderr, "[Font] TTF_OpenFont failed: %s\n", TTF_GetError());
        return false;
    }
    return true;
}

Vec2 Font::measure(const std::string& text) const {
    if (!m_font || text.empty()) return {0, 0};
    int w = 0, h = 0;
    TTF_SizeUTF8(m_font, text.c_str(), &w, &h);
    return {(float)w, (float)h};
}

void Font::clearCache() {
    m_lruList.clear();
    m_lruMap.clear();
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

    // Evict the LRU entry if needed.
    if (m_lruList.size() >= kMaxCacheEntries) {
        // Recycle the least-recently-used entry.
        // Its Texture already has a descriptor slot and MemBlock;
        // loadFromSurface → loadFromPixels will reuse them.
        auto& victim = m_lruList.back();
        m_lruMap.erase(victim.key);

        victim.key = text;
        victim.w = w;
        victim.h = h;
        victim.tex.loadFromSurface(gpu, ren, pixels, w, h, pitch);

        // Move recycled entry to front
        m_lruList.splice(m_lruList.begin(), m_lruList, std::prev(m_lruList.end()));
    } else {
        // Fresh entry
        m_lruList.emplace_front();
        auto& entry = m_lruList.front();
        entry.key = text;
        entry.w = w;
        entry.h = h;
        entry.tex.loadFromSurface(gpu, ren, pixels, w, h, pitch);
    }

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
