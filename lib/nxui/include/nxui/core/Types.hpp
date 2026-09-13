#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace nxui {

using VoidCallback = std::function<void()>;

struct Vec2 {
    float x = 0.f, y = 0.f;
    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

    float length()   const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
    Vec2  normalized() const {
        float l = length();
        return l > 1e-4f ? *this / l : Vec2{};
    }
    static Vec2 lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
};

struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
    Rect() = default;
    Rect(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}

    float right()  const { return x + width; }
    float bottom() const { return y + height; }
    Vec2  center() const { return {x + width * .5f, y + height * .5f}; }
    Vec2  position() const { return {x, y}; }
    Vec2  size() const { return {width, height}; }

    bool contains(float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    bool contains(const Vec2& p) const { return contains(p.x, p.y); }
    bool intersects(const Rect& o) const {
        return !(x >= o.right() || right() <= o.x || y >= o.bottom() || bottom() <= o.y);
    }
    Rect expanded(float m) const { return {x - m, y - m, width + 2*m, height + 2*m}; }
    Rect shrunk(float m) const { return expanded(-m); }

    static Rect lerp(const Rect& a, const Rect& b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                a.width  + (b.width  - a.width)  * t,
                a.height + (b.height - a.height) * t};
    }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    Color() = default;
    Color(float r, float g, float b, float a = 1.f) : r(r), g(g), b(b), a(a) {}

    explicit Color(uint32_t hex) {
        r = ((hex >> 24) & 0xFF) / 255.f;
        g = ((hex >> 16) & 0xFF) / 255.f;
        b = ((hex >>  8) & 0xFF) / 255.f;
        a = ( hex        & 0xFF) / 255.f;
    }

    static Color fromHSL(float h, float s, float l, float a = 1.f) {
        auto hue2rgb = [](float p, float q, float t) {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < 1.f/6) return p + (q - p) * 6 * t;
            if (t < .5f)   return q;
            if (t < 2.f/3) return p + (q - p) * (2.f/3 - t) * 6;
            return p;
        };
        if (s == 0) return {l, l, l, a};
        float q = l < .5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        return {hue2rgb(p, q, h + 1.f/3), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.f/3), a};
    }

    void toHSL(float& h, float& s, float& l) const {
        float mx = std::max({r, g, b}), mn = std::min({r, g, b});
        l = (mx + mn) / 2.f;
        if (mx == mn) { h = s = 0; return; }
        float d = mx - mn;
        s = l > .5f ? d / (2 - mx - mn) : d / (mx + mn);
        if      (mx == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
        else if (mx == g) h = (b - r) / d + 2.f;
        else              h = (r - g) / d + 4.f;
        h /= 6.f;
    }

    Color withAlpha(float na) const { return {r, g, b, na}; }
    Color lighter(float f = 1.2f) const {
        float h, s, l; toHSL(h, s, l);
        return fromHSL(h, s, std::min(l * f, 1.f), a);
    }
    Color darker(float f = .8f) const { return lighter(f); }

    static Color lerp(const Color& a, const Color& b, float t) {
        return {a.r + (b.r - a.r)*t, a.g + (b.g - a.g)*t,
                a.b + (b.b - a.b)*t, a.a + (b.a - a.a)*t};
    }

    static Color white() { return {1,1,1,1}; }
    static Color black() { return {0,0,0,1}; }
    static Color transparent() { return {0,0,0,0}; }
};

enum class Anchor { TopLeft, TopCenter, TopRight, CenterLeft, Center, CenterRight,
                    BottomLeft, BottomCenter, BottomRight };

inline Vec2 anchorOffset(Anchor a, const Vec2& sz) {
    float ox = 0, oy = 0;
    switch (a) {
        case Anchor::TopCenter: case Anchor::Center: case Anchor::BottomCenter:
            ox = -sz.x * .5f; break;
        case Anchor::TopRight: case Anchor::CenterRight: case Anchor::BottomRight:
            ox = -sz.x; break;
        default: break;
    }
    switch (a) {
        case Anchor::CenterLeft: case Anchor::Center: case Anchor::CenterRight:
            oy = -sz.y * .5f; break;
        case Anchor::BottomLeft: case Anchor::BottomCenter: case Anchor::BottomRight:
            oy = -sz.y; break;
        default: break;
    }
    return {ox, oy};
}

struct EdgeInsets {
    float top = 0, right = 0, bottom = 0, left = 0;
    EdgeInsets() = default;
    EdgeInsets(float all) : top(all), right(all), bottom(all), left(all) {}
    EdgeInsets(float t, float r, float b, float l) : top(t), right(r), bottom(b), left(l) {}
    float horizontal() const { return left + right; }
    float vertical()   const { return top + bottom; }
};

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

template<typename T>
inline T alignUp(T val, T align) { return (val + align - 1) & ~(align - 1); }

} // namespace nxui
