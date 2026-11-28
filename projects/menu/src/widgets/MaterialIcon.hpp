#pragma once

#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

namespace switchu::material {

inline std::string utf8(std::uint32_t cp) {
    std::string out;
    if (cp == 0)
        return out;
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    return out;
}

// Draw a Material Icons glyph centered in an edge×edge box (keeps QS/HOME sizes).
inline bool drawCentered(nxui::Renderer& ren,
                         nxui::Font* font,
                         std::uint32_t codepoint,
                         nxui::Vec2 center,
                         const nxui::Color& color,
                         float edgePx) {
    if (!font || !font->valid() || codepoint == 0 || edgePx < 4.f)
        return false;
    const std::string text = utf8(codepoint);
    const nxui::Vec2 size = font->measure(text);
    if (size.x < 1.f || size.y < 1.f)
        return false;
    const float s = std::clamp(edgePx / size.y, 0.35f, 2.0f);
    ren.drawText(text,
                 {center.x - size.x * s * 0.5f, center.y - size.y * s * 0.5f},
                 font, color, s);
    return true;
}

} // namespace switchu::material
