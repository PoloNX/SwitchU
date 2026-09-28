#pragma once

#include <nxui/core/Renderer.hpp>

#include <algorithm>

// Vector glyphs shared by the HOME profile tile and the profile carousel.
namespace switchu::ui {

// Check mark centred in `r`.
inline void drawCheckmark(nxui::Renderer& ren, const nxui::Rect& r, const nxui::Color& color) {
    const float side = std::min(r.width, r.height);
    const float stroke = std::max(2.5f, side * 0.14f);
    const nxui::Vec2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const nxui::Vec2 start{c.x - side * 0.26f, c.y + side * 0.02f};
    const nxui::Vec2 corner{c.x - side * 0.06f, c.y + side * 0.22f};
    const nxui::Vec2 end{c.x + side * 0.28f, c.y - side * 0.20f};
    ren.drawLine(start, corner, color, stroke);
    ren.drawLine(corner, end, color, stroke);
    // Round the joint so the two strokes don't leave a notch.
    ren.drawCircle(corner, stroke * 0.5f, color, 12);
}

// Generic head-and-shoulders placeholder filling `r`.
inline void drawProfileSilhouette(nxui::Renderer& ren, const nxui::Rect& r,
                                  const nxui::Color& color) {
    const float cx = r.x + r.width * 0.5f;
    ren.drawCircle({cx, r.y + r.height * 0.38f}, r.height * 0.19f, color, 40);
    const float bodyW = r.width * 0.60f;
    ren.drawRoundedRect({cx - bodyW * 0.5f, r.y + r.height * 0.62f,
                         bodyW, r.height * 0.28f},
                        color, r.height * 0.14f);
}

// Plus sign centred in `r`.
inline void drawPlusSign(nxui::Renderer& ren, const nxui::Rect& r, const nxui::Color& color) {
    const float side = std::min(r.width, r.height);
    const nxui::Vec2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const float arm = side * 0.25f;
    const float bar = std::max(3.f, side * 0.075f);
    ren.drawRoundedRect({c.x - arm, c.y - bar * 0.5f, arm * 2.f, bar}, color, bar * 0.5f);
    ren.drawRoundedRect({c.x - bar * 0.5f, c.y - arm, bar, arm * 2.f}, color, bar * 0.5f);
}

} // namespace switchu::ui
