#include "ControllerTestScreen.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float kStickSelectThreshold = 0.55f;
constexpr float kStickSelectDelay = 0.45f;
constexpr float kDeadzone = 0.12f;

void drawPanel(nxui::Renderer& ren, const nxui::Theme& theme, const nxui::Rect& rect, float alpha) {
    ren.drawRoundedRect(rect, theme.panelBase.withAlpha(0.93f * alpha), 22.f);
    ren.drawRoundedRectOutline(rect, theme.panelBorder.withAlpha(0.32f * alpha), 22.f, 1.2f);
    ren.drawRoundedRectOutline(rect.shrunk(1.5f), theme.panelHighlight.withAlpha(0.08f * alpha), 20.5f, 1.f);
}
}

ControllerTestScreen::ControllerTestScreen() {
    setFrameworkTouchEnabled(false);
    setVisible(false);
    setFocusable(true);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
}

void ControllerTestScreen::show() {
    if (m_active)
        return;
    m_active = true;
    m_animatingOut = false;
    m_activeStick = ActiveStick::None;
    m_candidateStick = ActiveStick::None;
    m_candidateTime = 0.f;
    m_stickX = m_stickY = 0.f;
    m_recordingTouch = false;
    m_touchFullscreen = false;
    m_bHoldTime = 0.f;
    m_touchTrace.clear();
    m_touchFullscreenAnim.setImmediate(0.f);
    m_alpha.setImmediate(0.f);
    m_alpha.set(1.f, 0.22f, nxui::Easing::outCubic);
    m_scale.setImmediate(0.92f);
    m_scale.set(1.f, 0.24f, nxui::Easing::outCubic);
    setVisible(true);
    if (m_input)
        m_input->setVirtualPointerShortcutsEnabled(false);
    setupActions();
    if (m_accessibilityCb)
        m_accessibilityCb(nxui::I18n::instance().tr("controller_test.opened", "Controller test. Tilt a stick briefly to select it. Hold B for one second or press HOME to close."));
}

void ControllerTestScreen::hide() {
    if (!m_active || m_animatingOut)
        return;
    m_animatingOut = true;
    if (m_input)
        m_input->setVirtualPointerShortcutsEnabled(true);
    if (m_closeSfxCb) m_closeSfxCb();
    m_alpha.set(0.f, 0.18f, nxui::Easing::outCubic);
    m_scale.set(0.96f, 0.18f, nxui::Easing::outCubic);
    clearActions();
}

void ControllerTestScreen::setupActions() {
    clearActions();
    addAction(static_cast<std::uint64_t>(nxui::Button::X), [this]() {
        if (m_touchFullscreen)
            exitTouchFullscreen();
    });
}

nxui::Rect ControllerTestScreen::panelRect(float scale) const {
    nxui::Rect rect{32.f, 32.f, 1216.f, 656.f};
    float w = rect.width * scale;
    float h = rect.height * scale;
    rect.x += (rect.width - w) * 0.5f;
    rect.y += (rect.height - h) * 0.5f;
    rect.width = w;
    rect.height = h;
    return rect;
}

nxui::Rect ControllerTestScreen::touchPanelRect(float scale) const {
    nxui::Rect p = panelRect(scale);
    return {p.x + 516.f * scale, p.y + 360.f * scale, 668.f * scale, 264.f * scale};
}

void ControllerTestScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_animatingOut)
        return;

    nxui::Rect zone{0.f, 0.f, 1280.f, 720.f};
    if (!m_touchFullscreen) {
        zone = touchPanelRect(m_scale.value()).shrunk(18.f * m_scale.value());
        zone.y += 42.f * m_scale.value();
        zone.height -= 42.f * m_scale.value();
    }

    if (input.touchDown() && zone.contains(input.touchX(), input.touchY())) {
        if (!m_touchFullscreen) {
            enterTouchFullscreen();
            zone = {0.f, 0.f, 1280.f, 720.f};
        }
        m_touchTrace.clear();
        recordTouchPoint(input.touchX(), input.touchY(), zone);
        m_recordingTouch = true;
    }
    if (input.isTouching() && m_recordingTouch) {
        recordTouchPoint(input.touchX(), input.touchY(), zone);
    }
    if (input.touchUp())
        m_recordingTouch = false;
}

void ControllerTestScreen::enterTouchFullscreen() {
    m_touchFullscreen = true;
    m_touchFullscreenAnim.set(1.f, 0.24f, nxui::Easing::outCubic);
    if (m_accessibilityCb)
        m_accessibilityCb(nxui::I18n::instance().tr(
            "controller_test.touch_fullscreen_opened",
            "Full-screen touch test. Press X to return. Hold B for one second or press HOME to close."));
}

void ControllerTestScreen::exitTouchFullscreen() {
    if (!m_touchFullscreen)
        return;
    m_touchFullscreen = false;
    m_recordingTouch = false;
    m_touchFullscreenAnim.set(0.f, 0.20f, nxui::Easing::outCubic);
}

void ControllerTestScreen::recordTouchPoint(float x, float y, const nxui::Rect& surface) {
    if (surface.width <= 0.f || surface.height <= 0.f)
        return;
    nxui::Vec2 p{
        std::clamp((x - surface.x) / surface.width, 0.f, 1.f),
        std::clamp((y - surface.y) / surface.height, 0.f, 1.f),
    };
    const float minNormalizedDistance = 2.f / std::max(surface.width, surface.height);
    if (m_touchTrace.empty() || (p - m_touchTrace.back()).lengthSq() >= minNormalizedDistance * minNormalizedDistance)
        m_touchTrace.push_back(p);
    if (m_touchTrace.size() > 512)
        m_touchTrace.erase(m_touchTrace.begin(), m_touchTrace.begin() + 128);
}

void ControllerTestScreen::drawTouchTrace(nxui::Renderer& ren, const nxui::Rect& surface,
                                          float alpha, float thickness) {
    auto mapPoint = [&surface](const nxui::Vec2& p) {
        return nxui::Vec2{surface.x + p.x * surface.width, surface.y + p.y * surface.height};
    };
    for (std::size_t i = 1; i < m_touchTrace.size(); ++i)
        ren.drawLine(mapPoint(m_touchTrace[i - 1]), mapPoint(m_touchTrace[i]),
                     m_theme->cursorNormal.withAlpha(0.88f * alpha), thickness);
    if (!m_touchTrace.empty())
        ren.drawCircle(mapPoint(m_touchTrace.back()), thickness + 2.f,
                       m_theme->cursorNormal.withAlpha(alpha), 20);
}

void ControllerTestScreen::update(float dt) {
    if (!isActive())
        return;
    m_alpha.update(dt);
    m_scale.update(dt);
    m_touchFullscreenAnim.update(dt);
    if (m_animatingOut && m_alpha.value() <= 0.01f) {
        m_active = false;
        m_animatingOut = false;
        setVisible(false);
        if (m_closedCb) m_closedCb();
        return;
    }
    if (!m_active || !m_input)
        return;

    if (m_input->isHeld(nxui::Button::B)) {
        m_bHoldTime += dt;
        if (m_bHoldTime >= 1.f) {
            m_bHoldTime = 0.f;
            hide();
            return;
        }
    } else {
        m_bHoldTime = 0.f;
    }

    const float lm = std::hypot(m_input->leftStickX(), m_input->leftStickY());
    const float rm = std::hypot(m_input->rightStickX(), m_input->rightStickY());
    ActiveStick candidate = ActiveStick::None;
    if (lm >= kStickSelectThreshold || rm >= kStickSelectThreshold)
        candidate = lm >= rm ? ActiveStick::Left : ActiveStick::Right;
    if (candidate == ActiveStick::None) {
        m_candidateStick = ActiveStick::None;
        m_candidateTime = 0.f;
    } else if (candidate != m_candidateStick) {
        m_candidateStick = candidate;
        m_candidateTime = 0.f;
    } else {
        m_candidateTime += dt;
        if (m_candidateTime >= kStickSelectDelay && m_activeStick != candidate) {
            m_activeStick = candidate;
            m_candidateTime = 0.f;
            if (m_accessibilityCb) {
                m_accessibilityCb(candidate == ActiveStick::Left
                    ? nxui::I18n::instance().tr("controller_test.left_selected", "Left stick selected")
                    : nxui::I18n::instance().tr("controller_test.right_selected", "Right stick selected"));
            }
        }
    }

    float x = 0.f, y = 0.f;
    if (m_activeStick == ActiveStick::Left) {
        x = m_input->leftStickX(); y = m_input->leftStickY();
    } else if (m_activeStick == ActiveStick::Right) {
        x = m_input->rightStickX(); y = m_input->rightStickY();
    }
    // A diagnostic view must display the hardware sample exactly. Filtering
    // made fast circular movements visibly lag behind and miss the edge.
    m_stickX = x;
    m_stickY = y;
}

std::string ControllerTestScreen::directionLabel(float x, float y) const {
    auto& i18n = nxui::I18n::instance();
    if (std::hypot(x, y) <= kDeadzone)
        return i18n.tr("controller_test.center", "Center");
    const bool horizontal = std::abs(x) > 0.38f;
    const bool vertical = std::abs(y) > 0.38f;
    if (vertical && horizontal)
        return (y > 0 ? i18n.tr("controller_test.up", "Up") : i18n.tr("controller_test.down", "Down"))
            + std::string(" · ")
            + (x > 0 ? i18n.tr("controller_test.right", "Right") : i18n.tr("controller_test.left", "Left"));
    if (horizontal) return x > 0 ? i18n.tr("controller_test.right", "Right") : i18n.tr("controller_test.left", "Left");
    return y > 0 ? i18n.tr("controller_test.up", "Up") : i18n.tr("controller_test.down", "Down");
}

void ControllerTestScreen::drawStickPanel(nxui::Renderer& ren, const nxui::Rect& r, float alpha) {
    drawPanel(ren, *m_theme, r, alpha);
    auto& i18n = nxui::I18n::instance();
    ren.drawText(i18n.tr("controller_test.joysticks", "Joysticks"), {r.x + 24.f, r.y + 20.f},
                 m_font, m_theme->textPrimary.withAlpha(alpha), 0.92f);
    std::string active = m_activeStick == ActiveStick::Left
        ? i18n.tr("controller_test.left_stick", "Left stick")
        : m_activeStick == ActiveStick::Right
            ? i18n.tr("controller_test.right_stick", "Right stick")
            : i18n.tr("controller_test.tilt_to_select", "Tilt a stick briefly to select it");
    ren.drawText(active, {r.x + 24.f, r.y + 62.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(alpha), 0.76f);

    nxui::Vec2 center{r.x + r.width * 0.5f, r.y + 214.f};
    const float outer = 91.f;
    ren.drawCircle(center, outer, m_theme->panelBorder.withAlpha(0.24f * alpha), 48);
    ren.drawCircle(center, outer * kDeadzone, m_theme->cursorNormal.withAlpha(0.13f * alpha), 32);
    ren.drawCircle({center.x + m_stickX * (outer - 19.f), center.y - m_stickY * (outer - 19.f)},
                   19.f, m_theme->cursorNormal.withAlpha(0.92f * alpha), 32);

    char values[80]{};
    std::snprintf(values, sizeof(values), "X %+0.3f     Y %+0.3f", m_stickX, m_stickY);
    ren.drawText(values, {r.x + 24.f, r.bottom() - 72.f}, m_smallFont,
                 m_theme->textPrimary.withAlpha(alpha), 0.78f);
    std::string direction = i18n.tr("controller_test.direction", "Direction") + ": " + directionLabel(m_stickX, m_stickY);
    ren.drawText(direction, {r.x + 24.f, r.bottom() - 42.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(alpha), 0.74f);
}

void ControllerTestScreen::drawButtonsPanel(nxui::Renderer& ren, const nxui::Rect& r, float alpha) {
    drawPanel(ren, *m_theme, r, alpha);
    ren.drawText(nxui::I18n::instance().tr("controller_test.buttons", "Buttons"),
                 {r.x + 24.f, r.y + 20.f}, m_font, m_theme->textPrimary.withAlpha(alpha), 0.92f);
    static constexpr ButtonVisual buttons[] = {
        {nxui::Button::A,"A"},{nxui::Button::B,"B"},{nxui::Button::X,"X"},{nxui::Button::Y,"Y"},
        {nxui::Button::L,"L"},{nxui::Button::R,"R"},{nxui::Button::ZL,"ZL"},{nxui::Button::ZR,"ZR"},
        {nxui::Button::Plus,"+"},{nxui::Button::Minus,"−"},{nxui::Button::LStick,"L3"},{nxui::Button::RStick,"R3"},
        {nxui::Button::DUp,"↑"},{nxui::Button::DDown,"↓"},{nxui::Button::DLeft,"←"},{nxui::Button::DRight,"→"},
    };
    constexpr float gapX = 10.f;
    constexpr float gapY = 8.f;
    constexpr float cellH = 34.f;
    const float cellW = (r.width - 48.f - 3.f * gapX) / 4.f;
    for (int i = 0; i < 16; ++i) {
        int row = i / 4, col = i % 4;
        nxui::Rect cell{r.x + 24.f + col * (cellW + gapX),
                        r.y + 64.f + row * (cellH + gapY), cellW, cellH};
        const bool held = m_input && m_input->isHeld(buttons[i].button);
        ren.drawRoundedRect(cell, (held ? m_theme->cursorNormal : m_theme->panelBorder)
                                  .withAlpha((held ? 0.88f : 0.18f) * alpha), 13.f);
        ren.drawRoundedRectOutline(cell, (held ? m_theme->textPrimary : m_theme->panelBorder)
                                         .withAlpha((held ? 0.62f : 0.30f) * alpha), 13.f, 1.f);
        nxui::Vec2 sz = m_smallFont->measure(buttons[i].label);
        ren.drawText(buttons[i].label, {cell.x + (cell.width - sz.x * 0.68f) * 0.5f, cell.y + 6.f},
                     m_smallFont, (held ? m_theme->textPrimary : m_theme->textSecondary).withAlpha(alpha), 0.68f);
    }
}

void ControllerTestScreen::drawTouchPanel(nxui::Renderer& ren, const nxui::Rect& r, float alpha) {
    drawPanel(ren, *m_theme, r, alpha);
    ren.drawText(nxui::I18n::instance().tr("controller_test.touchscreen", "Touch screen"),
                 {r.x + 24.f, r.y + 18.f}, m_font, m_theme->textPrimary.withAlpha(alpha), 0.88f);
    nxui::Rect zone = r.shrunk(18.f);
    zone.y += 42.f; zone.height -= 42.f;
    ren.drawRoundedRect(zone, m_theme->background.withAlpha(0.52f * alpha), 15.f);
    ren.drawRoundedRectOutline(zone, m_theme->panelBorder.withAlpha(0.34f * alpha), 15.f, 1.f);
    drawTouchTrace(ren, zone, alpha, 5.f);
    if (m_touchTrace.empty())
        ren.drawText(nxui::I18n::instance().tr("controller_test.touch_hint", "Touch or click and drag here"),
                     {zone.x + 18.f, zone.y + 18.f}, m_smallFont, m_theme->textSecondary.withAlpha(0.72f * alpha), 0.74f);
}

void ControllerTestScreen::drawFullscreenTouch(nxui::Renderer& ren, float alpha) {
    const float t = std::clamp(m_touchFullscreenAnim.value(), 0.f, 1.f);
    if (t <= 0.01f)
        return;
    const float a = alpha * t;
    nxui::Rect screen{0.f, 0.f, 1280.f, 720.f};
    ren.drawRect(screen, m_theme->background.withAlpha(0.98f * a));

    // Sparse guide grid makes edge and corner coverage easy to inspect.
    const nxui::Color guide = m_theme->panelBorder.withAlpha(0.18f * a);
    for (int x = 160; x < 1280; x += 160)
        ren.drawLine({(float)x, 0.f}, {(float)x, 720.f}, guide, 1.f);
    for (int y = 120; y < 720; y += 120)
        ren.drawLine({0.f, (float)y}, {1280.f, (float)y}, guide, 1.f);

    drawTouchTrace(ren, screen, a, 6.f);
    nxui::Rect hint{24.f, 20.f, 700.f, 48.f};
    ren.drawRoundedRect(hint, m_theme->panelBase.withAlpha(0.95f * a), 18.f);
    ren.drawText(nxui::I18n::instance().tr(
                     "controller_test.touch_fullscreen_hint",
                     "Touch test · X: return · Hold B 1 s or HOME: close"),
                 {hint.x + 18.f, hint.y + 12.f}, m_smallFont,
                 m_theme->textPrimary.withAlpha(a), 0.74f);
}

void ControllerTestScreen::render(nxui::Renderer& ren) {
    if (!isActive() || !m_theme || !m_font || !m_smallFont)
        return;
    const float alpha = m_alpha.value();
    const float scale = m_scale.value();
    nxui::Rect p = panelRect(scale);
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, nxui::Color::black().withAlpha(0.12f * alpha));
    ren.drawFrostedInset(p, m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f),
                        m_theme->panelBorder.withAlpha(0.32f), m_theme->panelHighlight.withAlpha(0.10f), 26.f, alpha);
    ren.drawText(nxui::I18n::instance().tr("controller_test.title", "Controller Test"),
                 {p.x + 30.f, p.y + 24.f}, m_font, m_theme->textPrimary.withAlpha(alpha), 1.f);
    const std::string closeHint = nxui::I18n::instance().tr(
        "controller_test.close_hint", "Hold B 1 s or press HOME to close");
    nxui::Vec2 closeSize = m_smallFont->measure(closeHint);
    ren.drawText(closeHint,
                 {p.right() - 30.f - closeSize.x * 0.68f, p.y + 30.f},
                 m_smallFont, m_theme->textSecondary.withAlpha(alpha), 0.68f);
    nxui::Rect stick{p.x + 30.f * scale, p.y + 82.f * scale,
                     456.f * scale, 542.f * scale};
    nxui::Rect buttons{p.x + 516.f * scale, p.y + 82.f * scale,
                       668.f * scale, 254.f * scale};
    nxui::Rect touch{p.x + 516.f * scale, p.y + 360.f * scale,
                     668.f * scale, 264.f * scale};
    drawStickPanel(ren, stick, alpha);
    drawButtonsPanel(ren, buttons, alpha);
    drawTouchPanel(ren, touch, alpha);
    drawFullscreenTouch(ren, alpha);
}
