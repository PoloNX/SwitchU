#include "BatteryWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <switch.h>
#include <cstdio>


void BatteryWidget::onContentUpdate(float dt) {
    m_timer += dt;
    if (m_timer < 5.f && m_level >= 0.f) return;
    m_timer = 0.f;

    u32 charge = 100;
    psmGetBatteryChargePercentage(&charge);
    m_level = charge / 100.f;

    PsmChargerType ct = PsmChargerType_Unconnected;
    psmGetChargerType(&ct);
    m_charging = (ct != PsmChargerType_Unconnected);
}

void BatteryWidget::onContentRender(nxui::Renderer& ren) {
    nxui::Rect cr = contentRect();
    float bw = 36.f, bh = 18.f;
    float bx = cr.x + (cr.width - bw - 4.f) * 0.5f;
    float textH = m_font ? m_font->measure("100%").y * 0.75f : 14.f;
    float contentH = bh + 6.f + textH;
    float by = cr.y + (cr.height - contentH) * 0.5f;
    float op = m_opacity;

    ren.drawRectOutline({bx, by, bw, bh}, m_textColor.withAlpha(0.7f * op), 1.5f);
    ren.drawRect({bx + bw, by + bh * 0.25f, 4, bh * 0.5f}, m_textColor.withAlpha(0.7f * op));

    float level = m_level;
    if (level < 0.f) level = 0.f;
    if (level > 1.f) level = 1.f;

    nxui::Color fill = level > 0.2f ? nxui::Color(0.3f, 0.9f, 0.3f, op) : nxui::Color(0.9f, 0.2f, 0.2f, op);
    float innerW = (bw - 4) * level;
    if (innerW > 0.f)
        ren.drawRect({bx + 2, by + 2, innerW, bh - 4}, fill);

    if (m_charging) {
        float boltW = 14.f;
        float boltH = 22.f;
        float boltX = bx + bw + 5.f;
        float boltY = by + (bh - boltH) * 0.5f;

        nxui::Color boltColor = nxui::Color(1.f, 0.92f, 0.25f, op);

        nxui::Vec2 p1 = {boltX + boltW * 0.45f, boltY};
        nxui::Vec2 p2 = {boltX + boltW * 0.95f, boltY + boltH * 0.35f};
        nxui::Vec2 p3 = {boltX + boltW * 0.60f, boltY + boltH * 0.35f};
        ren.drawTriangle(p1, p2, p3, boltColor);

        nxui::Vec2 p4 = {boltX + boltW * 0.28f, boltY + boltH * 0.45f};
        nxui::Vec2 p5 = {boltX + boltW * 0.70f, boltY + boltH * 0.45f};
        ren.drawRect({boltX + boltW * 0.28f, boltY + boltH * 0.40f,
                      boltW * 0.42f, boltH * 0.22f}, boltColor);

        nxui::Vec2 p6 = {boltX + boltW * 0.35f, boltY + boltH * 0.70f};
        nxui::Vec2 p7 = {boltX + boltW * 0.95f, boltY + boltH};
        nxui::Vec2 p8 = {boltX + boltW * 0.58f, boltY + boltH * 0.70f};
        ren.drawTriangle(p6, p7, p8, boltColor);
    }

    if (m_font) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d%%", (int)(level * 100));
        nxui::Vec2 sz = m_font->measure(buf);
        float tx = cr.x + (cr.width - sz.x * 0.75f) * 0.5f;
        float ty = by + bh + 6.f;
        ren.drawText(buf, {tx, ty}, m_font, m_textColor.withAlpha(op), 0.75f);
    }
}

nxui::Vec2 BatteryWidget::computeContentSize() const {
    float bw = 36.f, bh = 18.f;
    float iconExtra = 5.f + 8.f;
    float textH = m_font ? m_font->measure("100%").y * 0.75f : 14.f;
    return {bw + 4.f + iconExtra, bh + 6.f + textH};
}

