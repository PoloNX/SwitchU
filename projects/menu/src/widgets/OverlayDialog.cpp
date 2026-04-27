#include "OverlayDialog.hpp"
#include "ActionButtonStyle.hpp"
#include "settings/SettingsGlassTuning.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <utility>
#include <cmath>

namespace {

static nxui::Rect scaledRect(const nxui::Rect& rect, float scale) {
    nxui::Rect scaled = rect;
    if (scale >= 0.999f) {
        return scaled;
    }

    float width = scaled.width * scale;
    float height = scaled.height * scale;
    scaled.x += (scaled.width - width) * 0.5f;
    scaled.y += (scaled.height - height) * 0.5f;
    scaled.width = width;
    scaled.height = height;
    return scaled;
}

} // namespace


OverlayDialog::OverlayDialog() {
    setFrameworkTouchEnabled(false);
    setVisible(false);
    m_cursor.setBorderWidth(2.6f);
}


nxui::Rect OverlayDialog::panelRect() const {
    float panelX = 640.f - kPanelW * 0.5f;
    float panelY = 360.f - m_panelH * 0.5f;
    return {panelX, panelY, kPanelW, m_panelH};
}


void OverlayDialog::buildWidgetTree() {
    clearChildren();
    m_btnWidgets.clear();
    m_buttonFocus.clear();
    m_titleLabel.reset();
    m_messageLabel.reset();
    m_buttonRow.reset();

    nxui::Font* titleFont = m_font;
    nxui::Font* bodyFont  = m_smallFont ? m_smallFont : m_font;

    nxui::Color textPrimary   = m_theme ? m_theme->textPrimary   : nxui::Color::white();
    nxui::Color textSecondary = m_theme ? m_theme->textSecondary
                                        : nxui::Color(0.82f, 0.82f, 0.9f, 1.f);

    float contentW = kPanelW - kPanelPadX * 2.f;

    float titleH = (titleFont && !m_title.empty())
                       ? titleFont->measure(m_title).y : 0.f;
    float msgH   = 0.f;
    if (bodyFont && !m_message.empty()) {
        nxui::Label probe(m_message, bodyFont);
        probe.setScale(0.96f);
        probe.setMultiline(true);
        msgH = probe.measureWrappedText(contentW).y;
    }

    m_panelH = kPanelPadY
             + (titleH > 0.f ? titleH + kTitleMsgGap : 0.f)
             + (msgH   > 0.f ? msgH   + kMsgBtnGap   : 0.f)
             + kButtonH
             + kPanelPadY;

    setAxis(nxui::Axis::COLUMN);
    setRect(panelRect());
    setCornerRadius(kPanelRadius);
    setPadding(kPanelPadY, kPanelPadX, kPanelPadY, kPanelPadX);
    setAlignItems(nxui::AlignItems::STRETCH);
    setBackingEnabled(false);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setWireframeEnabled(false);
    setPanelOpacity(0.94f);
    if (m_theme) {
        setBaseColor(m_theme->panelBase.withAlpha(std::clamp(m_theme->panelBase.a * 0.82f, 0.18f, 0.32f)));
        setBorderColor(m_theme->panelBorder.withAlpha(std::clamp(m_theme->panelBorder.a * 0.95f, 0.14f, 0.36f)));
        setHighlightColor(m_theme->panelHighlight.withAlpha(std::clamp(m_theme->panelHighlight.a * 0.70f, 0.03f, 0.10f)));
    }

    if (titleFont && !m_title.empty()) {
        m_titleLabel = std::make_shared<nxui::Label>(m_title, titleFont);
        m_titleLabel->setTextColor(textPrimary);
        m_titleLabel->setHAlign(nxui::Label::HAlign::Center);
        m_titleLabel->setVAlign(nxui::Label::VAlign::Center);
        m_titleLabel->setRect({0, 0, contentW, titleH});
        m_titleLabel->setMarginBottom(kTitleMsgGap);
        addChild(m_titleLabel);
    }

    if (bodyFont && !m_message.empty()) {
        m_messageLabel = std::make_shared<nxui::Label>(m_message, bodyFont);
        m_messageLabel->setTextColor(textSecondary);
        m_messageLabel->setScale(0.96f);
        m_messageLabel->setMultiline(true);
        m_messageLabel->setLineSpacing(1.12f);
        m_messageLabel->setHAlign(nxui::Label::HAlign::Center);
        m_messageLabel->setVAlign(nxui::Label::VAlign::Top);
        m_messageLabel->setRect({0, 0, contentW, msgH});
        m_messageLabel->setMarginBottom(kMsgBtnGap);
        addChild(m_messageLabel);
    }

    int  btnCount = std::max(1, (int)m_buttons.size());
    float btnW    = (contentW - kButtonGap * (btnCount - 1)) / (float)btnCount;

    m_buttonRow = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    m_buttonRow->setRect({0, 0, contentW, kButtonH});
    m_buttonRow->setGap(kButtonGap);
    m_buttonRow->setAlignItems(nxui::AlignItems::STRETCH);
    m_buttonRow->setWireframeEnabled(false);

    for (int i = 0; i < btnCount; ++i) {
        auto btn = std::make_shared<nxui::GlassBox>(nxui::Axis::ROW);
        switchu::ui::prepareActionButton(*btn, kButtonRadius);
        btn->setCornerRadius(kButtonRadius);
        btn->setRect({0, 0, btnW, kButtonH});
        switchu::ui::applyActionButtonStyle(*btn, m_theme, 1.f, 0.f);
        btn->setWireframeEnabled(false);
        btn->setGrow(1.f);

        auto lbl = std::make_shared<nxui::Label>(m_buttons[i].label);
        lbl->setFont(bodyFont ? bodyFont : titleFont);
        lbl->setTextColor(textPrimary);
        lbl->setScale(0.94f);
        lbl->setHAlign(nxui::Label::HAlign::Center);
        lbl->setVAlign(nxui::Label::VAlign::Center);
        lbl->setRect({0, 0, btnW, kButtonH});
        lbl->setGrow(1.f);
        btn->addChild(lbl);

        m_buttonRow->addChild(btn);
        m_btnWidgets.push_back(btn);
    }

    m_buttonFocus.resize((size_t)btnCount);
    for (int i = 0; i < btnCount; ++i)
        m_buttonFocus[(size_t)i].setImmediate(i == m_selected ? 1.f : 0.f);

    addChild(m_buttonRow);

    layout();
}

void OverlayDialog::animateButtonFocus(float duration, nxui::EasingFunc easing) {
    int n = (int)m_buttonFocus.size();
    for (int i = 0; i < n; ++i) {
        float target = (i == m_selected) ? 1.f : 0.f;
        m_buttonFocus[(size_t)i].set(target, duration, easing);
    }
}


void OverlayDialog::show(const std::string& title,
                         const std::string& message,
                         std::vector<ButtonDef> buttons,
                         int initialSelected,
                         CancelCallback onCancel) {
    m_title   = title;
    m_message = message;
    m_buttons = std::move(buttons);
    if (m_buttons.empty())
        m_buttons.push_back({"OK", {}, true});

    m_selected = std::clamp(initialSelected, 0, (int)m_buttons.size() - 1);
    m_onCancel = std::move(onCancel);

    m_active       = true;
    m_animatingOut = false;
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;

    buildWidgetTree();

    m_overlayAlpha.setImmediate(0.f);
    m_panelScale.setImmediate(0.92f);
    m_contentReveal.setImmediate(0.f);

    m_overlayAlpha.set(1.f,  0.24f, nxui::Easing::outCubic);
    m_panelScale.set(1.f,    0.28f, nxui::Easing::outCubic);
    m_contentReveal.set(1.f, 0.34f, nxui::Easing::outCubic);

    if (m_selected < (int)m_btnWidgets.size()) {
        nxui::Rect br = scaledRect(m_btnWidgets[m_selected]->rect(), m_panelScale.value());
        m_cursor.setCornerRadius(kButtonRadius);
        m_cursor.moveTo(br.expanded(3.f), kButtonRadius, 0.001f);
    }

    animateButtonFocus(0.001f, nxui::Easing::linear);

    setFocusable(true);
    setVisible(true);
    setupActions();

    m_touchHitButton  = -1;
    m_touchOnSelected = false;
    m_ignoreInitialTouchRelease = true;
}

void OverlayDialog::hide() {
    if (!m_active || m_animatingOut) return;

    if (m_closeSfxCb) m_closeSfxCb();

    m_animatingOut = true;
    m_overlayAlpha.set(0.f,   0.20f, nxui::Easing::outCubic);
    m_panelScale.set(0.96f,   0.20f, nxui::Easing::outCubic);
    m_contentReveal.set(0.f,  0.16f, nxui::Easing::outCubic);

    setFocusable(false);
    clearActions();
}


void OverlayDialog::setupActions() {
    clearActions();

    addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        if (!m_active || m_animatingOut || m_buttons.empty()) return;
        int n = (int)m_buttons.size();
        m_selected = (m_selected + n - 1) % n;
        animateButtonFocus(0.16f, nxui::Easing::outCubic);
        if (m_navSfxCb) m_navSfxCb();
    });

    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (!m_active || m_animatingOut || m_buttons.empty()) return;
        int n = (int)m_buttons.size();
        m_selected = (m_selected + 1) % n;
        animateButtonFocus(0.16f, nxui::Easing::outCubic);
        if (m_navSfxCb) m_navSfxCb();
    });

    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (!m_active || m_animatingOut) return;
        activateSelected();
    });

    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (!m_active || m_animatingOut) return;
        cancel();
    });
}

void OverlayDialog::activateSelected() {
    if (m_selected < 0 || m_selected >= (int)m_buttons.size()) return;

    auto btn = m_buttons[m_selected];
    if (!btn.closeOnPress && m_activateSfxCb) m_activateSfxCb();
    if (btn.closeOnPress) hide();
    if (btn.onPress) btn.onPress();
}

void OverlayDialog::cancel() {
    auto cb = std::move(m_onCancel);
    hide();
    if (cb) cb();
}


void OverlayDialog::handleTouch(nxui::Input& input) {
    if (!m_active || m_animatingOut) return;

    if (input.touchDown()) {
        if (m_ignoreInitialTouchRelease)
            m_ignoreInitialTouchRelease = false;

        float tx = input.touchX();
        float ty = input.touchY();
        m_touchHitButton  = -1;
        m_touchOnSelected = false;
        for (int i = 0; i < (int)m_btnWidgets.size(); ++i) {
            nxui::Rect buttonRect = scaledRect(m_btnWidgets[i]->rect(), m_panelScale.value()).expanded(10.f);
            if (buttonRect.contains(tx, ty)) {
                m_touchHitButton = i;
                break;
            }
        }
        if (m_touchHitButton >= 0 && m_touchHitButton == m_selected)
            m_touchOnSelected = true;
    }

    if (input.touchUp()) {
        if (m_ignoreInitialTouchRelease) {
            m_ignoreInitialTouchRelease = false;
            m_touchHitButton  = -1;
            m_touchOnSelected = false;
            return;
        }

        float dx = std::abs(input.touchDeltaX());
        float dy = std::abs(input.touchDeltaY());
        if (dx < 20.f && dy < 20.f) {
            if (m_touchHitButton >= 0 && m_touchHitButton < (int)m_buttons.size()) {
                if (m_touchOnSelected) {
                    activateSelected();
                } else {
                    m_selected = m_touchHitButton;
                    animateButtonFocus(0.16f, nxui::Easing::outCubic);
                    if (m_navSfxCb) m_navSfxCb();
                }
            } else {
                float px = input.touchX();
                float py = input.touchY();
                if (!scaledRect(rect(), m_panelScale.value()).contains(px, py))
                    cancel();
            }
        }
        m_touchHitButton  = -1;
        m_touchOnSelected = false;
    }
}


void OverlayDialog::syncCursor() {
    if (m_selected >= 0 && m_selected < (int)m_btnWidgets.size()) {
        nxui::Rect br = scaledRect(m_btnWidgets[m_selected]->rect(), m_panelScale.value());
        m_cursor.moveTo(br.expanded(3.f), kButtonRadius, 0.16f);
    }
    if (m_theme)
        m_cursor.setColor(m_theme->cursorNormal);
    m_cursor.setOpacity(m_overlayAlpha.value());
}

void OverlayDialog::syncChildOpacities() {
    float alpha  = m_overlayAlpha.value();
    float reveal = m_contentReveal.value();
    float sc     = m_panelScale.value();

    setScale(sc);
    setPanelOpacity(alpha);
    setRect(panelRect());

    int idx = 0;
    auto applyStagger = [&](nxui::Widget* w) {
        if (!w) return;
        float delay = std::min(0.5f, idx * 0.08f);
        float local = std::clamp((reveal - delay) / 0.25f, 0.f, 1.f);
        w->setOpacity(alpha * local);
        ++idx;
    };

    if (m_titleLabel)   applyStagger(m_titleLabel.get());
    if (m_messageLabel) applyStagger(m_messageLabel.get());
    for (auto& btn : m_btnWidgets) {
        applyStagger(btn.get());
        for (auto& c : btn->children())
            c->setOpacity(btn->opacity());
    }

    for (int i = 0; i < (int)m_btnWidgets.size(); ++i) {
        float focus = (i < (int)m_buttonFocus.size()) ? m_buttonFocus[(size_t)i].value() : 0.f;
        focus = std::clamp(focus, 0.f, 1.f);
        switchu::ui::applyActionButtonStyle(*m_btnWidgets[i], m_theme, alpha, focus);
    }
}


void OverlayDialog::update(float dt) {
    if (!m_active && !m_animatingOut) return;

    if (m_animatingOut && m_overlayAlpha.value() < 0.01f) {
        m_active       = false;
        m_animatingOut = false;
        setVisible(false);
        return;
    }

    syncChildOpacities();
    syncCursor();

    for (auto& c : children())
        c->update(dt);
    m_cursor.update(dt);
}


void OverlayDialog::render(nxui::Renderer& ren) {
    if (!m_active && !m_animatingOut) return;

    float alpha = m_overlayAlpha.value();
    if (alpha < 0.01f) return;

    nxui::Rect panel = scaledRect(rect(), scale());
    const auto& tuning = settings::debug::settingsGlassTuning();
    bool needsBackdropRefresh = !m_backdropCacheValid
        || std::abs(m_cachedPreBlurRadius - tuning.preBlurRadius) > 0.001f
        || m_cachedBlurIterations != tuning.blurIterations;

    if (needsBackdropRefresh) {
        ren.captureToOffscreen(false);
        if (tuning.blurIterations > 0 && tuning.preBlurRadius > 0.001f) {
            ren.applyBlur(tuning.preBlurRadius, tuning.blurIterations);
        }
        ren.copyOffscreen(0, kBackdropCacheTarget);
        m_backdropCacheValid = true;
        m_cachedPreBlurRadius = tuning.preBlurRadius;
        m_cachedBlurIterations = tuning.blurIterations;
    }

    nxui::LiquidGlassSettings savedGlass = ren.liquidGlassSettings();
    auto& glass = ren.liquidGlassSettings();
    glass.refractionIntensity = std::clamp(tuning.refractionIntensity, 0.0f, 1.5f);
    glass.blurIntensity = std::max(0.0f, tuning.shaderBlurIntensity);
    glass.noiseIntensity = 0.0f;
    glass.glowIntensity = std::max(0.0f, tuning.glowIntensity);
    glass.saturation = std::max(0.0f, tuning.saturation);
    glass.opacityMultiplier = 1.0f;
    glass.roughness = std::max(0.0f, tuning.roughness);
    glass.powerFactor = std::max(1.001f, tuning.powerFactor);

    nxui::Color glassTint = m_theme
        ? m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark
            ? std::clamp(tuning.tintAlphaDark, 0.0f, 1.0f)
            : std::clamp(tuning.tintAlphaLight, 0.0f, 1.0f))
        : m_base.withAlpha(0.14f);
    nxui::Rect glassRect = panel.shrunk(std::max(0.0f, tuning.inset));
    float glassRadius = std::max(12.0f, kPanelRadius - std::max(0.0f, tuning.inset) * 0.5f);

    ren.drawLiquidGlass(kBackdropCacheTarget,
                        glassRect,
                        glassRadius,
                        glassTint,
                        alpha,
                        std::clamp(tuning.shade, 0.0f, 1.0f));
    ren.drawRoundedRectOutline(glassRect,
                               m_border.withAlpha(std::clamp(m_border.a * 0.90f, 0.14f, 0.34f) * alpha),
                               glassRadius,
                               1.2f);
    ren.drawRoundedRectOutline(glassRect.shrunk(1.5f),
                               m_highlight.withAlpha(std::clamp(m_highlight.a * 0.90f, 0.04f, 0.10f) * alpha),
                               std::max(0.0f, glassRadius - 1.5f),
                               1.0f);
    ren.liquidGlassSettings() = savedGlass;

    for (auto& c : children())
        c->render(ren);

    m_cursor.render(ren);
}
