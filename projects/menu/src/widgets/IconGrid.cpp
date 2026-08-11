#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Input.hpp>
#include <algorithm>
#include <cmath>


IconGrid::IconGrid() {}

void IconGrid::setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
                     int cols, int rows,
                     float cellW, float cellH,
                     float padX, float padY)
{
    m_allIcons = std::move(icons);
    reconfigureLayout(cols, rows, cellW, cellH, padX, padY);
}

void IconGrid::reconfigureLayout(int cols, int rows,
                                 float cellW, float cellH,
                                 float padX, float padY)
{
    m_cols  = cols;  m_rows = rows;
    m_cellW = cellW; m_cellH = cellH;
    m_padX  = padX;  m_padY  = padY;

    int perPage = iconsPerPage();
    m_totalPages = std::max(1, ((int)m_allIcons.size() + perPage - 1) / perPage);

    float gridW = m_cols * m_cellW + (m_cols - 1) * m_padX;
    float gridH = m_rows * m_cellH + (m_rows - 1) * m_padY;
    m_originX = (m_rect.width  - gridW) * 0.5f + m_rect.x;
    m_originY = (m_rect.height - gridH) * 0.5f + m_rect.y;

    setPage(m_page);
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

    bindEdgeActions(start, end);

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


void IconGrid::bindEdgeActions(int start, int end) {
    if (!m_edgePaging || m_cols <= 0)
        return;
    for (int i = start; i < end; ++i) {
        nxui::Widget* w = m_allIcons[i].get();
        const int col = (i - start) % m_cols;
        if (col == m_cols - 1) {
            auto next = [this]() { if (m_onEdgePage) m_onEdgePage(+1); };
            w->addAction(static_cast<uint64_t>(nxui::Button::DRight), next);
            w->addAction(static_cast<uint64_t>(nxui::Button::LStickR), next);
            w->addAction(static_cast<uint64_t>(nxui::Button::RStickR), next);
        }
        if (col == 0) {
            auto prev = [this]() { if (m_onEdgePage) m_onEdgePage(-1); };
            w->addAction(static_cast<uint64_t>(nxui::Button::DLeft), prev);
            w->addAction(static_cast<uint64_t>(nxui::Button::LStickL), prev);
            w->addAction(static_cast<uint64_t>(nxui::Button::RStickL), prev);
        }
    }
}

void IconGrid::positionPage(int page, float dx) {
    const int start = page * iconsPerPage();
    const int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i) {
        const int local = i - start;
        m_allIcons[i]->setRect({m_originX + (local % m_cols) * (m_cellW + m_padX) + dx,
                                m_originY + (local / m_cols) * (m_cellH + m_padY),
                                m_cellW, m_cellH});
    }
}

float IconGrid::pageStride() const {
    const float gridW = m_cols * m_cellW + (m_cols - 1) * m_padX;
    return std::max(m_rect.width, (m_originX - m_rect.x) + gridW + m_padX);
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

void IconGrid::startPageTransition(int targetPage) {
    targetPage = std::clamp(targetPage, 0, m_totalPages - 1);
    if (targetPage == m_page) return;

    const int fromPage = m_page;
    setPage(targetPage);

    if (!m_slideTransition) {
        m_sliding = false;
        startAppearAnimation();
        if (m_onPageSwitched) m_onPageSwitched();
        return;
    }

    m_slidePrevPage = fromPage;
    m_slideDir = (targetPage > fromPage) ? 1 : -1;
    m_slideT = 0.f;
    m_sliding = true;

    const int start = m_page * iconsPerPage();
    const int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i)
        m_allIcons[i]->forceVisible();

    const float stride = pageStride();
    m_slideInDx  = stride * (float)m_slideDir;
    m_slideOutDx = 0.f;
    positionPage(m_page, m_slideInDx);

    if (m_onPageSwitched) m_onPageSwitched();
}

void IconGrid::onUpdate(float dt) {
    if (!m_sliding)
        return;

    m_slideT += dt;
    const float t = std::clamp(m_slideT / kSlideDuration, 0.f, 1.f);
    const float eased = nxui::Easing::outCubic(t);
    const float stride = pageStride();

    m_slideInDx  = (1.f - eased) * stride * (float)m_slideDir;
    m_slideOutDx = m_slideInDx - stride * (float)m_slideDir;
    positionPage(m_page, m_slideInDx);

    if (t >= 1.f) {
        m_sliding = false;
        m_slideInDx = m_slideOutDx = 0.f;
        positionPage(m_page, 0.f);
    }
}

void IconGrid::renderPageAt(nxui::Renderer& ren, int page, float dx) {
    const int start = page * iconsPerPage();
    const int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i) {
        auto& icon = m_allIcons[i];
        const nxui::Rect saved = icon->rect();
        const int local = i - start;
        icon->setRect({m_originX + (local % m_cols) * (m_cellW + m_padX) + dx,
                       m_originY + (local / m_cols) * (m_cellH + m_padY),
                       m_cellW, m_cellH});
        icon->render(ren);
        icon->setRect(saved);
    }
}

void IconGrid::render(nxui::Renderer& ren) {
    if (!m_visible || m_opacity <= 0.f) return;

    if (!m_children.empty() && ren.gpu().offscreenReady())
        ren.captureToOffscreen(true);

    if (m_sliding) {
        ren.pushClipRect(m_rect);
        renderPageAt(ren, m_slidePrevPage, m_slideOutDx);
        for (auto& c : m_children) c->render(ren);
        ren.popClipRect();
        return;
    }

    for (auto& c : m_children) c->render(ren);
}

void IconGrid::onRender(nxui::Renderer&) {
}
