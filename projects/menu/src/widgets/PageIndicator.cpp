#include "PageIndicator.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

PageIndicator::PageIndicator() {
    setPadding(10.f, 18.f, 10.f, 18.f);
}

void PageIndicator::setPageCount(int total) {
    int clampedTotal = std::max(1, total);
    if (clampedTotal == m_total && m_geometryReady) return;

    m_total = clampedTotal;
    m_current = std::clamp(m_current, 0, m_total - 1);
    updateGeometryFromContent();
}

void PageIndicator::setCurrentPage(int page) {
    int clamped = std::clamp(page, 0, std::max(0, m_total - 1));
    if (clamped == m_current) return;
    m_current = clamped;
    m_popT = 0.f;
}

void PageIndicator::onUpdate(float dt) {
    if (m_popT < kDotPopDuration)
        m_popT = std::min(kDotPopDuration, m_popT + dt);
}

void PageIndicator::updateGeometryFromContent() {
    if (m_rect.width > 0.f)
        m_layoutWidth = std::max(m_layoutWidth, m_rect.width);

    float topY = m_rect.y;
    sizeToFit();
    setCornerRadius(m_rect.height * 0.5f);
    m_rect.x = (m_layoutWidth - m_rect.width) * 0.5f;
    m_rect.y = topY;
    m_geometryReady = true;
}

void PageIndicator::onContentRender(nxui::Renderer& ren) {
    nxui::Rect cr = contentRect();
    if (m_total <= 1) return;

    constexpr float dotR = 4.f;
    constexpr float gap  = 14.f;

    float dotsW = m_total * dotR * 2.f + (m_total - 1) * gap;
    float startX = cr.x + (cr.width - dotsW) * 0.5f;
    float cy = cr.y + cr.height * 0.5f;
    float op = m_opacity;

    nxui::Color inactive = m_theme ? m_theme->pageIndicator : nxui::Color(1.f, 1.f, 1.f, 0.35f);
    nxui::Color active   = m_hasActiveColor
        ? m_activeColor
        : (m_theme ? m_theme->pageIndicatorActive : nxui::Color(1.f, 1.f, 1.f, 0.95f));

    // Pop: the fresh dot swells then settles while a ripple fades outwards,
    // echoing the page slide without moving the pill's layout.
    const bool popping = m_popT < kDotPopDuration;
    const float popP = popping
        ? std::clamp(m_popT / kDotPopDuration, 0.f, 1.f) : 1.f;
    const float activeR = dotR * (1.f + 0.34f * std::sin(popP * 3.14159265f));

    for (int i = 0; i < m_total; ++i) {
        float cx = startX + i * (dotR * 2.f + gap) + dotR;
        nxui::Color c = (i == m_current) ? active : inactive;
        if (i == m_current && popping) {
            const float ringR = dotR + 11.f * popP;
            ren.drawCircle({cx, cy}, ringR,
                           active.withAlpha(active.a * 0.30f * (1.f - popP) * op), 20);
        }
        const float r = (i == m_current) ? activeR : dotR;
        ren.drawCircle({cx, cy}, r, c.withAlpha(c.a * op), 16);
    }

}

nxui::Vec2 PageIndicator::computeContentSize() const {
    if (m_total <= 1) return {0.f, 0.f};

    constexpr float dotR = 4.f;
    constexpr float gap  = 14.f;

    float dotsW = m_total * dotR * 2.f + (m_total - 1) * gap;
    float dotsH = dotR * 2.f;
    return {dotsW, dotsH};
}
