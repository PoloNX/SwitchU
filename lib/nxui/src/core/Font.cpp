#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nxui {

namespace {

bool nextUtf8(const char*& p, const char* end, std::uint32_t& cp) {
    if (p >= end)
        return false;
    const unsigned char c0 = static_cast<unsigned char>(*p++);
    if (c0 < 0x80) {
        cp = c0;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0 && p < end) {
        const unsigned char c1 = static_cast<unsigned char>(*p++);
        cp = (std::uint32_t(c0 & 0x1F) << 6) | std::uint32_t(c1 & 0x3F);
        return true;
    }
    if ((c0 & 0xF0) == 0xE0 && p + 1 < end) {
        const unsigned char c1 = static_cast<unsigned char>(*p++);
        const unsigned char c2 = static_cast<unsigned char>(*p++);
        cp = (std::uint32_t(c0 & 0x0F) << 12) | (std::uint32_t(c1 & 0x3F) << 6)
            | std::uint32_t(c2 & 0x3F);
        return true;
    }
    if ((c0 & 0xF8) == 0xF0 && p + 2 < end) {
        const unsigned char c1 = static_cast<unsigned char>(*p++);
        const unsigned char c2 = static_cast<unsigned char>(*p++);
        const unsigned char c3 = static_cast<unsigned char>(*p++);
        cp = (std::uint32_t(c0 & 0x07) << 18) | (std::uint32_t(c1 & 0x3F) << 12)
            | (std::uint32_t(c2 & 0x3F) << 6) | std::uint32_t(c3 & 0x3F);
        return true;
    }
    cp = 0xFFFD;
    return true;
}

TTF_Font* openMemoryFace(const void* data, std::size_t size, int ptSize) {
    if (!data || size == 0 || ptSize <= 0)
        return nullptr;
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    if (!rw)
        return nullptr;
    // freesrc=1 frees the RWops wrapper only; const sharedmem stays mapped.
    TTF_Font* font = TTF_OpenFontRW(rw, 1, ptSize);
    if (!font)
        std::fprintf(stderr, "[Font] TTF_OpenFontRW failed: %s\n", TTF_GetError());
    return font;
}

} // namespace

Font::~Font() {
    clearFallbacks();
    closePrimary();
}

void Font::closePrimary() {
    if (m_font) {
        TTF_CloseFont(m_font);
        m_font = nullptr;
    }
}

void Font::clearFallbacks() {
    for (TTF_Font* face : m_fallbacks)
        TTF_CloseFont(face);
    m_fallbacks.clear();
}

bool Font::load(GpuDevice& gpu, Renderer& ren,
                const std::string& path, int ptSize)
{
    TTF_Font* newFont = TTF_OpenFont(path.c_str(), ptSize);
    if (!newFont) {
        std::fprintf(stderr, "[Font] TTF_OpenFont failed: %s\n", TTF_GetError());
        return false;
    }

    clearFallbacks();
    closePrimary();
    m_font = newFont;
    m_gpu = &gpu;
    m_ren = &ren;
    m_ptSize = ptSize;
    ++m_revision;
    clearCache();
    return true;
}

bool Font::loadFromMemory(GpuDevice& gpu, Renderer& ren,
                          const void* data, std::size_t size, int ptSize)
{
    TTF_Font* newFont = openMemoryFace(data, size, ptSize);
    if (!newFont)
        return false;

    clearFallbacks();
    closePrimary();
    m_font = newFont;
    m_gpu = &gpu;
    m_ren = &ren;
    m_ptSize = ptSize;
    ++m_revision;
    clearCache();
    return true;
}

bool Font::addFallbackFromMemory(const void* data, std::size_t size) {
    if (!m_font || m_ptSize <= 0)
        return false;
    TTF_Font* face = openMemoryFace(data, size, m_ptSize);
    if (!face)
        return false;
    m_fallbacks.push_back(face);
    ++m_revision;
    clearCache();
    return true;
}

TTF_Font* Font::fontForCodepoint(std::uint32_t cp) const {
    if (m_font && TTF_GlyphIsProvided32(m_font, cp))
        return m_font;
    for (TTF_Font* face : m_fallbacks) {
        if (face && TTF_GlyphIsProvided32(face, cp))
            return face;
    }
    return m_font;
}

bool Font::primaryCovers(const std::string& text) const {
    if (!m_font)
        return false;
    if (m_fallbacks.empty())
        return true;
    const char* p = text.data();
    const char* end = p + text.size();
    std::uint32_t cp = 0;
    while (nextUtf8(p, end, cp)) {
        if (cp == 0)
            continue;
        if (!TTF_GlyphIsProvided32(m_font, cp))
            return false;
    }
    return true;
}

SDL_Surface* Font::renderTextSurface(const std::string& text) const {
    if (!m_font || text.empty())
        return nullptr;

    SDL_Color white = {255, 255, 255, 255};
    if (primaryCovers(text))
        return TTF_RenderUTF8_Blended(m_font, text.c_str(), white);

    // Mixed-script path: render each glyph with the first face that provides it.
    struct GlyphBlit {
        SDL_Surface* surface = nullptr;
        int advance = 0;
        int minx = 0;
        int maxy = 0;
    };
    std::vector<GlyphBlit> glyphs;
    glyphs.reserve(text.size());

    int totalAdvance = 0;
    int maxAscent = 0;
    int maxDescent = 0;
    const char* p = text.data();
    const char* end = p + text.size();
    std::uint32_t cp = 0;
    while (nextUtf8(p, end, cp)) {
        if (cp == 0)
            continue;
        TTF_Font* face = fontForCodepoint(cp);
        if (!face)
            face = m_font;
        int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
        if (TTF_GlyphMetrics32(face, cp, &minx, &maxx, &miny, &maxy, &advance) != 0)
            advance = TTF_FontHeight(face) / 2;
        SDL_Surface* g = TTF_RenderGlyph32_Blended(face, cp, white);
        GlyphBlit blit{};
        blit.surface = g;
        blit.advance = advance > 0 ? advance : (g ? g->w : 0);
        blit.minx = minx;
        blit.maxy = maxy;
        glyphs.push_back(blit);
        totalAdvance += blit.advance;
        maxAscent = std::max(maxAscent, TTF_FontAscent(face));
        maxDescent = std::max(maxDescent, -TTF_FontDescent(face));
    }

    if (glyphs.empty())
        return nullptr;

    const int width = std::max(1, totalAdvance + 2);
    const int height = std::max(1, maxAscent + maxDescent + 2);
    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                      SDL_PIXELFORMAT_ABGR8888);
    if (!out) {
        for (auto& g : glyphs)
            if (g.surface) SDL_FreeSurface(g.surface);
        return nullptr;
    }
    SDL_SetSurfaceBlendMode(out, SDL_BLENDMODE_BLEND);

    int penX = 0;
    const int baseline = maxAscent;
    for (auto& g : glyphs) {
        if (g.surface) {
            SDL_Rect dst{
                penX + g.minx,
                baseline - g.maxy,
                g.surface->w,
                g.surface->h};
            SDL_BlitSurface(g.surface, nullptr, out, &dst);
            SDL_FreeSurface(g.surface);
            g.surface = nullptr;
        }
        penX += g.advance;
    }
    return out;
}

Vec2 Font::measure(const std::string& text) const {
    if (!m_font || text.empty()) return {0, 0};
    const auto cached = m_lruMap.find(text);
    if (cached != m_lruMap.end())
        return {static_cast<float>(cached->second->w),
                static_cast<float>(cached->second->h)};

    if (primaryCovers(text)) {
        int w = 0, h = 0;
        TTF_SizeUTF8(m_font, text.c_str(), &w, &h);
        return {(float)w, (float)h};
    }

    int totalAdvance = 0;
    int maxAscent = 0;
    int maxDescent = 0;
    const char* p = text.data();
    const char* end = p + text.size();
    std::uint32_t cp = 0;
    while (nextUtf8(p, end, cp)) {
        if (cp == 0)
            continue;
        TTF_Font* face = fontForCodepoint(cp);
        if (!face)
            face = m_font;
        int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
        if (TTF_GlyphMetrics32(face, cp, &minx, &maxx, &miny, &maxy, &advance) != 0)
            advance = TTF_FontHeight(face) / 2;
        totalAdvance += advance > 0 ? advance : 0;
        maxAscent = std::max(maxAscent, TTF_FontAscent(face));
        maxDescent = std::max(maxDescent, -TTF_FontDescent(face));
    }
    return {static_cast<float>(std::max(1, totalAdvance)),
            static_cast<float>(std::max(1, maxAscent + maxDescent))};
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
    auto it = m_lruMap.find(text);
    if (it != m_lruMap.end()) {
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
        return &it->second->tex;
    }

    SDL_Surface* surface = renderTextSurface(text);
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
