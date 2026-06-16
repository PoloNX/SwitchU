#include "DateTimeWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <ctime>
#include <cstdio>

void DateTimeWidget::setUse12HourClock(bool enabled) {
    if (m_use12HourClock == enabled)
        return;

    m_use12HourClock = enabled;
    m_timeStr.clear();
    m_timer = 1.f;
}

void DateTimeWidget::onContentUpdate(float dt) {
    m_timer += dt;
    if (m_timer < 1.f && !m_timeStr.empty()) return;
    m_timer = 0.f;

    std::time_t t = std::time(nullptr);
    std::tm* tm   = std::localtime(&t);
    if (!tm) return;
    char buf[64];
    if (m_use12HourClock) {
        int hour = tm->tm_hour % 12;
        if (hour == 0)
            hour = 12;
        std::snprintf(buf, sizeof(buf), "%d:%02d %s", hour, tm->tm_min,
                      tm->tm_hour >= 12 ? "PM" : "AM");
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
    }
    m_timeStr = buf;
    std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d",
                  tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);
    m_dateStr = buf;
}

void DateTimeWidget::onContentRender(nxui::Renderer& ren) {
    if (!m_font) return;

    nxui::Rect cr = contentRect();
    nxui::Font* sf = m_smallFont ? m_smallFont : m_font;

    nxui::Vec2 timeSz = m_font->measure(m_timeStr);
    nxui::Vec2 dateSz = sf->measure(m_dateStr);
    float contentH = timeSz.y + 2.f + dateSz.y * 0.7f;
    float tx = cr.x + (cr.width - timeSz.x) * 0.5f;
    float ty = cr.y + (cr.height - contentH) * 0.5f;
    ren.drawText(m_timeStr, {tx, ty}, m_font, m_textColor.withAlpha(m_opacity), 1.f);

    float dx = cr.x + (cr.width - dateSz.x * 0.7f) * 0.5f;
    float dy = ty + timeSz.y + 2.f;
    ren.drawText(m_dateStr, {dx, dy}, sf, m_secondaryColor.withAlpha(m_opacity), 0.7f);
}

nxui::Vec2 DateTimeWidget::computeContentSize() const {
    if (!m_font) return {130.f, 46.f};
    nxui::Vec2 timeSz = m_font->measure(m_use12HourClock ? "12:00 PM" : "00:00");
    nxui::Font* sf = m_smallFont ? m_smallFont : m_font;
    nxui::Vec2 dateSz = sf->measure("00/00/0000");
    float w = std::max(timeSz.x, dateSz.x * 0.7f);
    float h = timeSz.y + 2.f + dateSz.y * 0.7f;
    return {w, h};
}
