#include <nxui/widgets/Box.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace nxui {

// Public API

void Box::layout(const Rect& bounds) {
    m_rect = bounds;
    layoutChildren();
}

void Box::layout() {
    layoutChildren();
}

void Box::onRender(Renderer& /*ren*/) {
    // Base Box draws no body by default.
}

void Box::render(Renderer& ren) {
    if (!m_visible || m_opacity <= 0.f) return;

    // Allow Box subclasses (e.g. GlassPanel/GlassWidget) to render their body.
    onRender(ren);

    // Then render children (snapshot — tree may be modified during render).
    auto kids = m_children;
    for (auto& c : kids) c->render(ren);

    // Optional debug wireframe overlay for all Box widgets, on top.
    if (!ren.boxWireframeEnabled() || !m_wireframeEnabled) return;

    const Rect r = m_rect;
    if (r.width <= 0.f || r.height <= 0.f) return;

    // Dark blue contour
    ren.drawRectOutline(r, Color(0.03f, 0.10f, 0.38f, 1.0f), 1.6f);

    // Red X from corner to corner
    constexpr float inset = 0.5f;
    Vec2 tl{r.x + inset, r.y + inset};
    Vec2 tr{r.x + r.width - inset, r.y + inset};
    Vec2 bl{r.x + inset, r.y + r.height - inset};
    Vec2 br{r.x + r.width - inset, r.y + r.height - inset};

    ren.drawLine(tl, br, Color(0.95f, 0.12f, 0.12f, 1.0f), 1.6f);
    ren.drawLine(tr, bl, Color(0.95f, 0.12f, 0.12f, 1.0f), 1.6f);
}

// Internal flex layout

void Box::layoutChildren() {
    // Gather visible children
    std::vector<Widget*> items;
    items.reserve(m_children.size());
    for (auto& c : m_children)
        if (c->isVisible()) items.push_back(c.get());
    if (items.empty()) return;

    const Rect inner = contentRect();  // area after padding
    const bool isRow = (m_axis == Axis::ROW);

    // Determine if we should reverse
    bool reversed = false;
    if (m_direction == Direction::RIGHT_TO_LEFT)  reversed = true;
    if (m_direction == Direction::LEFT_TO_RIGHT)  reversed = false;
    // INHERIT: ROW→LTR, COLUMN→TTB (no reverse)

    // Phase 1: compute base sizes along the main axis.
    float totalMain = 0.f;
    float totalGrow = 0.f;
    float totalShrink = 0.f;

    struct ItemInfo {
        float baseSize;   // requested main-axis size (from child rect)
        float finalSize;  // after grow/shrink
        float marginBefore; // margin on main-axis start side
        float marginAfter;  // margin on main-axis end side
    };
    std::vector<ItemInfo> info(items.size());

    for (size_t i = 0; i < items.size(); ++i) {
        auto* w = items[i];
        const auto& m = w->margin();

        float base = isRow ? w->rect().width : w->rect().height;
        float mBefore = isRow ? m.left : m.top;
        float mAfter  = isRow ? m.right : m.bottom;

        // Clamp to min/max
        float lo = isRow ? w->minWidth() : w->minHeight();
        float hi = isRow ? w->maxWidth() : w->maxHeight();
        base = std::max(lo, std::min(hi, base));

        info[i].baseSize    = base;
        info[i].finalSize   = base;
        info[i].marginBefore = mBefore;
        info[i].marginAfter  = mAfter;

        totalMain += base + mBefore + mAfter;
        totalGrow   += w->grow();
        totalShrink += w->shrink();
    }

    // Add gaps
    float totalGaps = m_gap * (float)(items.size() - 1);
    totalMain += totalGaps;

    float availableMain = isRow ? inner.width : inner.height;
    float freeSpace = availableMain - totalMain;

    // Phase 2: distribute free space.
    if (freeSpace > 0 && totalGrow > 0) {
        for (size_t i = 0; i < items.size(); ++i) {
            float extra = freeSpace * (items[i]->grow() / totalGrow);
            info[i].finalSize += extra;
            // Re-clamp
            float hi = isRow ? items[i]->maxWidth() : items[i]->maxHeight();
            info[i].finalSize = std::min(info[i].finalSize, hi);
        }
    } else if (freeSpace < 0 && totalShrink > 0) {
        float deficit = -freeSpace;
        for (size_t i = 0; i < items.size(); ++i) {
            float reduction = deficit * (items[i]->shrink() / totalShrink);
            info[i].finalSize = std::max(info[i].finalSize - reduction, 0.f);
            // Re-clamp
            float lo = isRow ? items[i]->minWidth() : items[i]->minHeight();
            info[i].finalSize = std::max(info[i].finalSize, lo);
        }
    }

    // Recompute total after grow/shrink for justify
    float usedMain = 0.f;
    for (size_t i = 0; i < items.size(); ++i)
        usedMain += info[i].finalSize + info[i].marginBefore + info[i].marginAfter;
    usedMain += totalGaps;
    freeSpace = availableMain - usedMain;

    // Phase 3: position items along the main axis.
    float startOffset = 0.f;
    float spacing = 0.f;   // extra spacing between items (for space-between/around/evenly)

    switch (m_justify) {
        case JustifyContent::FLEX_START:
            startOffset = 0.f;
            break;
        case JustifyContent::FLEX_END:
            startOffset = freeSpace;
            break;
        case JustifyContent::CENTER:
            startOffset = freeSpace * 0.5f;
            break;
        case JustifyContent::SPACE_BETWEEN:
            if (items.size() > 1)
                spacing = freeSpace / (float)(items.size() - 1);
            break;
        case JustifyContent::SPACE_AROUND:
            spacing = freeSpace / (float)items.size();
            startOffset = spacing * 0.5f;
            break;
        case JustifyContent::SPACE_EVENLY:
            spacing = freeSpace / (float)(items.size() + 1);
            startOffset = spacing;
            break;
    }

    // Phase 4: place each child.
    float cursor = (isRow ? inner.x : inner.y) + startOffset;
    float crossStart = isRow ? inner.y : inner.x;
    float crossSize  = isRow ? inner.height : inner.width;

    // If reversed, iterate backwards
    int start = reversed ? (int)items.size() - 1 : 0;
    int end   = reversed ? -1 : (int)items.size();
    int step  = reversed ? -1 : 1;

    for (int idx = start; idx != end; idx += step) {
        auto* w = items[idx];
        auto& inf = info[idx];

        cursor += inf.marginBefore;

        // Main-axis position & size
        float mainPos  = cursor;
        float mainSize = inf.finalSize;

        // Cross-axis position & size
        float crossMarginBefore = isRow ? w->margin().top : w->margin().left;
        float crossMarginAfter  = isRow ? w->margin().bottom : w->margin().right;
        float childCross = isRow ? w->rect().height : w->rect().width;
        float availCross = crossSize - crossMarginBefore - crossMarginAfter;

        float crossPos;
        float crossFinal = childCross;

        switch (m_align) {
            default:
            case AlignItems::AUTO:
            case AlignItems::FLEX_START:
                crossPos = crossStart + crossMarginBefore;
                break;
            case AlignItems::CENTER:
                crossPos = crossStart + crossMarginBefore + (availCross - childCross) * 0.5f;
                break;
            case AlignItems::FLEX_END:
                crossPos = crossStart + crossMarginBefore + availCross - childCross;
                break;
            case AlignItems::STRETCH:
                crossPos = crossStart + crossMarginBefore;
                crossFinal = availCross;
                break;
        }

        // Apply to child rect
        if (isRow) {
            w->setRect({mainPos, crossPos, mainSize, crossFinal});
        } else {
            w->setRect({crossPos, mainPos, crossFinal, mainSize});
        }

        // If child is a Box, recurse layout
        if (w->isBox())
            static_cast<Box*>(w)->layout();

        w->onLayout();

        cursor += mainSize + inf.marginAfter + m_gap + spacing;
    }
}

} // namespace nxui
