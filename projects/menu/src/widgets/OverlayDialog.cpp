#include "OverlayDialog.hpp"
#include "settings/SettingsGlassTuning.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cstdint>
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
    float panelX = 640.f - m_panelW * 0.5f;
    float panelY = 360.f - m_panelH * 0.5f;
    return {panelX, panelY, m_panelW, m_panelH};
}


void OverlayDialog::buildWidgetTree() {
    m_mode = DialogMode::Buttons;
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

    m_panelW = kPanelW;
    float contentW = m_panelW - kPanelPadX * 2.f;

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
        setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f));
        setBorderColor(m_theme->panelBorder.withAlpha(0.42f));
        setHighlightColor(m_theme->panelHighlight.withAlpha(0.10f));
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
        auto btn = std::make_shared<ActionButton>();
        btn->setTheme(m_theme);
        btn->setCornerRadius(kButtonRadius);
        btn->setRect({0, 0, btnW, kButtonH});
        btn->setVisualState(1.f, 0.f);
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

bool OverlayDialog::loadUsers(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    m_users.clear();

    AccountUid uids[8] = {};
    s32 count = 0;
    Result rc = accountListAllUsers(uids, 8, &count);
    if (R_FAILED(rc) || count <= 0)
        return false;

    for (int i = 0; i < count; ++i) {
        AccountProfile profile{};
        rc = accountGetProfile(&profile, uids[i]);
        if (R_FAILED(rc))
            continue;

        AccountProfileBase base{};
        AccountUserData userData{};
        rc = accountProfileGet(&profile, &userData, &base);
        if (R_FAILED(rc)) {
            accountProfileClose(&profile);
            continue;
        }

        UserEntry entry;
        entry.uid = uids[i];
        entry.nickname = base.nickname;

        u32 imgSize = 0;
        if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imgSize)) && imgSize > 0) {
            std::vector<std::uint8_t> imgBuf(imgSize);
            u32 realSize = 0;
            if (R_SUCCEEDED(accountProfileLoadImage(&profile, imgBuf.data(), imgSize, &realSize))
                && realSize > 0) {
                entry.icon.loadFromMemory(gpu, ren, imgBuf.data(), realSize);
            }
        }

        accountProfileClose(&profile);
        m_users.push_back(std::move(entry));
    }

    return !m_users.empty();
}

void OverlayDialog::buildUserSelect() {
    m_mode = DialogMode::UserSelect;
    clearChildren();
    m_btnWidgets.clear();
    m_buttonFocus.clear();
    m_titleLabel.reset();
    m_messageLabel.reset();
    m_buttonRow.reset();

    int count = std::max(1, (int)m_users.size());
    float titleH = m_font ? m_font->measure("A").y * 1.1f : 28.f;
    float nameH = m_smallFont ? m_smallFont->measure("Ag").y : (m_font ? m_font->measure("Ag").y : 20.f);
    float totalUsersW = count * kUserAvatarSize + (count - 1) * kUserAvatarGap;

    m_panelW = totalUsersW + kPanelPadX * 2.f;
    m_panelH = kPanelPadY * 2.f + titleH + kUserTitleGap + kUserAvatarSize + 12.f + nameH;

    setAxis(nxui::Axis::COLUMN);
    setRect(panelRect());
    setCornerRadius(kPanelRadius);
    setPadding(0.f);
    setAlignItems(nxui::AlignItems::STRETCH);
    setBackingEnabled(false);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setWireframeEnabled(false);
    setPanelOpacity(0.94f);
    if (m_theme) {
        setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f));
        setBorderColor(m_theme->panelBorder.withAlpha(0.42f));
        setHighlightColor(m_theme->panelHighlight.withAlpha(0.10f));
    }

    m_userAvatarRects.resize(m_users.size());
}

void OverlayDialog::animateButtonFocus(float duration, nxui::EasingFunc easing) {
    int n = (int)m_buttonFocus.size();
    for (int i = 0; i < n; ++i) {
        float target = (i == m_selected) ? 1.f : 0.f;
        m_buttonFocus[(size_t)i].set(target, duration, easing);
    }
}

void OverlayDialog::currentAccessibilityParts(std::string& context,
                                              std::string& position,
                                              std::string& summary,
                                              bool& forceRepeat) const {
    context.clear();
    position.clear();
    summary.clear();
    forceRepeat = false;

    if (!m_active || m_animatingOut)
        return;

    context = m_title;
    if (!m_message.empty())
        context += (context.empty() ? "" : ". ") + m_message;

    if (m_mode == DialogMode::UserSelect) {
        std::string name;
        if (m_selected >= 0 && m_selected < (int)m_users.size())
            name = m_users[(size_t)m_selected].nickname;
        if (name.empty())
            name = "Profil " + std::to_string(m_selected + 1);
        summary = name;
        if (m_accessibilitySpeakPosition && !m_users.empty())
            position = std::to_string(m_selected + 1) + " sur " + std::to_string((int)m_users.size());
        if (m_accessibilitySpeakHints)
            summary += ". Gauche et droite pour changer de profil. A pour valider. B pour annuler.";
        return;
    }

    std::string choice;
    if (m_selected >= 0 && m_selected < (int)m_buttons.size())
        choice = m_buttons[(size_t)m_selected].label;

    if (!choice.empty())
        summary = choice;
    if (m_accessibilitySpeakPosition && !m_buttons.empty())
        position = std::to_string(m_selected + 1) + " sur " + std::to_string((int)m_buttons.size());
    if (m_accessibilitySpeakHints) {
        if (!summary.empty()) summary += ". ";
        summary += "Gauche et droite pour changer. A pour valider. B pour annuler.";
    }
}

std::string OverlayDialog::currentAccessibilitySummary() const {
    std::string context;
    std::string position;
    std::string summary;
    bool forceRepeat = false;
    currentAccessibilityParts(context, position, summary, forceRepeat);

    std::string out = context;
    if (!summary.empty())
        out += (out.empty() ? "" : ". ") + summary;
    if (!position.empty())
        out += (out.empty() ? "" : ". ") + position;
    return out;
}

void OverlayDialog::announceCurrentSelection(bool forceRepeatOverride, bool forceContextOverride) {
    if (m_accessibilityStructuredCb) {
        std::string context;
        std::string position;
        std::string summary;
        bool forceRepeat = false;
        currentAccessibilityParts(context, position, summary, forceRepeat);
        forceRepeat = forceRepeat || forceRepeatOverride;
        m_accessibilityStructuredCb(context, position, summary, forceRepeat, forceContextOverride);
        return;
    }
    if (m_accessibilityCb)
        m_accessibilityCb(currentAccessibilitySummary());
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
    m_touchHitUser = -1;
    m_touchOnSelected = false;
    m_ignoreInitialTouchRelease = true;
    m_pendingInitialAccessibilityFrames = 2;
}

void OverlayDialog::showUserSelect(UserSelectCallback onSelect, CancelCallback onCancel) {
    if (m_users.empty()) {
        if (onSelect) {
            AccountUid uid{};
            onSelect(uid);
        }
        return;
    }

    m_title = nxui::I18n::instance().tr("userselect.title", "Who's playing?");
    m_message.clear();
    m_buttons.clear();
    m_selected = std::clamp(m_selected, 0, (int)m_users.size() - 1);
    m_onUserSelect = std::move(onSelect);
    m_onCancel = std::move(onCancel);

    m_active = true;
    m_animatingOut = false;
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;

    buildUserSelect();

    m_overlayAlpha.setImmediate(0.f);
    m_panelScale.setImmediate(0.92f);
    m_contentReveal.setImmediate(0.f);

    m_overlayAlpha.set(1.f, 0.24f, nxui::Easing::outCubic);
    m_panelScale.set(1.f, 0.28f, nxui::Easing::outCubic);
    m_contentReveal.set(1.f, 0.34f, nxui::Easing::outCubic);

    m_cursor.setCornerRadius(kUserAvatarSize * 0.5f);
    m_cursor.moveTo(userAvatarRect(m_selected).expanded(4.f), kUserAvatarSize * 0.5f, 0.001f);

    setFocusable(true);
    setVisible(true);
    setupUserActions();

    m_touchHitButton = -1;
    m_touchHitUser = -1;
    m_touchOnSelected = false;
    m_ignoreInitialTouchRelease = true;
    announceCurrentSelection();
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
        announceCurrentSelection();
    });

    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (!m_active || m_animatingOut || m_buttons.empty()) return;
        int n = (int)m_buttons.size();
        m_selected = (m_selected + 1) % n;
        animateButtonFocus(0.16f, nxui::Easing::outCubic);
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection();
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

void OverlayDialog::setupUserActions() {
    clearActions();

    auto selectPrevious = [this]() {
        if (!m_active || m_animatingOut || m_users.empty()) return;
        int n = (int)m_users.size();
        m_selected = (m_selected + n - 1) % n;
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection();
    };

    auto selectNext = [this]() {
        if (!m_active || m_animatingOut || m_users.empty()) return;
        int n = (int)m_users.size();
        m_selected = (m_selected + 1) % n;
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection();
    };

    addDirectionAction(nxui::FocusDirection::LEFT, selectPrevious);
    addDirectionAction(nxui::FocusDirection::UP, selectPrevious);
    addDirectionAction(nxui::FocusDirection::RIGHT, selectNext);
    addDirectionAction(nxui::FocusDirection::DOWN, selectNext);

    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (!m_active || m_animatingOut) return;
        activateSelectedUser();
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

void OverlayDialog::activateSelectedUser() {
    if (m_selected < 0 || m_selected >= (int)m_users.size())
        return;
    AccountUid uid = m_users[(size_t)m_selected].uid;
    auto cb = std::move(m_onUserSelect);
    if (m_activateSfxCb) m_activateSfxCb();
    hide();
    if (cb)
        cb(uid);
}

void OverlayDialog::cancel() {
    auto cb = std::move(m_onCancel);
    hide();
    if (cb) cb();
}


void OverlayDialog::handleTouch(nxui::Input& input) {
    if (!m_active || m_animatingOut) return;

    if (m_mode == DialogMode::UserSelect) {
        if (input.touchDown()) {
            if (m_ignoreInitialTouchRelease)
                m_ignoreInitialTouchRelease = false;

            float tx = input.touchX();
            float ty = input.touchY();
            m_touchHitUser = -1;
            m_touchOnSelected = false;
            for (int i = 0; i < (int)m_users.size(); ++i) {
                if (userAvatarRect(i).expanded(12.f).contains(tx, ty)) {
                    m_touchHitUser = i;
                    break;
                }
            }
            if (m_touchHitUser >= 0 && m_touchHitUser == m_selected)
                m_touchOnSelected = true;
        }

        if (input.touchUp()) {
            if (m_ignoreInitialTouchRelease) {
                m_ignoreInitialTouchRelease = false;
                m_touchHitUser = -1;
                m_touchOnSelected = false;
                return;
            }

            float dx = std::abs(input.touchDeltaX());
            float dy = std::abs(input.touchDeltaY());
            if (dx < 20.f && dy < 20.f) {
                if (m_touchHitUser >= 0 && m_touchHitUser < (int)m_users.size()) {
                    if (m_touchOnSelected) {
                        activateSelectedUser();
                    } else {
                        m_selected = m_touchHitUser;
                        if (m_navSfxCb) m_navSfxCb();
                        announceCurrentSelection();
                    }
                } else {
                    float px = input.touchX();
                    float py = input.touchY();
                    if (!scaledRect(rect(), m_panelScale.value()).contains(px, py))
                        cancel();
                }
            }
            m_touchHitUser = -1;
            m_touchOnSelected = false;
        }
        return;
    }

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
                    announceCurrentSelection();
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
    if (m_mode == DialogMode::UserSelect) {
        syncUserCursor();
        return;
    }

    if (m_selected >= 0 && m_selected < (int)m_btnWidgets.size()) {
        nxui::Rect br = scaledRect(m_btnWidgets[m_selected]->rect(), m_panelScale.value());
        m_cursor.moveTo(br.expanded(3.f), kButtonRadius, 0.16f);
    }
    if (m_theme)
        m_cursor.setColor(m_theme->cursorNormal);
    m_cursor.setOpacity(m_overlayAlpha.value());
}

void OverlayDialog::syncUserCursor() {
    if (m_selected >= 0 && m_selected < (int)m_users.size()) {
        nxui::Rect r = userAvatarRect(m_selected);
        m_cursor.moveTo(r.expanded(4.f), r.width * 0.5f, 0.16f);
    }
    if (m_theme)
        m_cursor.setColor(m_theme->cursorNormal);
    m_cursor.setOpacity(m_overlayAlpha.value());
}

void OverlayDialog::syncChildOpacities() {
    if (m_mode == DialogMode::UserSelect) {
        syncUserOpacities();
        return;
    }

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
        m_btnWidgets[i]->setTheme(m_theme);
        m_btnWidgets[i]->setVisualState(alpha, focus);
    }
}

void OverlayDialog::syncUserOpacities() {
    float alpha = m_overlayAlpha.value();
    float sc = m_panelScale.value();
    setScale(sc);
    setPanelOpacity(alpha);
    setRect(panelRect());
}

nxui::Rect OverlayDialog::userAvatarRect(int index) const {
    int n = (int)m_users.size();
    if (n <= 0 || index < 0 || index >= n)
        return {};

    nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    float sc = m_panelScale.value();
    float titleH = m_font ? m_font->measure("A").y * 1.1f : 28.f;
    float totalUsersW = n * kUserAvatarSize + (n - 1) * kUserAvatarGap;
    float startX = panel.x + (panel.width - totalUsersW * sc) * 0.5f;
    float startY = panel.y + (kPanelPadY + titleH + kUserTitleGap) * sc;
    return {
        startX + index * (kUserAvatarSize + kUserAvatarGap) * sc,
        startY,
        kUserAvatarSize * sc,
        kUserAvatarSize * sc,
    };
}

void OverlayDialog::renderUserContent(nxui::Renderer& ren, float alpha) {
    if (alpha <= 0.01f)
        return;

    nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    float sc = m_panelScale.value();
    float reveal = m_contentReveal.value();
    float contentAlpha = alpha * reveal;

    nxui::Color textPrimary = m_theme ? m_theme->textPrimary : nxui::Color::white();
    nxui::Color textSecondary = m_theme ? m_theme->textSecondary : nxui::Color(0.82f, 0.82f, 0.9f, 1.f);

    if (m_font && !m_title.empty()) {
        nxui::Vec2 titleSz = m_font->measure(m_title);
        float tx = panel.x + (panel.width - titleSz.x * sc) * 0.5f;
        float ty = panel.y + kPanelPadY * sc;
        ren.drawText(m_title, {tx, ty}, m_font, textPrimary.withAlpha(contentAlpha), sc);
    }

    nxui::Font* nameFont = m_smallFont ? m_smallFont : m_font;
    m_userAvatarRects.resize(m_users.size());
    for (int i = 0; i < (int)m_users.size(); ++i) {
        nxui::Rect avatar = userAvatarRect(i);
        m_userAvatarRects[(size_t)i] = avatar;

        float selected = (i == m_selected) ? 1.f : 0.f;
        float visualScale = 1.f + 0.08f * selected;
        nxui::Rect drawRect = scaledRect(avatar, visualScale);
        if (m_users[(size_t)i].icon.valid()) {
            ren.drawTextureRounded(&m_users[(size_t)i].icon,
                                   drawRect,
                                   drawRect.width * 0.5f,
                                   nxui::Color::white().withAlpha(contentAlpha));
        } else {
            float r = drawRect.width * 0.5f;
            ren.drawCircle({drawRect.x + r, drawRect.y + r}, r,
                           textSecondary.withAlpha(0.35f * contentAlpha), 32);
        }

        if (nameFont && !m_users[(size_t)i].nickname.empty()) {
            float nameScale = 0.85f * sc;
            nxui::Vec2 nameSz = nameFont->measure(m_users[(size_t)i].nickname);
            float maxW = avatar.width;
            float tx = avatar.x + (maxW - std::min(nameSz.x * nameScale, maxW)) * 0.5f;
            float ty = avatar.bottom() + 8.f * sc;
            ren.pushClipRect({avatar.x, ty - 2.f * sc, maxW, nameSz.y * nameScale + 4.f * sc});
            ren.drawText(m_users[(size_t)i].nickname,
                         {tx, ty},
                         nameFont,
                         (i == m_selected ? textPrimary : textSecondary).withAlpha(contentAlpha),
                         nameScale);
            ren.popClipRect();
        }
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

    if (m_pendingInitialAccessibilityFrames > 0) {
        --m_pendingInitialAccessibilityFrames;
        if (m_pendingInitialAccessibilityFrames == 0)
            announceCurrentSelection(true, true);
    }

    if (m_mode == DialogMode::Buttons) {
        for (auto& c : children())
            c->update(dt);
    }
    m_cursor.update(dt);
}

void OverlayDialog::renderGlassPanel(nxui::Renderer& ren,
                                     const nxui::Theme* theme,
                                     const nxui::Rect& panel,
                                     float radius,
                                     const nxui::Color& base,
                                     const nxui::Color& border,
                                     const nxui::Color& highlight,
                                     float alpha) {
    if (alpha <= 0.01f)
        return;

    nxui::Color glassTint = theme
        ? theme->panelBase.withAlpha(theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f)
        : base.withAlpha(0.95f);
    const auto& tuning = settings::debug::settingsGlassTuning();
    nxui::Rect glassRect = panel.shrunk(std::max(0.0f, tuning.inset));
    float glassRadius = std::max(12.0f, radius - std::max(0.0f, tuning.inset) * 0.5f);

    ren.drawFrostedInset(
        glassRect,
        glassTint,
        border.withAlpha(std::clamp(border.a * 0.90f, 0.14f, 0.34f)),
        highlight.withAlpha(std::clamp(highlight.a * 0.90f, 0.04f, 0.10f)),
        glassRadius,
        alpha);
}

void OverlayDialog::render(nxui::Renderer& ren) {
    if (!m_active && !m_animatingOut) return;

    float alpha = m_overlayAlpha.value();
    if (alpha < 0.01f) return;

    nxui::Rect panel = scaledRect(rect(), scale());
    renderGlassPanel(ren, m_theme, panel, kPanelRadius, m_base, m_border, m_highlight,
                     alpha);

    if (m_mode == DialogMode::UserSelect) {
        renderUserContent(ren, alpha);
    } else {
        for (auto& c : children())
            c->render(ren);
    }

    m_cursor.render(ren);
}
