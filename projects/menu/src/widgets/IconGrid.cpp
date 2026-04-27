#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>


IconGrid::IconGrid() {}

void IconGrid::setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
                     int cols, int rows,
                     float cellW, float cellH,
                     float padX, float padY)
{
    m_allIcons = std::move(icons);
    m_cols  = cols;  m_rows = rows;
    m_cellW = cellW; m_cellH = cellH;
    m_padX  = padX;  m_padY  = padY;

    int perPage = iconsPerPage();
    m_totalPages = std::max(1, ((int)m_allIcons.size() + perPage - 1) / perPage);

    float gridW = m_cols * m_cellW + (m_cols - 1) * m_padX;
    float gridH = m_rows * m_cellH + (m_rows - 1) * m_padY;
    m_originX = (m_rect.width  - gridW) * 0.5f + m_rect.x;
    m_originY = (m_rect.height - gridH) * 0.5f + m_rect.y;

    setPage(0);
}

void IconGrid::setPage(int page) {
    m_page = std::clamp(page, 0, m_totalPages - 1);
    layoutPage();
}

void IconGrid::layoutPage() {
    nxui::Widget* prevFocused = m_focus.current();

    clearChildren();
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());

    std::vector<nxui::Widget*> fItems;

    for (int i = start; i < end; ++i) {
        auto& icon = m_allIcons[i];
        int local  = i - start;
        int col    = local % m_cols;
        int row    = local / m_cols;
        float x = m_originX + col * (m_cellW + m_padX);
        float y = m_originY + row * (m_cellH + m_padY);
        icon->setRect({x, y, m_cellW, m_cellH});
        addChild(icon);
        if (icon->isFocusable())
            fItems.push_back(icon.get());
    }

    m_focus.setGrid(fItems, m_cols);
    if (prevFocused) {
        for (auto* item : fItems) {
            if (item == prevFocused) {
                m_focus.setFocus(prevFocused);
                break;
            }
        }
    }
}

int IconGrid::focusedGlobalIndex() const {
    auto* cur = m_focus.current();
    if (!cur)
        return -1;
    for (int i = 0; i < (int)m_allIcons.size(); ++i) {
        if (m_allIcons[i].get() == cur)
            return i;
    }
    return -1;
}

bool IconGrid::focusGlobalIndex(int idx) {
    if (idx < 0 || idx >= (int)m_allIcons.size())
        return false;
    if (!m_allIcons[idx] || !m_allIcons[idx]->isFocusable())
        return false;

    int perPage = iconsPerPage();
    if (perPage <= 0)
        return false;

    int wantedPage = idx / perPage;
    if (wantedPage != m_page)
        setPage(wantedPage);

    m_focus.setFocus(m_allIcons[idx].get());
    return true;
}

bool IconGrid::swapSlots(int a, int b) {
    if (a < 0 || b < 0 || a >= (int)m_allIcons.size() || b >= (int)m_allIcons.size())
        return false;
    if (a == b)
        return true;

    std::swap(m_allIcons[a], m_allIcons[b]);
    layoutPage();
    return true;
}

std::vector<GlossyIcon*> IconGrid::pageIcons() const {
    std::vector<GlossyIcon*> out;
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i) out.push_back(m_allIcons[i].get());
    return out;
}

int IconGrid::hitTest(float screenX, float screenY) const {
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i) {
        nxui::Rect r = m_allIcons[i]->focusRect();
        if (r.contains(screenX, screenY))
            return i - start;
    }
    return -1;
}

void IconGrid::startAppearAnimation() {
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    int maxDist = (m_cols - 1) + (m_rows - 1);
    for (int i = start; i < end; ++i) {
        int local = i - start;
        int col   = local % m_cols;
        int row   = local / m_cols;
        float t   = maxDist > 0 ? (float)(col + row) / maxDist : 0.f;
        float delay = t * 0.40f;
        m_allIcons[i]->startAppear(delay);
    }
}

void IconGrid::startWaveTransition(int targetPage) {
    targetPage = std::clamp(targetPage, 0, m_totalPages - 1);
    if (targetPage == m_page) return;

    m_waveActive     = true;
    m_waveTargetPage = targetPage;
    m_waveTime       = 0.f;
    m_wavePhase      = WavePhase::Capture;
}

void IconGrid::onUpdate(float dt) {
    if (m_waveActive && m_wavePhase == WavePhase::Animating) {
        m_waveTime += dt;
        if (m_waveTime >= m_waveDuration) {
            m_waveActive = false;
            m_wavePhase  = WavePhase::Idle;
        }
    }
}

void IconGrid::render(nxui::Renderer& ren) {
    if (!m_visible || m_opacity <= 0.f) return;

    if (m_wavePhase == WavePhase::Capture) {
        for (auto& c : m_children) c->render(ren);

        ren.captureToOffscreen();

        setPage(m_waveTargetPage);
        startAppearAnimation();

        m_wavePhase = WavePhase::Animating;
        m_waveTime  = 0.f;

        ren.applyWave(0.f, 0.015f, 12.f);

        if (m_onPageSwitched) m_onPageSwitched();
        return;
    }

    for (auto& c : m_children) c->render(ren);

    if (m_waveActive && m_wavePhase == WavePhase::Animating) {
        float t = m_waveTime / m_waveDuration;
        float fade = 1.f - t;
        float amplitude = 0.012f * fade;
        if (amplitude > 0.001f) {
            ren.captureToOffscreen();
            ren.applyWave(m_waveTime * 8.f, amplitude, 12.f);
        }
    }
}

void IconGrid::onRender(nxui::Renderer&) {
}

