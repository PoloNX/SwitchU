#include "BatteryWidget.hpp"
#include "BatteryDrawing.hpp"
#include <nxui/core/Renderer.hpp>
#include <switch.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

using switchu::battery_drawing::drawLightningBolt;

void BatteryWidget::setBatteryStatus(uint32_t percentage, bool charging) {
    if (percentage > 100)
        percentage = 100;
    m_level = static_cast<float>(percentage) / 100.f;
    m_charging = charging;
    m_timer = 0.f;
}

void BatteryWidget::onContentUpdate(float dt) {
    if (m_charging)
        m_chargeAnim += dt;
#ifdef SWITCHU_MENU
    // The daemon owns PSM and pushes state changes. Duplicating the query here
    // woke both processes periodically and could race service teardown.
    return;
#else
    m_timer += dt;
    if (m_timer < 1.f && m_level >= 0.f) return;
    m_timer = 0.f;

    u32 charge = 100;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&charge))) {
        if (charge > 100)
            charge = 100;
        m_level = charge / 100.f;
    }

    PsmChargerType ct = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&ct)))
        m_charging = (ct != PsmChargerType_Unconnected);
#endif
}

void BatteryWidget::onContentRender(nxui::Renderer& ren) {
    nxui::Rect cr = contentRect();
    float bw = 46.f, bh = 22.f;
    float boltSlotW = m_charging ? 22.f : 0.f;
    float gap = m_charging ? 8.f : 0.f;
    float groupW = bw + gap + boltSlotW;
    float bx = cr.x + (cr.width - groupW) * 0.5f;
    float textH = m_font ? m_font->measure("100%").y * 0.75f : 14.f;
    float contentH = bh + 6.f + textH;
    float by = cr.y + (cr.height - contentH) * 0.5f;
    float op = m_opacity;

    float level = m_level;
    if (level < 0.f) level = 0.f;
    if (level > 1.f) level = 1.f;

    nxui::Rect body = {bx, by, bw, bh};
    float radius = bh * 0.44f;
    nxui::Color shell = m_textColor.withAlpha(0.20f * op);
    nxui::Color shellTop = nxui::Color::white().withAlpha(0.16f * op);
    nxui::Color shellEdge = m_textColor.withAlpha(0.52f * op);
    nxui::Color terminal = m_textColor.withAlpha(0.38f * op);

    ren.drawRoundedRect(body, shell, radius);
    ren.drawRoundedRect({body.x + 2.f, body.y + 2.f, body.width - 4.f, body.height * 0.42f},
                        shellTop, radius * 0.72f);
    ren.drawRoundedRectOutline(body, shellEdge, radius, 1.35f);
    ren.drawRoundedRect({body.right() + 2.f, body.y + bh * 0.32f, 4.8f, bh * 0.36f},
                        terminal, 2.4f);

    float chargePulse = m_charging ? (0.74f + 0.26f * (0.5f + 0.5f * std::sin(m_chargeAnim * 5.2f))) : 1.f;
    nxui::Color fill = level > 0.20f
        ? nxui::Color(0.32f, 0.93f, 0.52f, op * chargePulse)
        : nxui::Color(0.95f, 0.24f, 0.20f, op);
    if (m_charging && level > 0.20f)
        fill = nxui::Color(0.46f, 0.96f, 0.66f, op * chargePulse);

    nxui::Rect inner = body.shrunk(3.4f);
    float innerW = std::max(0.f, inner.width * level);
    if (innerW > 0.5f) {
        nxui::Rect fillRect = {inner.x, inner.y, innerW, inner.height};
        ren.drawRoundedRect(fillRect, fill, std::min(radius * 0.68f, fillRect.width * 0.5f));
        ren.drawRoundedRect({fillRect.x + 1.4f, fillRect.y + 1.2f,
                             std::max(0.f, fillRect.width - 2.8f), fillRect.height * 0.34f},
                            nxui::Color::white().withAlpha(0.16f * op * chargePulse),
                            std::min(radius * 0.45f, fillRect.width * 0.45f));
    }

    if (m_charging) {
        float boltPulse = 0.70f + 0.30f * (0.5f + 0.5f * std::sin(m_chargeAnim * 6.8f));
        float boltH = 27.f + 1.6f * boltPulse;
        float boltW = boltH * 0.78f;
        float boltX = body.right() + gap + (boltSlotW - boltW) * 0.5f;
        float boltY = by + (bh - boltH) * 0.5f - 0.5f;
        nxui::Rect boltRect = {boltX, boltY, boltW, boltH};

        nxui::Color glow = nxui::Color(1.f, 0.74f, 0.12f, 0.18f * op * boltPulse);
        drawLightningBolt(ren, boltRect.expanded(2.8f), glow, glow.withAlpha(0.f), 0.f);

        nxui::Color boltColor = nxui::Color(1.f, 0.86f, 0.18f, op * (0.88f + 0.12f * boltPulse));
        nxui::Color boltEdge = nxui::Color(1.f, 0.64f, 0.08f, op * 0.72f);
        drawLightningBolt(ren, boltRect, boltColor, boltEdge, 1.15f);
        drawLightningBolt(ren, boltRect.shrunk(2.4f),
                          nxui::Color::white().withAlpha(0.12f * op),
                          nxui::Color::white().withAlpha(0.f), 0.f);
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
    float bw = 46.f, bh = 22.f;
    float iconExtra = 8.f + 22.f;
    float textH = m_font ? m_font->measure("100%").y * 0.75f : 14.f;
    return {bw + 4.f + iconExtra, bh + 6.f + textH};
}
