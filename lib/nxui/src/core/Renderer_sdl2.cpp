#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Font.hpp>
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace nxui {

Renderer::Renderer(GpuDevice& gpu) : m_gpu(gpu) {
    m_clipStack.reserve(8);
    m_vtxBuf.resize(GpuDevice::MAX_VERTICES);
    m_vtxBase = m_vtxBuf.data();
}

Renderer::~Renderer() {}

bool Renderer::initialize() {
    std::printf("[Renderer-SDL2] Init complete (no shaders)\n");
    return true;
}

void Renderer::beginFrame() {
    m_vtxCount = 0;
    m_vtxBatchStart = 0;
    m_curTexSlot = -1;
    m_texturing  = false;
    m_boundTex = nullptr;
    m_clipStack.clear();

    auto* r = m_gpu.sdlRenderer();
    SDL_SetRenderDrawColor(r, 13, 20, 38, 255); // 0.05, 0.08, 0.15
    SDL_RenderClear(r);
    SDL_RenderSetClipRect(r, nullptr);
}

void Renderer::endFrame() {
    flush();
}

void Renderer::flush() {
    uint32_t batchVerts = m_vtxCount - m_vtxBatchStart;
    if (batchVerts == 0) return;

    auto* sdlRen = m_gpu.sdlRenderer();

    // Build SDL_Vertex array from our Vertex2D batch
    // Process triangles (3 vertices at a time)
    for (uint32_t i = m_vtxBatchStart; i + 2 < m_vtxCount; i += 3) {
        SDL_Vertex verts[3];
        for (int j = 0; j < 3; ++j) {
            auto& v = m_vtxBase[i + j];
            verts[j].position  = {v.x, v.y};
            verts[j].color     = {
                (uint8_t)(v.r * 255.f),
                (uint8_t)(v.g * 255.f),
                (uint8_t)(v.b * 255.f),
                (uint8_t)(v.a * 255.f)
            };
            verts[j].tex_coord = {v.u, v.v};
        }

        SDL_Texture* tex = m_texturing ? m_boundTex : nullptr;
        SDL_RenderGeometry(sdlRen, tex, verts, 3, nullptr, 0);
    }

    m_vtxBatchStart = m_vtxCount;
}

void Renderer::bindTexture(int slot) {
    (void)slot;
    // For SDL2 we bind the actual SDL_Texture* during drawTexture calls.
    if (slot < 0) {
        if (m_texturing) flush();
        m_texturing = false;
        m_boundTex = nullptr;
        m_curTexSlot = -1;
    }
}

void Renderer::resetTextureSlots() {
    m_nextDescSlot = 0;
    m_curTexSlot = -1;
    m_boundTex = nullptr;
}

void Renderer::useShader(ShaderProgram prog) {
    // No-op for SDL2 (no shader support).
    (void)prog;
}

void Renderer::pushFsUniforms(const FsUniforms& fs) {
    // No-op for SDL2.
    (void)fs;
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

    SDL_Rect sr = {
        (int)std::max(0.f, clip.x), (int)std::max(0.f, clip.y),
        (int)std::max(0.f, clip.width), (int)std::max(0.f, clip.height)
    };
    SDL_RenderSetClipRect(m_gpu.sdlRenderer(), &sr);
}

void Renderer::popClipRect() {
    flush();
    if (!m_clipStack.empty()) m_clipStack.pop_back();
    if (m_clipStack.empty()) {
        SDL_RenderSetClipRect(m_gpu.sdlRenderer(), nullptr);
    } else {
        auto& clip = m_clipStack.back();
        SDL_Rect sr = {
            (int)std::max(0.f, clip.x), (int)std::max(0.f, clip.y),
            (int)std::max(0.f, clip.width), (int)std::max(0.f, clip.height)
        };
        SDL_RenderSetClipRect(m_gpu.sdlRenderer(), &sr);
    }
}

void Renderer::addVertex(float x, float y, float u, float v, const Color& c) {
    if (m_vtxCount >= GpuDevice::MAX_VERTICES) {
        flush();
        if (m_vtxCount >= GpuDevice::MAX_VERTICES) {
            std::printf("[Renderer-SDL2] WARN: vertex buffer full\n");
            return;
        }
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
    m_texturing = false;
    m_boundTex = nullptr;
    addQuad(r.x, r.y, r.right(), r.bottom(), 0, 0, 1, 1, c);
}

void Renderer::drawRectOutline(const Rect& r, const Color& c, float t) {
    drawRect({r.x, r.y, r.width, t}, c);
    drawRect({r.x, r.bottom() - t, r.width, t}, c);
    drawRect({r.x, r.y + t, t, r.height - 2*t}, c);
    drawRect({r.right()-t, r.y + t, t, r.height - 2*t}, c);
}

void Renderer::drawGradientRect(const Rect& r, const Color& top, const Color& bottom) {
    m_texturing = false;
    m_boundTex = nullptr;
    addQuadGrad(r.x, r.y, r.right(), r.bottom(), 0, 0, 1, 1, top, bottom);
}

void Renderer::drawRoundedRect(const Rect& r, const Color& c, float radius) {
    if (radius <= 0.f) { drawRect(r, c); return; }
    float rad = std::min(radius, std::min(r.width, r.height) * 0.5f);

    flush();
    m_texturing = false;
    m_boundTex = nullptr;

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

    flush();
    m_texturing = false;
    m_boundTex = nullptr;

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
    flush();
    m_texturing = false;
    m_boundTex = nullptr;
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
    flush();
    m_texturing = false;
    m_boundTex = nullptr;
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

    flush();
    m_texturing = false;
    m_boundTex = nullptr;

    addVertex(a.x, a.y, 0, 0, c);
    addVertex(b.x, b.y, 0, 0, c);
    addVertex(cc.x, cc.y, 0, 0, c);
    addVertex(b.x, b.y, 0, 0, c);
    addVertex(dd.x, dd.y, 0, 0, c);
    addVertex(cc.x, cc.y, 0, 0, c);
}

void Renderer::drawTexture(const Texture* tex, const Rect& dest, const Color& tint) {
    if (!tex || !tex->valid()) return;

    flush();

    // For SDL2, use SDL_RenderCopy directly instead of geometry batching.
    SDL_Texture* sdlTex = tex->sdlTexture();
    if (!sdlTex) return;

    SDL_SetTextureColorMod(sdlTex,
        (uint8_t)(tint.r * 255.f),
        (uint8_t)(tint.g * 255.f),
        (uint8_t)(tint.b * 255.f));
    SDL_SetTextureAlphaMod(sdlTex, (uint8_t)(tint.a * 255.f));

    SDL_Rect dr = {(int)dest.x, (int)dest.y, (int)dest.width, (int)dest.height};
    SDL_RenderCopy(m_gpu.sdlRenderer(), sdlTex, nullptr, &dr);
}

void Renderer::drawTextureSub(const Texture* tex, const Rect& src, const Rect& dest, const Color& tint) {
    if (!tex || !tex->valid()) return;

    flush();

    SDL_Texture* sdlTex = tex->sdlTexture();
    if (!sdlTex) return;

    SDL_SetTextureColorMod(sdlTex,
        (uint8_t)(tint.r * 255.f),
        (uint8_t)(tint.g * 255.f),
        (uint8_t)(tint.b * 255.f));
    SDL_SetTextureAlphaMod(sdlTex, (uint8_t)(tint.a * 255.f));

    SDL_Rect sr = {(int)src.x, (int)src.y, (int)src.width, (int)src.height};
    SDL_Rect dr = {(int)dest.x, (int)dest.y, (int)dest.width, (int)dest.height};
    SDL_RenderCopy(m_gpu.sdlRenderer(), sdlTex, &sr, &dr);
}

void Renderer::drawTextureRounded(const Texture* tex, const Rect& dest, float radius, const Color& tint) {
    // SDL2 cannot do rounded texture clipping easily; fall back to a regular textured rect,
    // unless a rounded geometry path is explicitly used below.
    if (!tex) return;
    if (radius <= 0.f) { drawTexture(tex, dest, tint); return; }

    flush();

    SDL_Texture* sdlTex = tex->sdlTexture();
    if (!sdlTex) return;

    SDL_SetTextureColorMod(sdlTex,
        (uint8_t)(tint.r * 255.f),
        (uint8_t)(tint.g * 255.f),
        (uint8_t)(tint.b * 255.f));
    SDL_SetTextureAlphaMod(sdlTex, (uint8_t)(tint.a * 255.f));

    float rad = std::min(radius, std::min(dest.width, dest.height) * 0.5f);
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
    SDL_Color sc = {
        (uint8_t)(tint.r * 255.f),
        (uint8_t)(tint.g * 255.f),
        (uint8_t)(tint.b * 255.f),
        (uint8_t)(tint.a * 255.f)
    };

    for (int i = 0; i < ptCount; ++i) {
        auto& p0 = pts[i];
        auto& p1 = pts[(i + 1) % ptCount];
        Vec2 uv0 = toUV(p0.x, p0.y);
        Vec2 uv1 = toUV(p1.x, p1.y);

        SDL_Vertex verts[3] = {
            {{fcx,  fcy},  sc, {cuv.x, cuv.y}},
            {{p0.x, p0.y}, sc, {uv0.x, uv0.y}},
            {{p1.x, p1.y}, sc, {uv1.x, uv1.y}},
        };
        SDL_RenderGeometry(m_gpu.sdlRenderer(), sdlTex, verts, 3, nullptr, 0);
    }
}

void Renderer::drawText(const std::string& text, const Vec2& pos, Font* font,
                         const Color& color, float scale) {
    if (!font || text.empty()) return;
    font->draw(*this, text, pos, color, scale);
}

void Renderer::captureToOffscreen(bool reuseIfValid) {
    (void)reuseIfValid;
    // No-op: SDL2 backend has no offscreen render targets.
}

void Renderer::copyOffscreen(int srcTarget, int dstTarget) {
    (void)srcTarget;
    (void)dstTarget;
    // No-op.
}

void Renderer::drawOffscreen(int target, const Rect& dest, const Color& tint) {
    (void)target; (void)dest; (void)tint;
    // No-op.
}

void Renderer::drawOffscreenRounded(int target, const Rect& dest, float radius, const Color& tint) {
    (void)target;
    drawRoundedRect(dest, tint, radius);
}

void Renderer::drawLiquidGlass(int target, const Rect& panelRect, float radius,
                               const Color& tint, float opacity, float shade) {
    (void)target;
    (void)shade;
    drawRoundedRect(panelRect, tint.withAlpha(tint.a * opacity), radius);
}

void Renderer::applyBlur(float radius, int passes) {
    (void)radius; (void)passes;
    // No-op: blur not available in SDL2 backend.
}

void Renderer::applyWave(float time, float amplitude, float frequency) {
    (void)time; (void)amplitude; (void)frequency;
    // No-op: wave not available in SDL2 backend.
}

} // namespace nxui
