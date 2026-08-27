#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Input.hpp>
#include <algorithm>
#include <cmath>


IconGrid::IconGrid() {}

namespace {

float clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

} // namespace

void IconGrid::setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
                     int cols, int rows,
                     float cellW, float cellH,
                     float padX, float padY)
{
    m_allIcons = std::move(icons);
    reconfigureLayout(cols, rows, cellW, cellH, padX, padY);
}

void IconGrid::setLayoutMode(AppLayoutMode mode) {
    if (m_layoutMode == mode) return;
    m_layoutMode = mode;
    int cur = focusedGlobalIndex();
    m_layoutReveal.setImmediate(0.86f);
    m_layoutReveal.set(1.f, 0.24f, nxui::Easing::outCubic);
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        m_lineScrollOffset.setImmediate(cur >= 0 ? static_cast<float>(cur) : 0.f);
        layoutLine();
    } else {
        setPage(cur >= 0 ? cur / std::max(1, iconsPerPage()) : m_page);
        layoutPage();
    }
    if (cur >= 0 && cur < (int)m_allIcons.size() && m_allIcons[cur]->isFocusable()) {
        m_focus.setFocus(m_allIcons[cur].get());
    }
}

bool IconGrid::isDynamicLineScrolling() const {
    return m_layoutMode == AppLayoutMode::DynamicLine
        && std::abs(m_lineScrollOffset.value() - m_lineScrollOffset.target()) > 0.01f;
}

void IconGrid::setDynamicLineUpTarget(nxui::Widget* target) {
    m_lineUpTarget = target;
    if (m_layoutMode != AppLayoutMode::DynamicLine)
        return;
    for (auto& icon : m_allIcons) {
        if (icon)
            icon->setCustomNavigation(nxui::FocusDirection::UP, m_lineUpTarget);
    }
}

void IconGrid::setDynamicLineDownTarget(nxui::Widget* target) {
    m_lineDownTarget = target;
    if (m_layoutMode != AppLayoutMode::DynamicLine)
        return;
    for (auto& icon : m_allIcons) {
        if (icon)
            icon->setCustomNavigation(nxui::FocusDirection::DOWN, m_lineDownTarget);
    }
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

    if (m_layoutMode == AppLayoutMode::DynamicLine)
        layoutLine();
    else
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
        icon->setCustomNavigation(nxui::FocusDirection::LEFT, nullptr);
        icon->setCustomNavigation(nxui::FocusDirection::RIGHT, nullptr);
        icon->setCustomNavigation(nxui::FocusDirection::UP, nullptr);
        icon->setCustomNavigation(nxui::FocusDirection::DOWN, m_lineDownTarget);
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

void IconGrid::layoutLine() {
    nxui::Widget* prevFocused = m_focus.current();
    clearChildren();

    std::vector<nxui::Widget*> fItems;
    fItems.reserve(m_allIcons.size());
    for (size_t i = 0; i < m_allIcons.size(); ++i) {
        auto& icon = m_allIcons[i];
        int left = -1;
        for (int j = (int)i - 1; j >= 0; --j) {
            if (m_allIcons[(size_t)j] && m_allIcons[(size_t)j]->isFocusable()) {
                left = j;
                break;
            }
        }
        int right = -1;
        for (int j = (int)i + 1; j < (int)m_allIcons.size(); ++j) {
            if (m_allIcons[(size_t)j] && m_allIcons[(size_t)j]->isFocusable()) {
                right = j;
                break;
            }
        }
        icon->setCustomNavigation(nxui::FocusDirection::LEFT,
                                  left >= 0 ? m_allIcons[(size_t)left].get() : nullptr);
        icon->setCustomNavigation(nxui::FocusDirection::RIGHT,
                                  right >= 0 ? m_allIcons[(size_t)right].get() : nullptr);
        icon->setCustomNavigation(nxui::FocusDirection::UP, m_lineUpTarget);
        icon->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
        addChild(icon);
        if (icon->isFocusable())
            fItems.push_back(icon.get());
    }

    m_focus.setGrid(fItems, std::max(1, (int)fItems.size()));

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

nxui::Rect IconGrid::dynamicIconRect(int index, float* outScale,
                                     float* outOpacity,
                                     float* outDistance) const {
    const float centerX = m_rect.x + m_rect.width * 0.5f;
    // Leave a dedicated control strip below the profiles, then place the app
    // carousel in the lower half of the HOME scene.
    const float centerY = m_rect.y + m_rect.height * 0.66f;
    const float offset = m_lineScrollOffset.value();
    const float baseCellW = m_cellW > 0.f ? m_cellW : 150.f;
    const float baseCellH = m_cellH > 0.f ? m_cellH : 150.f;
    // The old extra 36 px made neighbouring apps feel disconnected. A small,
    // stable gutter keeps the row compact even when grid padding is reconfigured.
    const float lineSpacing = baseCellW + std::max(8.f, m_padX * 0.4f);

    float d = static_cast<float>(index) - offset;
    const float absD = std::abs(d);
    float s = 1.f;
    float a = 1.f;
    if (absD <= 1.0f) {
        // Position drives the visual state: the departing icon now shrinks as
        // it leaves centre while the incoming icon travels and grows into it.
        const float centerBlend = 1.f - absD;
        const float smoothBlend = centerBlend * centerBlend * (3.f - 2.f * centerBlend);
        s = 0.82f + smoothBlend * (1.36f - 0.82f);
        a = 0.76f + smoothBlend * 0.24f;
    } else {
        s = std::max(0.54f, 0.82f - (absD - 1.0f) * 0.12f);
        a = std::max(0.0f, 0.76f - (absD - 1.0f) * 0.24f);
    }

    const float reveal = clamp01(m_layoutReveal.value());
    const float revealScale = 0.94f + reveal * 0.06f;
    s *= revealScale;
    a *= reveal;

    const float liftT = std::min(absD, 1.f);
    const float smoothLift = liftT * liftT * (3.f - 2.f * liftT);
    const float sideLift = 32.f * smoothLift;
    const float w = baseCellW * s;
    const float h = baseCellH * s;
    const float x = centerX + d * lineSpacing - w * 0.5f;
    const float y = centerY - h * 0.5f - sideLift;

    if (outScale) *outScale = s;
    if (outOpacity) *outOpacity = a;
    if (outDistance) *outDistance = absD;
    return {x, y, w, h};
}

nxui::Rect IconGrid::focusedDisplayRect() const {
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        const int focused = focusedGlobalIndex();
        if (focused >= 0)
            return dynamicIconRect(focused);
    }
    if (auto* cur = m_focus.current())
        return cur->focusRect();
    return {};
}

bool IconGrid::focusGlobalIndex(int idx) {
    if (idx < 0 || idx >= (int)m_allIcons.size())
        return false;
    if (!m_allIcons[idx] || !m_allIcons[idx]->isFocusable())
        return false;

    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        m_focus.setFocus(m_allIcons[idx].get());
        m_lineScrollOffset.set(static_cast<float>(idx), kLineScrollDuration,
                               nxui::Easing::outCubic);
        return true;
    }

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
    if (m_layoutMode == AppLayoutMode::DynamicLine)
        layoutLine();
    else
        layoutPage();
    return true;
}

std::vector<GlossyIcon*> IconGrid::pageIcons() const {
    std::vector<GlossyIcon*> out;
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        int cur = focusedGlobalIndex();
        int center = cur >= 0 ? cur : 0;
        int start = std::max(0, center - 4);
        int end = std::min((int)m_allIcons.size(), center + 5);
        for (int i = start; i < end; ++i)
            out.push_back(m_allIcons[i].get());
        return out;
    }
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i) out.push_back(m_allIcons[i].get());
    return out;
}

int IconGrid::hitTest(float screenX, float screenY) const {
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        for (int i = 0; i < (int)m_allIcons.size(); ++i) {
            nxui::Rect r = dynamicIconRect(i);
            if (r.contains(screenX, screenY))
                return i;
        }
        return -1;
    }
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
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        int cur = focusedGlobalIndex();
        int center = cur >= 0 ? cur : 0;
        for (int i = 0; i < (int)m_allIcons.size(); ++i) {
            float dist = static_cast<float>(std::abs(i - center));
            float delay = std::min(0.40f, dist * 0.06f);
            m_allIcons[i]->startAppear(delay);
        }
        return;
    }
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
    if (m_layoutMode == AppLayoutMode::DynamicLine) return;

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
    m_layoutReveal.update(dt);
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        m_lineScrollOffset.update(dt);
        int cur = focusedGlobalIndex();
        if (cur >= 0 && std::abs(m_lineScrollOffset.target() - static_cast<float>(cur)) > 0.001f) {
            m_lineScrollOffset.set(static_cast<float>(cur), kLineScrollDuration,
                                   nxui::Easing::outCubic);
        }

        for (int i = 0; i < (int)m_allIcons.size(); ++i) {
            m_allIcons[i]->setRect(dynamicIconRect(i));
        }
        return;
    }

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

void IconGrid::renderDynamicLine(nxui::Renderer& ren) {
    ren.pushClipRect(m_rect);

    struct RenderCandidate {
        int index;
        float absD;
        float d;
        float s;
        float a;
    };
    std::vector<RenderCandidate> candidates;
    candidates.reserve(m_allIcons.size());

    for (int i = 0; i < (int)m_allIcons.size(); ++i) {
        float s = 1.f;
        float a = 1.f;
        float absD = 0.f;
        const nxui::Rect r = dynamicIconRect(i, &s, &a, &absD);
        if (absD > 4.5f && i != focusedGlobalIndex()) continue;
        const float d = r.center().x - m_rect.center().x;
        candidates.push_back({i, absD, d, s, a});
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.absD > rhs.absD;
    });

    for (const auto& c : candidates) {
        auto& icon = m_allIcons[c.index];
        const nxui::Rect savedRect = icon->rect();
        const float savedOp = icon->opacity();

        icon->setRect(dynamicIconRect(c.index));
        icon->setOpacity(savedOp * c.a);
        icon->render(ren);

        icon->setRect(savedRect);
        icon->setOpacity(savedOp);
    }

    ren.popClipRect();
}

void IconGrid::render(nxui::Renderer& ren) {
    if (!m_visible || m_opacity <= 0.f) return;

    if (!m_children.empty() && ren.gpu().offscreenReady())
        ren.captureToOffscreen(true);

    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        renderDynamicLine(ren);
        return;
    }

    const float reveal = clamp01(m_layoutReveal.value());
    if (reveal < 0.999f) {
        for (auto& c : m_children) {
            const float savedOp = c->opacity();
            c->setOpacity(savedOp * reveal);
            c->render(ren);
            c->setOpacity(savedOp);
        }
        return;
    }

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
