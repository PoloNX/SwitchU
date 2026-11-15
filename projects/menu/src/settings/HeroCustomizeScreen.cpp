#include "HeroCustomizeScreen.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPanelMargin = 32.f;
constexpr float kPanelRadius = 26.f;
constexpr float kTabWidth = 260.f;
constexpr float kTabRowHeight = 58.f;
constexpr float kTabRailInset = 14.f;
constexpr float kTabCardGap = 8.f;
constexpr float kInnerPad = 30.f;
constexpr float kZoomMin = 1.f;
constexpr float kZoomMax = 2.5f;

std::string ellipsize(nxui::Font* font, const std::string& text, float maxW, float scale) {
    if (!font || text.empty() || maxW <= 1.f)
        return text;
    if (font->measure(text).x * scale <= maxW)
        return text;
    std::string out = text;
    const std::string ellipsis = "...";
    while (out.size() > 1) {
        out.pop_back();
        if (font->measure(out + ellipsis).x * scale <= maxW)
            return out + ellipsis;
    }
    return ellipsis;
}

bool hit(const nxui::Rect& r, float x, float y) {
    return x >= r.x && x <= r.right() && y >= r.y && y <= r.bottom();
}

} // namespace

HeroCustomizeScreen::HeroCustomizeScreen(nxui::GpuDevice& gpu, nxui::Renderer& renderer)
    : m_gpu(gpu), m_renderer(renderer) {
    setRect({0.f, 0.f, m_canvasW, m_canvasH});
    setVisible(false);
    setFocusable(true);
    setFrameworkTouchEnabled(false);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setBaseColor({0.f, 0.f, 0.f, 0.f});
    setBorderColor({0.f, 0.f, 0.f, 0.f});

    auto addNav = [this](nxui::Button button, int dx, int dy) {
        addAction(static_cast<std::uint64_t>(button), [this, dx, dy]() {
            if (m_adjusting) return;
            moveFocus(dx, dy);
        });
    };
    addNav(nxui::Button::DLeft, -1, 0);
    addNav(nxui::Button::LStickL, -1, 0);
    addNav(nxui::Button::DRight, 1, 0);
    addNav(nxui::Button::LStickR, 1, 0);
    addNav(nxui::Button::DUp, 0, -1);
    addNav(nxui::Button::LStickU, 0, -1);
    addNav(nxui::Button::DDown, 0, 1);
    addNav(nxui::Button::LStickD, 0, 1);

    addAction(static_cast<std::uint64_t>(nxui::Button::L), [this]() {
        if (m_adjusting) return;
        setPortraitSlot(false);
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::R), [this]() {
        if (m_adjusting) return;
        setPortraitSlot(true);
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::A), [this]() { activateFocused(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::B), [this]() {
        if (m_adjusting)
            endAdjustKeep(); // keep crop changes; leave adjust mode
        else
            cancelAndClose();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::Y), [this]() {
        if (m_adjusting)
            resetActiveSlot();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::X), [this]() {
        if (m_adjusting)
            toggleFit();
    });
}

void HeroCustomizeScreen::setCanvasSize(float width, float height) {
    m_canvasW = std::max(1.f, width);
    m_canvasH = std::max(1.f, height);
    setRect({0.f, 0.f, m_canvasW, m_canvasH});
}

void HeroCustomizeScreen::show(std::uint64_t titleId, const std::string& title) {
    m_titleId = titleId;
    m_title = title;
    m_portraitSlot = m_canvasH > m_canvasW + 0.5f;
    m_landscape = SteamGridDbManager::heroPresentation(titleId, false);
    m_portrait = SteamGridDbManager::heroPresentation(titleId, true);
    m_adjusting = false;
    m_focus = FocusItem::Tabs; // start on Landscape / Portrait rail
    m_sourceReady = false;
    m_source = nxui::Texture{};
    loadSourceTexture();
    m_active = true;
    setVisible(true);
}

void HeroCustomizeScreen::hide() {
    m_active = false;
    m_adjusting = false;
    setVisible(false);
    m_source = nxui::Texture{};
    m_sourceReady = false;
}

HeroCustomizeScreen::Presentation& HeroCustomizeScreen::activeSlot() {
    return m_portraitSlot ? m_portrait : m_landscape;
}

const HeroCustomizeScreen::Presentation& HeroCustomizeScreen::activeSlot() const {
    return m_portraitSlot ? m_portrait : m_landscape;
}

void HeroCustomizeScreen::loadSourceTexture() {
    m_sourceReady = false;
    if (m_titleId == 0) return;
    m_source = nxui::Texture{};
    m_sourceReady = m_source.loadFromFile(
        m_gpu, m_renderer, SteamGridDbManager::heroPath(m_titleId), 960);
}

void HeroCustomizeScreen::setPortraitSlot(bool portrait) {
    if (m_portraitSlot == portrait) return;
    if (m_adjusting)
        endAdjustKeep();
    m_portraitSlot = portrait;
}

void HeroCustomizeScreen::enterAdjust() {
    m_adjusting = true;
    m_focus = FocusItem::Preview;
    m_touchPanActive = false;
    m_pinchActive = false;
}

void HeroCustomizeScreen::endAdjustKeep() {
    m_adjusting = false;
    m_touchPanActive = false;
    m_pinchActive = false;
    m_focus = FocusItem::Preview;
}

void HeroCustomizeScreen::toggleFit() {
    activeSlot().fill = !activeSlot().fill;
}

void HeroCustomizeScreen::resetActiveSlot() {
    activeSlot() = Presentation{};
}

void HeroCustomizeScreen::activateFocused() {
    if (m_adjusting) {
        endAdjustKeep();
        return;
    }
    switch (m_focus) {
        case FocusItem::Tabs:
            // Enter the pane (same idea as Theme Shop), don't flip Landscape/Portrait.
            m_focus = FocusItem::Preview;
            break;
        case FocusItem::Preview:
            enterAdjust();
            break;
        case FocusItem::Save:
            saveAndClose();
            break;
        case FocusItem::Cancel:
            cancelAndClose();
            break;
    }
}

void HeroCustomizeScreen::moveFocus(int dx, int dy) {
    if (m_adjusting) return;

    const bool topRail = isPortraitCanvas();

    // Landscape left rail: Up/Down switch tabs; Right → pane; Left from pane → tabs.
    if (!topRail) {
        if (m_focus == FocusItem::Tabs) {
            if (dx > 0) {
                m_focus = FocusItem::Preview;
                return;
            }
            if (dy < 0) setPortraitSlot(false); // Up → Landscape
            if (dy > 0) setPortraitSlot(true);  // Down → Portrait
            return;
        }

        if (dx < 0) {
            // Only the leftmost pane controls jump back to tabs.
            if (m_focus == FocusItem::Preview || m_focus == FocusItem::Save)
                m_focus = FocusItem::Tabs;
            else if (m_focus == FocusItem::Cancel)
                m_focus = FocusItem::Save;
            return;
        }
        if (dx > 0) {
            if (m_focus == FocusItem::Preview)
                m_focus = FocusItem::Save;
            else if (m_focus == FocusItem::Save)
                m_focus = FocusItem::Cancel;
            return;
        }
        if (dy > 0) {
            if (m_focus == FocusItem::Preview)
                m_focus = FocusItem::Save;
            else if (m_focus == FocusItem::Save)
                m_focus = FocusItem::Cancel;
            return;
        }
        if (dy < 0) {
            if (m_focus == FocusItem::Cancel)
                m_focus = FocusItem::Save;
            else if (m_focus == FocusItem::Save)
                m_focus = FocusItem::Preview;
            return;
        }
        return;
    }

    // Portrait top rail (rough; polish later when testing Flip-Grip).
    if (m_focus == FocusItem::Tabs) {
        if (dx < 0) { setPortraitSlot(false); return; }
        if (dx > 0) { setPortraitSlot(true); return; }
        if (dy > 0) { m_focus = FocusItem::Preview; return; }
        return;
    }
    if (dx != 0) {
        if (m_focus == FocusItem::Preview) {
            m_focus = dx < 0 ? FocusItem::Tabs : FocusItem::Save;
            return;
        }
        if (m_focus == FocusItem::Save) {
            m_focus = dx < 0 ? FocusItem::Preview : FocusItem::Cancel;
            return;
        }
        if (m_focus == FocusItem::Cancel) {
            if (dx < 0) m_focus = FocusItem::Save;
            return;
        }
    }
    if (dy < 0) {
        if (m_focus == FocusItem::Preview)
            m_focus = FocusItem::Tabs;
        else
            m_focus = FocusItem::Preview;
    } else if (dy > 0) {
        if (m_focus == FocusItem::Tabs)
            m_focus = FocusItem::Preview;
        else if (m_focus == FocusItem::Preview)
            m_focus = FocusItem::Save;
        else if (m_focus == FocusItem::Save)
            m_focus = FocusItem::Cancel;
    }
}

void HeroCustomizeScreen::saveAndClose() {
    if (m_adjusting)
        endAdjustKeep();
    if (m_titleId != 0) {
        SteamGridDbManager::saveHeroPresentation(m_titleId, false, m_landscape);
        SteamGridDbManager::saveHeroPresentation(m_titleId, true, m_portrait);
        SteamGridDbManager::clearHeroDecodeCaches(m_titleId);
        if (m_savedCb)
            m_savedCb(m_titleId);
    }
    hide();
    if (m_closedCb)
        m_closedCb();
}

void HeroCustomizeScreen::cancelAndClose() {
    // Discard unsaved edits (reload from disk next open).
    m_adjusting = false;
    hide();
    if (m_closedCb)
        m_closedCb();
}

nxui::Rect HeroCustomizeScreen::panelRect() const {
    // Match Theme Shop / Settings landscape panel (margin 32 all around).
    const bool portrait = m_canvasH > m_canvasW + 0.5f;
    if (portrait) {
        constexpr float kMarginX = 16.f;
        constexpr float kMarginY = 28.f;
        return {kMarginX, kMarginY,
                m_canvasW - 2.f * kMarginX,
                m_canvasH - 2.f * kMarginY};
    }
    return {kPanelMargin, kPanelMargin,
            m_canvasW - 2.f * kPanelMargin,
            m_canvasH - 2.f * kPanelMargin};
}

nxui::Rect HeroCustomizeScreen::tabsRect(const nxui::Rect& panel) const {
    const bool topRail = m_canvasH > m_canvasW + 0.5f;
    const float headerH = 86.f;
    if (topRail) {
        return {panel.x + kInnerPad * 0.4f,
                panel.y + headerH,
                panel.width - kInnerPad * 0.8f,
                kTabRowHeight + kTabRailInset};
    }
    return {panel.x + 18.f,
            panel.y + headerH,
            kTabWidth,
            panel.height - headerH - 24.f};
}

nxui::Rect HeroCustomizeScreen::contentRect(const nxui::Rect& panel) const {
    const bool topRail = m_canvasH > m_canvasW + 0.5f;
    const float headerH = 86.f;
    const nxui::Rect tabs = tabsRect(panel);
    if (topRail) {
        const float top = tabs.bottom() + 12.f;
        return {panel.x + kInnerPad,
                top,
                panel.width - kInnerPad * 2.f,
                panel.bottom() - 24.f - top};
    }
    return {tabs.right() + 18.f,
            panel.y + headerH,
            panel.right() - 24.f - (tabs.right() + 18.f),
            panel.height - headerH - 24.f};
}

nxui::Rect HeroCustomizeScreen::previewFrame(const nxui::Rect& content) const {
    // Save/Cancel band. On Flip-Grip, Fill joins that row while adjusting so
    // the legend can sit alone under the preview.
    const float buttonBand = 72.f;
    const float adjustBand = !m_adjusting ? 28.f
        : (isPortraitCanvas() ? 36.f : 48.f);
    const float availH = std::max(120.f, content.height - buttonBand - adjustBand);
    const float availW = content.width;
    const float aspect = m_portraitSlot ? (9.f / 16.f) : (16.f / 9.f);
    float w = availW;
    float h = w / aspect;
    if (h > availH) {
        h = availH;
        w = h * aspect;
    }
    const float x = content.x + (content.width - w) * 0.5f;
    // Landscape preview on a tall canvas looks better vertically centered;
    // portrait previews already fill most of the band.
    const float y = (isPortraitCanvas() && !m_portraitSlot)
        ? content.y + (availH - h) * 0.5f
        : content.y;
    return {x, y, w, h};
}

nxui::Rect HeroCustomizeScreen::cropButtonRect(const nxui::Rect& content,
                                               const nxui::Rect& frame) const {
    const float btnW = 120.f;
    const float btnH = 40.f;
    if (isPortraitCanvas()) {
        // Flip-Grip: same row / height as Save/Cancel, bottom-left of the pane.
        constexpr float kActionH = 48.f;
        return {content.x, content.bottom() - kActionH, btnW, kActionH};
    }
    // Landscape HOME: pin Fill under the preview (frame for wide slot,
    // content left for skinny portrait slot).
    const float x = m_portraitSlot ? content.x : frame.x;
    return {x, frame.bottom() + 8.f, btnW, btnH};
}

nxui::Rect HeroCustomizeScreen::saveButtonRect(const nxui::Rect& content) const {
    const float btnW = 150.f;
    const float btnH = 48.f;
    const float y = content.bottom() - btnH;
    return {content.right() - btnW * 2.f - 12.f, y, btnW, btnH};
}

nxui::Rect HeroCustomizeScreen::cancelButtonRect(const nxui::Rect& content) const {
    const float btnW = 150.f;
    const float btnH = 48.f;
    const float y = content.bottom() - btnH;
    return {content.right() - btnW, y, btnW, btnH};
}

nxui::Rect HeroCustomizeScreen::focusRectFor(FocusItem item) const {
    const nxui::Rect panel = panelRect();
    const nxui::Rect tabs = tabsRect(panel);
    const nxui::Rect content = contentRect(panel);
    const bool topRail = m_canvasH > m_canvasW + 0.5f;
    const float tabH = kTabRowHeight - kTabCardGap;
    switch (item) {
        case FocusItem::Tabs: {
            if (topRail) {
                const float gap = 8.f;
                const float tabW = (tabs.width - kTabRailInset * 2.f - gap) * 0.5f;
                const float x = tabs.x + kTabRailInset
                    + (m_portraitSlot ? (tabW + gap) : 0.f);
                return {x, tabs.y + kTabRailInset, tabW, tabH};
            }
            const float y = tabs.y + kTabRailInset
                + (m_portraitSlot ? (tabH + kTabCardGap) : 0.f);
            return {tabs.x + kTabRailInset, y,
                    tabs.width - kTabRailInset * 2.f, tabH};
        }
        case FocusItem::Preview:
            return previewFrame(content).expanded(4.f);
        case FocusItem::Save:
            return saveButtonRect(content).expanded(2.f);
        case FocusItem::Cancel:
            return cancelButtonRect(content).expanded(2.f);
    }
    return content;
}

nxui::Rect HeroCustomizeScreen::mappedRect(const nxui::Texture& texture,
                                           const nxui::Rect& area,
                                           bool fill, float focusX, float focusY,
                                           float zoom) {
    if (!texture.valid()) return area;
    focusX = std::clamp(focusX, 0.f, 1.f);
    focusY = std::clamp(focusY, 0.f, 1.f);
    zoom = std::clamp(zoom, kZoomMin, kZoomMax);
    const float scaleX = area.width / texture.width();
    const float scaleY = area.height / texture.height();
    const float scale = (fill ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY)) * zoom;
    const float w = texture.width() * scale;
    const float h = texture.height() * scale;
    return {area.x + (area.width - w) * focusX, area.y + (area.height - h) * focusY, w, h};
}

void HeroCustomizeScreen::drawTab(nxui::Renderer& ren, const nxui::Rect& r,
                                  const std::string& label, bool selected,
                                  bool focused) const {
    if (!m_theme) return;
    const auto bg = selected
        ? m_theme->panelBase.withAlpha(0.98f)
        : m_theme->panelBorder.withAlpha(0.18f);
    ren.drawRoundedRect(r, bg, 16.f);
    ren.drawRoundedRectOutline(r, m_theme->panelBorder.withAlpha(selected ? 0.40f : 0.22f),
                               16.f, 1.f);
    if (selected) {
        ren.drawRoundedRect({r.x + 8.f, r.y + 10.f, 4.f, r.height - 20.f},
                            m_theme->cursorNormal.withAlpha(0.95f), 2.f);
    }
    if (focused) {
        ren.drawRoundedRectOutline(r.expanded(2.f), m_theme->cursorNormal, 18.f, 2.2f);
    }
    if (m_font) {
        const float scale = 0.82f;
        const float tw = m_font->measure(label).x * scale;
        ren.drawText(label,
                     {r.x + (r.width - tw) * 0.5f + (selected ? 4.f : 0.f),
                      r.y + (r.height - 18.f) * 0.5f},
                     m_font, m_theme->textPrimary, scale);
    }
}

void HeroCustomizeScreen::drawActionButton(nxui::Renderer& ren, const nxui::Rect& r,
                                           const std::string& label, bool focused,
                                           bool primary) const {
    if (!m_theme) return;
    const auto bg = primary
        ? m_theme->cursorNormal.withAlpha(focused ? 0.95f : 0.78f)
        : m_theme->panelBorder.withAlpha(focused ? 0.42f : 0.24f);
    ren.drawRoundedRect(r, bg, 14.f);
    if (focused)
        ren.drawRoundedRectOutline(r.expanded(2.f), m_theme->cursorNormal, 16.f, 2.f);
    if (m_font) {
        const float scale = 0.8f;
        const float tw = m_font->measure(label).x * scale;
        ren.drawText(label,
                     {r.x + (r.width - tw) * 0.5f, r.y + (r.height - 18.f) * 0.5f},
                     m_font, m_theme->textPrimary, scale);
    }
}

void HeroCustomizeScreen::handleFrame(nxui::Input& input) {
    if (!m_active) return;

    const nxui::Rect panel = panelRect();
    const nxui::Rect tabs = tabsRect(panel);
    const nxui::Rect content = contentRect(panel);
    const nxui::Rect frame = previewFrame(content);
    const nxui::Rect crop = cropButtonRect(content, frame);

    if (m_adjusting) {
        constexpr float kDead = 0.22f;
        const float lx = input.leftStickX();
        const float ly = input.leftStickY();
        if (std::abs(lx) > kDead || std::abs(ly) > kDead) {
            auto& slot = activeSlot();
            // ~1.4 focus units/sec at full stick (was 0.55 — felt sluggish).
            const float speed = 1.4f * (1.f / 60.f);
            slot.focusX = std::clamp(slot.focusX + lx * speed, 0.f, 1.f);
            slot.focusY = std::clamp(slot.focusY - ly * speed, 0.f, 1.f);
        }
        // Right stick Y zooms (up = in).
        const float ry = input.rightStickY();
        if (std::abs(ry) > kDead) {
            auto& slot = activeSlot();
            // ~2.2 zoom units/sec at full stick (was 0.9).
            const float zoomSpeed = 2.2f * (1.f / 60.f);
            slot.setActiveZoom(slot.activeZoom() + ry * zoomSpeed);
        }
        // D-pad nudges while adjusting (held for continuous crawl).
        auto& slot = activeSlot();
        constexpr float n = 0.028f;
        if (input.isHeld(nxui::Button::DLeft))  slot.focusX = std::clamp(slot.focusX - n, 0.f, 1.f);
        if (input.isHeld(nxui::Button::DRight)) slot.focusX = std::clamp(slot.focusX + n, 0.f, 1.f);
        if (input.isHeld(nxui::Button::DUp))    slot.focusY = std::clamp(slot.focusY - n, 0.f, 1.f);
        if (input.isHeld(nxui::Button::DDown))  slot.focusY = std::clamp(slot.focusY + n, 0.f, 1.f);

        // Touch: pinch zoom (2 fingers) or drag pan (1 finger on the preview).
        const int touchCount = input.touchCount();
        if (touchCount >= 2) {
            const float x0 = input.touchPointX(0);
            const float y0 = input.touchPointY(0);
            const float x1 = input.touchPointX(1);
            const float y1 = input.touchPointY(1);
            const float dist = std::hypot(x1 - x0, y1 - y0);
            m_touchPanActive = false;
            if (!m_pinchActive || m_pinchLastDist < 1.f) {
                m_pinchActive = true;
                m_pinchLastDist = dist;
            } else if (dist > 1.f) {
                slot.setActiveZoom(slot.activeZoom() * (dist / m_pinchLastDist));
                m_pinchLastDist = dist;
            }
        } else {
            m_pinchActive = false;
            m_pinchLastDist = 0.f;
            if (touchCount == 1 && input.isTouching()) {
                const float tx = input.touchX();
                const float ty = input.touchY();
                if (input.touchDown()) {
                    if (hit(frame, tx, ty) && !hit(crop, tx, ty)) {
                        m_touchPanActive = true;
                        m_touchLastX = tx;
                        m_touchLastY = ty;
                    } else {
                        m_touchPanActive = false;
                    }
                } else if (m_touchPanActive) {
                    const float dx = (tx - m_touchLastX) / std::max(1.f, frame.width);
                    const float dy = (ty - m_touchLastY) / std::max(1.f, frame.height);
                    // Drag the image with the finger (focus moves opposite).
                    slot.focusX = std::clamp(slot.focusX - dx, 0.f, 1.f);
                    slot.focusY = std::clamp(slot.focusY - dy, 0.f, 1.f);
                    m_touchLastX = tx;
                    m_touchLastY = ty;
                }
            } else {
                m_touchPanActive = false;
            }
        }
    } else {
        m_touchPanActive = false;
        m_pinchActive = false;
    }

    if (!input.touchDown()) return;
    // Don't steal UI taps while a pan/pinch gesture owns the contact.
    if (m_touchPanActive || m_pinchActive) return;

    const float tx = input.touchX();
    const float ty = input.touchY();
    const bool topRail = m_canvasH > m_canvasW + 0.5f;
    const float tabH = kTabRowHeight - kTabCardGap;

    if (topRail) {
        const float gap = 8.f;
        const float tabW = (tabs.width - kTabRailInset * 2.f - gap) * 0.5f;
        const nxui::Rect land{tabs.x + kTabRailInset, tabs.y + kTabRailInset, tabW, tabH};
        const nxui::Rect port{land.right() + gap, land.y, tabW, tabH};
        if (hit(land, tx, ty)) { setPortraitSlot(false); m_focus = FocusItem::Tabs; return; }
        if (hit(port, tx, ty)) { setPortraitSlot(true); m_focus = FocusItem::Tabs; return; }
    } else {
        const nxui::Rect land{tabs.x + kTabRailInset, tabs.y + kTabRailInset,
                              tabs.width - kTabRailInset * 2.f, tabH};
        const nxui::Rect port{land.x, land.bottom() + kTabCardGap, land.width, tabH};
        if (hit(land, tx, ty)) { setPortraitSlot(false); m_focus = FocusItem::Tabs; return; }
        if (hit(port, tx, ty)) { setPortraitSlot(true); m_focus = FocusItem::Tabs; return; }
    }

    if (hit(saveButtonRect(content), tx, ty)) {
        if (!m_adjusting) saveAndClose();
        return;
    }
    if (hit(cancelButtonRect(content), tx, ty)) {
        cancelAndClose(); // discards unsaved edits
        return;
    }

    if (m_adjusting && hit(crop, tx, ty)) {
        toggleFit();
        return;
    }
    if (!m_adjusting && hit(frame, tx, ty)) {
        m_focus = FocusItem::Preview;
        enterAdjust();
    }
}

void HeroCustomizeScreen::onUpdate(float dt) {
    if (!m_active) return;
    m_uiTime += dt;
}

void HeroCustomizeScreen::onRender(nxui::Renderer& renderer) {
    if (!m_active || !m_theme) return;
    auto& i18n = nxui::I18n::instance();

    renderer.drawRect(rect(), nxui::Color(0.f, 0.f, 0.f, 0.55f));

    const nxui::Rect panel = panelRect();
    renderer.drawRoundedRect({panel.x, panel.y + 7.f, panel.width, panel.height},
                             nxui::Color(0.f, 0.f, 0.f, 0.20f), kPanelRadius);
    renderer.drawRoundedRect(panel, m_theme->panelBase.withAlpha(0.98f), kPanelRadius);
    renderer.drawRoundedRectOutline(panel.shrunk(1.f),
                                    m_theme->panelBorder.withAlpha(0.34f),
                                    kPanelRadius - 1.f, 1.4f);

    if (m_font) {
        const std::string heading = i18n.tr("game.steamgriddb.customize_hero", "Customize Hero");
        renderer.drawText(heading, {panel.x + kInnerPad, panel.y + 28.f}, m_font,
                          m_theme->textPrimary, 1.0f);
        if (!m_title.empty()) {
            const std::string title = ellipsize(m_font, m_title, panel.width - kInnerPad * 2.f, 0.72f);
            renderer.drawText(title, {panel.x + kInnerPad, panel.y + 58.f}, m_font,
                              m_theme->textSecondary, 0.72f);
        }
    }

    const nxui::Rect tabs = tabsRect(panel);
    const bool topRail = m_canvasH > m_canvasW + 0.5f;
    const float tabH = kTabRowHeight - kTabCardGap;
    // Tab rail card backdrop (Theme Shop style).
    renderer.drawRoundedRect(tabs, m_theme->panelBase.withAlpha(
        m_theme->mode == nxui::ThemeMode::Dark ? 0.94f : 0.96f), topRail ? 20.f : 24.f);
    renderer.drawRoundedRectOutline(tabs, m_theme->panelBorder.withAlpha(0.30f),
                                    topRail ? 20.f : 24.f, 1.f);

    const std::string landLabel = i18n.tr("game.steamgriddb.hero_slot_landscape", "Landscape");
    const std::string portLabel = i18n.tr("game.steamgriddb.hero_slot_portrait", "Portrait");
    if (topRail) {
        const float gap = 8.f;
        const float tabW = (tabs.width - kTabRailInset * 2.f - gap) * 0.5f;
        const nxui::Rect land{tabs.x + kTabRailInset, tabs.y + kTabRailInset, tabW, tabH};
        const nxui::Rect port{land.right() + gap, land.y, tabW, tabH};
        drawTab(renderer, land, landLabel, !m_portraitSlot,
                m_focus == FocusItem::Tabs && !m_portraitSlot && !m_adjusting);
        drawTab(renderer, port, portLabel, m_portraitSlot,
                m_focus == FocusItem::Tabs && m_portraitSlot && !m_adjusting);
    } else {
        const nxui::Rect land{tabs.x + kTabRailInset, tabs.y + kTabRailInset,
                              tabs.width - kTabRailInset * 2.f, tabH};
        const nxui::Rect port{land.x, land.bottom() + kTabCardGap, land.width, tabH};
        drawTab(renderer, land, landLabel, !m_portraitSlot,
                m_focus == FocusItem::Tabs && !m_portraitSlot && !m_adjusting);
        drawTab(renderer, port, portLabel, m_portraitSlot,
                m_focus == FocusItem::Tabs && m_portraitSlot && !m_adjusting);
    }

    const nxui::Rect content = contentRect(panel);
    const nxui::Rect frame = previewFrame(content);
    renderer.drawRoundedRect(frame, nxui::Color(0.f, 0.f, 0.f, 0.55f), 12.f);
    renderer.pushClipRect(frame);
    const auto& slot = activeSlot();
    if (m_sourceReady && m_source.valid()) {
        renderer.drawTexture(&m_source,
                             mappedRect(m_source, frame, slot.fill, slot.focusX, slot.focusY,
                                        slot.activeZoom()),
                             nxui::Color::white());
    } else if (m_smallFont) {
        const std::string missing = i18n.tr("game.steamgriddb.hero_missing", "No hero image");
        const float tw = m_smallFont->measure(missing).x * 0.85f;
        renderer.drawText(missing,
                          {frame.x + (frame.width - tw) * 0.5f, frame.y + frame.height * 0.5f - 8.f},
                          m_smallFont, m_theme->textSecondary, 0.85f);
    }
    renderer.popClipRect();

    const bool previewFocused = (m_focus == FocusItem::Preview && !m_adjusting)
        || m_adjusting;
    if (previewFocused) {
        const float pulse = m_adjusting
            ? (0.75f + 0.25f * std::sin(m_uiTime * 5.f))
            : 1.f;
        renderer.drawRoundedRectOutline(frame.expanded(3.f),
                                        m_theme->cursorNormal.withAlpha(pulse), 14.f, 2.4f);
    } else {
        renderer.drawRoundedRectOutline(frame, m_theme->panelBorder.withAlpha(0.55f), 12.f, 1.5f);
    }

    if (m_adjusting) {
        const std::string fitLabel = slot.fill
            ? i18n.tr("game.steamgriddb.hero_fit_fill", "Fill")
            : i18n.tr("game.steamgriddb.hero_fit_fit", "Fit");
        const nxui::Rect crop = cropButtonRect(content, frame);
        drawActionButton(renderer, crop, fitLabel, true, false);
        if (m_smallFont) {
            const std::string hint = i18n.tr(
                "game.steamgriddb.customize_hero_adjust_hints",
                "Drag/Stick pan  ·  Pinch/R-Stick zoom  ·  X crop  ·  Y reset  ·  A/B done");
            constexpr float hintScale = 0.86f;
            const float tw = m_smallFont->measure(hint).x * hintScale;
            const float th = std::max(16.f, m_smallFont->measure(hint).y * hintScale);
            if (isPortraitCanvas()) {
                // Flip-Grip: Fill is on the Save row — legend gets the full
                // width under the preview so the long hint isn't crushed.
                const float hintX = content.x + std::max(0.f, (content.width - tw) * 0.5f);
                renderer.drawText(hint,
                                  {hintX, frame.bottom() + 10.f},
                                  m_smallFont, m_theme->textSecondary, hintScale);
            } else {
                // Landscape HOME: Fill under preview; legend in the remaining span.
                const float legendLeft = crop.right() + 16.f;
                const float legendSpan = std::max(0.f, content.right() - legendLeft);
                const float hintX = legendLeft + std::max(0.f, (legendSpan - tw) * 0.5f);
                renderer.drawText(hint,
                                  {hintX, crop.y + (crop.height - th) * 0.5f},
                                  m_smallFont, m_theme->textSecondary, hintScale);
            }
        }
    } else if (m_focus == FocusItem::Preview && m_smallFont) {
        const std::string hint = i18n.tr(
            "game.steamgriddb.customize_hero_select_hint",
            "A to adjust image");
        constexpr float selectScale = 0.88f;
        const float tw = m_smallFont->measure(hint).x * selectScale;
        renderer.drawText(hint,
                          {frame.x + (frame.width - tw) * 0.5f, frame.bottom() + 8.f},
                          m_smallFont, m_theme->textSecondary, selectScale);
    }

    drawActionButton(renderer, saveButtonRect(content),
                     i18n.tr("button.save", "Save"),
                     m_focus == FocusItem::Save && !m_adjusting, true);
    drawActionButton(renderer, cancelButtonRect(content),
                     i18n.tr("button.cancel", "Cancel"),
                     m_focus == FocusItem::Cancel && !m_adjusting, false);
}
