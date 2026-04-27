#include "UserSelectScreen.hpp"
#include "core/AudioManager.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Input.hpp>
#include <switch.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

UserSelectScreen::UserSelectScreen() {
    setFrameworkTouchEnabled(false);
}


bool UserSelectScreen::loadUsers(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    m_users.clear();

    AccountUid uids[8] = {};
    s32 count = 0;
    Result rc = accountListAllUsers(uids, 8, &count);
    DebugLog::log("[UserSelect] accountListAllUsers rc=0x%X count=%d", rc, count);
    if (R_FAILED(rc) || count <= 0) return false;

    for (int i = 0; i < count; ++i) {
        AccountProfile profile;
        rc = accountGetProfile(&profile, uids[i]);
        if (R_FAILED(rc)) continue;

        AccountProfileBase base;
        AccountUserData userData;
        rc = accountProfileGet(&profile, &userData, &base);
        if (R_FAILED(rc)) {
            accountProfileClose(&profile);
            continue;
        }

        UserEntry entry;
        entry.uid = uids[i];
        entry.nickname = base.nickname;

        u32 imgSize = 0;
        u32 realSize = 0;
        rc = accountProfileGetImageSize(&profile, &imgSize);
        if (R_SUCCEEDED(rc) && imgSize > 0) {
            std::vector<uint8_t> imgBuf(imgSize);
            rc = accountProfileLoadImage(&profile, imgBuf.data(), imgSize, &realSize);
            if (R_SUCCEEDED(rc) && realSize > 0) {
                entry.icon.loadFromMemory(gpu, ren, imgBuf.data(), realSize);
            }
        }

        accountProfileClose(&profile);
        m_users.push_back(std::move(entry));
    }

    DebugLog::log("[UserSelect] loaded %d users", (int)m_users.size());
    return !m_users.empty();
}

void UserSelectScreen::show(SelectCallback onSelect, CancelCallback onCancel) {
    if (m_users.empty()) {
        if (onSelect) {
            AccountUid uid = {};
            onSelect(uid);
        }
        return;
    }

    m_onSelect = std::move(onSelect);
    m_onCancel = std::move(onCancel);
    m_active   = true;
    m_animatingOut = false;
    m_ignoreInitialTouchRelease = true;
    m_overlayAlpha.setImmediate(0.f);
    m_panelScale.setImmediate(0.85f);
    m_overlayAlpha.set(1.f, 0.2f, nxui::Easing::outExpo);
    m_panelScale.set(1.f, 0.25f, nxui::Easing::outExpo);
    if (m_audio) m_audio->playSfx(Sfx::ModalShow);

    setFocusable(true);
    clearActions();

    int n = (int)m_users.size();

    addDirectionAction(nxui::FocusDirection::LEFT, [this, n]() {
        if (!m_active || m_animatingOut || n == 0) return;
        m_selected = (m_selected - 1 + n) % n;
        if (m_audio) m_audio->playSfx(Sfx::Navigate);
    });

    addDirectionAction(nxui::FocusDirection::RIGHT, [this, n]() {
        if (!m_active || m_animatingOut || n == 0) return;
        m_selected = (m_selected + 1) % n;
        if (m_audio) m_audio->playSfx(Sfx::Navigate);
    });

    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (!m_active || m_animatingOut || m_users.empty()) return;
        if (m_audio) m_audio->playSfx(Sfx::Activate);
        AccountUid uid = m_users[m_selected].uid;
        auto cb = std::move(m_onSelect);
        hide();
        if (cb) cb(uid);
    });

    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (!m_active || m_animatingOut) return;
        if (m_audio) m_audio->playSfx(Sfx::ModalHide);
        auto cb = std::move(m_onCancel);
        hide();
        if (cb) cb();
    });
}

void UserSelectScreen::hide() {
    m_animatingOut = true;
    m_overlayAlpha.set(0.f, 0.15f, nxui::Easing::outExpo);
    m_panelScale.set(0.9f, 0.15f, nxui::Easing::outExpo);

    setFocusable(false);
    clearActions();
}

void UserSelectScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_animatingOut) return;

    int n = (int)m_users.size();
    if (n == 0) return;

    if (input.touchDown()) {
        if (m_ignoreInitialTouchRelease)
            m_ignoreInitialTouchRelease = false;

        float tx = input.touchX();
        float ty = input.touchY();
        m_touchHitAvatar = -1;
        m_touchOnSelected = false;
        for (int i = 0; i < (int)m_avatarRects.size(); ++i) {
            if (m_avatarRects[i].contains(tx, ty)) {
                m_touchHitAvatar = i;
                break;
            }
        }
        if (m_touchHitAvatar >= 0 && m_touchHitAvatar == m_selected) {
            m_touchOnSelected = true;
        }
    }

    if (input.touchUp()) {
        if (m_ignoreInitialTouchRelease) {
            m_ignoreInitialTouchRelease = false;
            m_touchHitAvatar = -1;
            m_touchOnSelected = false;
            return;
        }

        float dx = std::abs(input.touchDeltaX());
        float dy = std::abs(input.touchDeltaY());
        if (dx < 20.f && dy < 20.f) {
            if (m_touchHitAvatar >= 0 && m_touchHitAvatar < n) {
                if (m_touchOnSelected) {
                    if (m_audio) m_audio->playSfx(Sfx::Activate);
                    AccountUid uid = m_users[m_selected].uid;
                    auto cb = std::move(m_onSelect);
                    hide();
                    if (cb) cb(uid);
                } else {
                    m_selected = m_touchHitAvatar;
                    if (m_audio) m_audio->playSfx(Sfx::Navigate);
                }
            } else {
                if (m_audio) m_audio->playSfx(Sfx::ModalHide);
                auto cb = std::move(m_onCancel);
                hide();
                if (cb) cb();
            }
        }
        m_touchHitAvatar = -1;
    }
}

void UserSelectScreen::onUpdate(float dt) {
    if (m_animatingOut && m_overlayAlpha.value() < 0.01f) {
        m_active = false;
        m_animatingOut = false;
    }

    m_panel.setScale(m_panelScale.value());
    m_panel.setOpacity(m_overlayAlpha.value());
    m_titlePanel.setScale(m_panelScale.value());
    m_titlePanel.setOpacity(m_overlayAlpha.value());
    m_cursor.setOpacity(m_overlayAlpha.value());
    m_cursor.update(dt);
}

void UserSelectScreen::onRender(nxui::Renderer& ren) {
    if (!m_active) return;

    float alpha = m_overlayAlpha.value();
    float sc    = m_panelScale.value();
    int n       = (int)m_users.size();
    if (n == 0 || alpha < 0.01f) return;

    ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, 0.55f * alpha));

    float avatarSize = 96.f;
    float gap        = 32.f;
    float itemW      = avatarSize;
    float totalW     = n * itemW + (n - 1) * gap;
    float panelPadX  = 48.f;
    float panelPadY  = 36.f;
    float nameH      = m_font ? m_font->measure("Ag").y : 20.f;
    float contentH   = avatarSize + 12.f + nameH;
    float panelW     = totalW + panelPadX * 2.f;
    float panelH     = contentH + panelPadY * 2.f;

    float titleH = m_font ? m_font->measure("A").y * 1.1f : 28.f;
    float totalH = titleH + 16.f + panelH;

    float cx = 1280.f * 0.5f;
    float cy = 720.f  * 0.5f;

    float scaledTotalH = totalH * sc;

    if (m_font) {
        const std::string title = nxui::I18n::instance().tr("userselect.title", "Who's playing?");
        nxui::Vec2 titleSz = m_font->measure(title);
        float tPadX = 20.f;
        float tPadY = 8.f;
        float tPillW = titleSz.x * sc + tPadX * 2.f;
        float tPillH = titleSz.y * sc + tPadY * 2.f;
        float tPillX = cx - tPillW * 0.5f;
        float tPillY = cy - scaledTotalH * 0.5f - tPadY;
        m_titlePanel.setCornerRadius(tPillH * 0.5f);
        m_titlePanel.setRect({tPillX, tPillY, tPillW, tPillH});
        m_titlePanel.render(ren);

        float tx = cx - titleSz.x * 0.5f * sc;
        float ty = tPillY + tPadY;
        ren.drawText(title, {tx, ty}, m_font,
                     m_textPrimary.withAlpha(alpha), sc);
    }

    float panelX = cx - panelW * 0.5f;
    float panelY = cy - totalH * 0.5f + (titleH + 16.f);
    m_panel.setRect({panelX, panelY, panelW, panelH});
    m_panel.render(ren);

    float scaledPanelW = panelW * sc;
    float scaledPanelH = panelH * sc;
    float rpX = cx - scaledPanelW * 0.5f;
    float rpY = panelY + (panelH - scaledPanelH) * 0.5f;

    float startX = rpX + panelPadX * sc;
    float startY = rpY + panelPadY * sc;
    float scaledAvatarSize = avatarSize * sc;
    float scaledGap        = gap * sc;
    float scaledItemW      = itemW * sc;

    m_avatarRects.resize(n);

    for (int i = 0; i < n; ++i) {
        float ix = startX + i * (scaledItemW + scaledGap);
        float iy = startY;

        m_avatarRects[i] = {ix, iy, scaledAvatarSize, scaledAvatarSize};

        if (i == m_selected) {
            float pad = 4.f * sc;
            nxui::Rect cursorRect = {ix - pad, iy - pad,
                               scaledAvatarSize + pad * 2.f,
                               scaledAvatarSize + pad * 2.f};
            float circleRadius = cursorRect.width * 0.5f;
            m_cursor.moveTo(cursorRect, circleRadius, 0.15f);
        }

        auto& user = m_users[i];
        if (user.icon.valid()) {
            ren.drawTextureRounded(&user.icon,
                                   {ix, iy, scaledAvatarSize, scaledAvatarSize},
                                   scaledAvatarSize * 0.5f,
                                   nxui::Color::white().withAlpha(alpha));
        } else {
            float r = scaledAvatarSize * 0.5f;
            ren.drawCircle({ix + r, iy + r}, r,
                           nxui::Color(0.3f, 0.3f, 0.4f, 0.6f * alpha), 32);
        }

        if (i == m_touchHitAvatar && !m_touchOnSelected) {
            float r = scaledAvatarSize * 0.5f;
            ren.drawCircle({ix + r, iy + r}, r,
                           nxui::Color(1.f, 1.f, 1.f, 0.18f * alpha), 32);
        }

        nxui::Font* f = m_smallFont ? m_smallFont : m_font;
        if (f && !user.nickname.empty()) {
            nxui::Vec2 nameSz = f->measure(user.nickname);
            float nameScale = 0.85f * sc;
            float actualW = nameSz.x * nameScale;
            float nx = ix + (scaledAvatarSize - actualW) * 0.5f;
            float ny = iy + scaledAvatarSize + 8.f * sc;
            nxui::Color nc = (i == m_selected) ? m_textPrimary : m_textSecondary;
            ren.drawText(user.nickname, {nx, ny}, f,
                         nc.withAlpha(alpha), nameScale);
        }
    }

    m_cursor.render(ren);
}

