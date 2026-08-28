#pragma once

#include <nxui/core/Renderer.hpp>

namespace switchu::battery_drawing {

inline nxui::Vec2 boltPoint(const nxui::Rect& r, float x, float y) {
    return {r.x + (x / 16.f) * r.width, r.y + (y / 16.f) * r.height};
}

inline void drawLightningBolt(nxui::Renderer& ren, const nxui::Rect& r,
                              const nxui::Color& fill,
                              const nxui::Color& edge,
                              float outlineThickness) {
    const nxui::Vec2 p0 = boltPoint(r, 4.732f, 7.95335f);
    const nxui::Vec2 p1 = boltPoint(r, 6.90908f, 2.f);
    const nxui::Vec2 p2 = boltPoint(r, 10.54547f, 2.f);
    const nxui::Vec2 p3 = boltPoint(r, 8.36364f, 7.01316f);
    const nxui::Vec2 p4 = boltPoint(r, 11.27275f, 7.01316f);
    const nxui::Vec2 p5 = boltPoint(r, 4.72725f, 14.f);
    const nxui::Vec2 p6 = boltPoint(r, 6.93656f, 7.95135f);

    ren.drawTriangle(p0, p1, p2, fill);
    ren.drawTriangle(p0, p2, p3, fill);
    ren.drawTriangle(p0, p3, p6, fill);
    ren.drawTriangle(p6, p3, p4, fill);
    ren.drawTriangle(p6, p4, p5, fill);

    if (outlineThickness <= 0.f) return;
    ren.drawLine(p0, p1, edge, outlineThickness);
    ren.drawLine(p1, p2, edge, outlineThickness);
    ren.drawLine(p2, p3, edge, outlineThickness);
    ren.drawLine(p3, p4, edge, outlineThickness);
    ren.drawLine(p4, p5, edge, outlineThickness);
    ren.drawLine(p5, p6, edge, outlineThickness);
    ren.drawLine(p6, p0, edge, outlineThickness);
}

} // namespace switchu::battery_drawing
