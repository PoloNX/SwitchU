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

    bool btEnabled = false;
    if (R_SUCCEEDED(setsysGetBluetoothEnableFlag(&btEnabled)))
        m_btEnabled = btEnabled;
    else
        m_btEnabled = false;
}

void BatteryWidget::onContentRender(nxui::Renderer& ren) {
    nxui::Rect cr = contentRect();
    float bw = 36.f, bh = 18.f;
    float iconSize = 12.f;
    float boltW = m_charging ? 14.f : 0.f;
    float boltGap = m_charging ? 6.f : 0.f;
    float btGap = 8.f;
    float textH = m_font ? m_font->measure("100%").y * 0.75f : 14.f;
    float contentH = bh + 6.f + textH;
    float totalW = bw + boltGap + boltW + btGap + iconSize;
    float bx = cr.x + (cr.width - totalW) * 0.5f;
    float by = cr.y + (cr.height - contentH) * 0.5f;
    float op = m_opacity;
    
    // Battery outline and nozzle
    ren.drawRectOutline({bx, by, bw, bh}, m_textColor.withAlpha(0.7f * op), 1.5f);
    ren.drawRect({bx + bw, by + bh * 0.25f, 4, bh * 0.5f}, m_textColor.withAlpha(0.7f * op));

    float level = m_level;
    if (level < 0.f) level = 0.f;
    if (level > 1.f) level = 1.f;

    nxui::Color fill = level > 0.2f ? nxui::Color(0.3f, 0.9f, 0.3f, op) : nxui::Color(0.9f, 0.2f, 0.2f, op);
    float innerW = (bw - 4) * level;
    if (innerW > 0.f)
        ren.drawRect({bx + 2, by + 2, innerW, bh - 4}, fill);

    // Charging bolt (right of battery)
    float boltX = bx + bw + boltGap;
    if (m_charging) {
        float boltY = by + (bh - 22.f) * 0.5f;
        nxui::Color boltColor = nxui::Color(1.f, 0.92f, 0.25f, op);

        nxui::Vec2 p1 = {boltX + 6.5f, boltY};
        nxui::Vec2 p2 = {boltX + 13.f, boltY + 7.7f};
        nxui::Vec2 p3 = {boltX + 8.4f, boltY + 7.7f};
        ren.drawTriangle(p1, p2, p3, boltColor);

        ren.drawRect({boltX + 5.88f, boltY + 8.8f, 5.88f, 4.84f}, boltColor);

        nxui::Vec2 p6 = {boltX + 4.9f, boltY + 15.4f};
        nxui::Vec2 p7 = {boltX + 13.f, boltY + 22.f};
        nxui::Vec2 p8 = {boltX + 8.1f, boltY + 15.4f};
        ren.drawTriangle(p6, p7, p8, boltColor);
    }

// 1. Configuración de Layout y Color
float btX = bx + bw + boltGap + boltW + btGap;
float btY = by + (bh - iconSize) * 0.5f;

nxui::Color btColor = m_btEnabled
    ? nxui::Color(0.0f, 0.5f, 1.0f, op) // Azul Bluetooth puro
    : m_textColor.withAlpha(0.25f * op);

// 2. Parámetros de Geometría (Maximizados)
float cx = btX + iconSize * 0.5f; // Eje vertical
float cy = btY + iconSize * 0.5f; // Centro horizontal

// Definimos los límites para que el icono sea lo más grande posible dentro de iconSize
float h = iconSize * 0.5f;   // Radio vertical (ocupa todo el alto)
float w = iconSize * 0.35f;  // Ancho de las alas
float lw = iconSize * 0.12f; // Grosor de línea dinámico (muy importante para la visibilidad)

// Coordenadas de los nodos rúnicos
float top = cy - h;
float bot = cy + h;
float midTop = cy - h * 0.45f;
float midBot = cy + h * 0.45f;

// --- DIBUJO DETALLADO ---

// A. Espina Dorsal (La línea vertical central es la base)
ren.drawLine({cx, top}, {cx, bot}, btColor, lw);

// B. Trazo Superior (Cola Izquierda -> Pico Derecho -> Punto Inferior)
// Este trazo forma la mitad superior del logo
ren.drawLine({cx - w, midTop}, {cx + w, midBot}, btColor, lw);
ren.drawLine({cx + w, midBot}, {cx, bot}, btColor, lw);

// C. Trazo Inferior (Cola Izquierda -> Pico Derecho -> Punto Superior)
// Este trazo forma la mitad inferior y cruza con el anterior en el centro exacto
ren.drawLine({cx - w, midBot}, {cx + w, midTop}, btColor, lw);
ren.drawLine({cx + w, midTop}, {cx, top}, btColor, lw);

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
    float iconSize = 12.f;
    float boltW = m_charging ? 14.f : 0.f;
    float boltGap = m_charging ? 6.f : 0.f;
    float btGap = 8.f;
    float textH = m_font ? m_font->measure("100%").y * 0.75f : 14.f;
    return {bw + boltGap + boltW + btGap + iconSize + 4.f, bh + 6.f + textH};
}

