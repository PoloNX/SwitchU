#pragma once
#include <nxui/core/Types.hpp>
#include "Texture.hpp"
#include <SDL2/SDL_ttf.h>
#include <string>
#include <unordered_map>
#include <list>

namespace nxui {

class Renderer;
class GpuDevice;

class Font {
public:
    Font() = default;
    ~Font();

    bool load(GpuDevice& gpu, Renderer& ren,
              const std::string& path, int ptSize);

    // Render a string to a texture (cached) and draw it
    void draw(Renderer& ren, const std::string& text,
              const Vec2& pos, const Color& color, float scale = 1.f);

    // Measure text dimensions
    Vec2 measure(const std::string& text) const;

    // Clear all cached glyph textures (call before GPU descriptor reset)
    void clearCache();

    int ptSize() const { return m_ptSize; }

private:
    // Render full string to texture (cache by string)
    Texture* getOrRender(GpuDevice& gpu, Renderer& ren, const std::string& text);

    TTF_Font* m_font = nullptr;
    int       m_ptSize = 0;
    GpuDevice* m_gpu = nullptr;
    Renderer*  m_ren = nullptr;

    // LRU string-texture cache
    // The list stores entries in LRU order (most-recently-used at front).
    // The map provides O(1) lookup by string → list iterator.
    // On eviction, the Texture of the least-recently-used entry is reused
    // (its descriptor slot and MemBlock are recycled via loadFromPixels).
    static constexpr size_t kMaxCacheEntries = 384;

    struct CacheEntry {
        std::string key;
        Texture     tex;
        int         w = 0, h = 0;
    };
    using LruList = std::list<CacheEntry>;
    using LruMap  = std::unordered_map<std::string, LruList::iterator>;

    LruList m_lruList;
    LruMap  m_lruMap;
};

} // namespace nxui
