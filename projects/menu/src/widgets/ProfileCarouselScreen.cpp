#include "ProfileCarouselScreen.hpp"
#include "ProfileDrawing.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>

namespace {
// Tile row.
constexpr float kTileSize = 136.f;
constexpr float kTileGap = 36.f;
constexpr float kTileStep = kTileSize + kTileGap;
constexpr float kTileRadius = 18.f;
constexpr float kRowMarginX = 70.f;
constexpr float kPortraitRowMarginX = 28.f;
constexpr float kSelectedGrow = 14.f;

// Details panel (landscape defaults; portrait recenters in drawDetails).
constexpr float kDetailAvatarSize = 190.f;
constexpr float kDetailAvatarX = 90.f;
constexpr float kDetailColumnX = 340.f;
constexpr float kPortraitDetailAvatarSize = 160.f;

constexpr float kSwipeThreshold = 60.f;
constexpr float kTapThreshold = 20.f;

bool sameUid(const AccountUid& a, const AccountUid& b) {
    return a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1];
}
}

ProfileCarouselScreen::ProfileCarouselScreen() {
    setTag("profileCarousel");
    setFrameworkTouchEnabled(false);
    setVisible(false);
    setFocusable(true);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setRect({0.f, 0.f, m_canvasW, m_canvasH});
}

void ProfileCarouselScreen::setCanvasSize(float width, float height) {
    m_canvasW = width > 1.f ? width : 1280.f;
    m_canvasH = height > 1.f ? height : 720.f;
    setRect({0.f, 0.f, m_canvasW, m_canvasH});
    m_rowScroll.setImmediate(rowScrollTarget(m_selected));
}

void ProfileCarouselScreen::setProfiles(std::vector<HomeProfile> profiles, bool addUserAvailable) {
    std::optional<AccountUid> selectedUid;
    if (const HomeProfile* p = tileProfile(m_selected))
        selectedUid = p->uid;

    m_profiles = std::move(profiles);
    m_addUserAvailable = addUserAvailable;

    // Keep the highlighted tile stable while avatars stream in.
    int index = std::clamp(m_selected, 0, tileCount() - 1);
    if (selectedUid) {
        for (int i = 0; i < (int)m_profiles.size(); ++i)
            if (sameUid(m_profiles[(size_t)i].uid, *selectedUid))
                index = i + 1;
    }
    m_selected = index;
    m_rowScroll.setImmediate(rowScrollTarget(index));
}

const HomeProfile* ProfileCarouselScreen::tileProfile(int index) const {
    if (tileKind(index) != CardKind::Profile || index > (int)m_profiles.size())
        return nullptr;
    return &m_profiles[(size_t)(index - 1)];
}

bool ProfileCarouselScreen::isLockedTile(int index) const {
    return index == lockedTileIndex();
}

int ProfileCarouselScreen::lockedTileIndex() const {
    if (m_lockedUid) {
        for (int i = 0; i < (int)m_profiles.size(); ++i)
            if (sameUid(m_profiles[(size_t)i].uid, *m_lockedUid))
                return i + 1;
    }
    // A locked profile that no longer exists behaves like "nobody".
    return 0;
}

std::string ProfileCarouselScreen::tileName(int index) const {
    if (const HomeProfile* p = tileProfile(index))
        return p->nickname;
    if (tileKind(index) == CardKind::AddUser)
        return nxui::I18n::instance().tr("userselect.add_user", "Add user");
    return nxui::I18n::instance().tr("profilelock.none", "Ask each time");
}

float ProfileCarouselScreen::rowContentWidth() const {
    return tileCount() * kTileStep - kTileGap;
}

float ProfileCarouselScreen::rowScrollTarget(int index) const {
    const float margin = isPortraitCanvas() ? kPortraitRowMarginX : kRowMarginX;
    const float viewW = screenW() - margin * 2.f;
    const float contentW = rowContentWidth();
    if (contentW <= viewW)
        return 0.f;
    const float center = index * kTileStep + kTileSize * 0.5f;
    return std::clamp(center - viewW * 0.5f, 0.f, contentW - viewW);
}

nxui::Rect ProfileCarouselScreen::tileRect(int index) const {
    const float margin = isPortraitCanvas() ? kPortraitRowMarginX : kRowMarginX;
    const float viewW = screenW() - margin * 2.f;
    const float contentW = rowContentWidth();
    const float startX = contentW <= viewW
        ? (screenW() - contentW) * 0.5f
        : margin - m_rowScroll.value();
    return {startX + index * kTileStep, rowY(), kTileSize, kTileSize};
}

void ProfileCarouselScreen::show() {
    if (m_active && !m_animatingOut)
        return;
    m_active = true;
    m_animatingOut = false;
    m_touchHitTile = -1;
    m_selected = lockedTileIndex();
    m_rowScroll.setImmediate(rowScrollTarget(m_selected));
    m_focusPop.setImmediate(1.f);
    m_alpha.setImmediate(0.f);
    m_alpha.set(1.f, 0.22f, nxui::Easing::outCubic);
    setVisible(true);
    setupActions();

    if (m_accessibilityCb) {
        auto& i18n = nxui::I18n::instance();
        m_accessibilityCb(i18n.tr("profilelock.title", "Default profile") + ". " +
                          i18n.tr("profilelock.opened_hint",
                                  "Left and right to browse. X sets the default profile."));
    }
    announceSelection();
}

void ProfileCarouselScreen::hide() {
    if (!m_active || m_animatingOut)
        return;
    m_animatingOut = true;
    if (m_closeSfxCb) m_closeSfxCb();
    m_alpha.set(0.f, 0.18f, nxui::Easing::outCubic);
    clearActions();
}

void ProfileCarouselScreen::setupActions() {
    clearActions();
    addDirectionAction(nxui::FocusDirection::LEFT, [this]() { moveHorizontal(-1); });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() { moveHorizontal(1); });
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (isOpen()) activateSelected();
    });
    addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (isOpen()) lockSelected();
    });
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (isOpen()) hide();
    });
}

void ProfileCarouselScreen::selectTile(int index, bool announce) {
    index = std::clamp(index, 0, tileCount() - 1);
    if (index == m_selected)
        return;
    m_selected = index;
    m_rowScroll.set(rowScrollTarget(index), 0.24f, nxui::Easing::outCubic);
    m_focusPop.setImmediate(0.f);
    m_focusPop.set(1.f, 0.20f, nxui::Easing::outCubic);
    if (m_navSfxCb) m_navSfxCb();
    if (announce)
        announceSelection();
}

void ProfileCarouselScreen::moveHorizontal(int direction) {
    if (!isOpen())
        return;
    selectTile(m_selected + direction);
}

void ProfileCarouselScreen::activateSelected() {
    if (onAddUserTile()) {
        if (m_addUserCb) m_addUserCb();
        return;
    }
    if (const HomeProfile* p = tileProfile(m_selected))
        if (m_openProfileCb) m_openProfileCb(p->uid);
}

void ProfileCarouselScreen::lockSelected() {
    if (onAddUserTile() || isLockedTile(m_selected))
        return;
    std::optional<AccountUid> uid;
    if (const HomeProfile* p = tileProfile(m_selected))
        uid = p->uid;
    m_lockedUid = uid;
    if (m_lockSfxCb) m_lockSfxCb();
    if (m_lockProfileCb)
        m_lockProfileCb(uid);
    if (m_accessibilityCb) {
        auto& i18n = nxui::I18n::instance();
        m_accessibilityCb(uid
            ? i18n.tr("profilelock.pill_locked", "Playing as") + " " + tileName(m_selected)
            : i18n.tr("profilelock.pill_unlocked", "Ask who's playing"));
    }
}

void ProfileCarouselScreen::announceSelection() {
    if (!m_accessibilityCb)
        return;
    auto& i18n = nxui::I18n::instance();
    if (onAddUserTile()) {
        m_accessibilityCb(i18n.tr("userselect.add_user", "Add user") + ". " +
                          i18n.tr("userselect.add_user_hint", "A to create a new user."));
        return;
    }
    std::string text = tileName(m_selected);
    if (isLockedTile(m_selected) && m_selected != 0)
        text += ", " + i18n.tr("profilelock.default_badge", "Default");
    text += ", " + std::to_string(m_selected + 1) + " " +
            i18n.tr("accessibility.context.of", "of") + " " + std::to_string(tileCount());
    m_accessibilityCb(text);
}

void ProfileCarouselScreen::handleTouch(nxui::Input& input) {
    if (!isOpen())
        return;
    auto hitTile = [this](float x, float y) {
        for (int i = 0; i < tileCount(); ++i)
            if (tileRect(i).expanded(8.f).contains(x, y))
                return i;
        return -1;
    };

    if (input.touchDown()) {
        m_touchHitTile = hitTile(input.touchX(), input.touchY());
    }

    if (input.touchUp()) {
        const float dx = input.touchDeltaX();
        const float dy = input.touchDeltaY();
        const bool tap = std::abs(dx) < kTapThreshold && std::abs(dy) < kTapThreshold;
        if (std::abs(dx) >= kSwipeThreshold && std::abs(dx) > std::abs(dy)) {
            // Swipe drags the row: moving the finger left reveals tiles on the right.
            const int steps = std::max(1, (int)std::lround(std::abs(dx) / kTileStep));
            selectTile(m_selected + (dx < 0.f ? steps : -steps));
        } else if (tap && m_touchHitTile >= 0 &&
                   hitTile(input.touchX(), input.touchY()) == m_touchHitTile) {
            if (m_touchHitTile == m_selected)
                activateSelected();
            else
                selectTile(m_touchHitTile);
        }
        m_touchHitTile = -1;
    }
}

void ProfileCarouselScreen::update(float dt) {
    if (!isActive())
        return;
    m_alpha.update(dt);
    m_rowScroll.update(dt);
    m_focusPop.update(dt);
    if (m_animatingOut && m_alpha.value() <= 0.01f) {
        m_active = false;
        m_animatingOut = false;
        setVisible(false);
        if (m_closedCb) m_closedCb();
    }
}

void ProfileCarouselScreen::render(nxui::Renderer& ren) {
    if (!isActive() || !m_theme || !m_font)
        return;
    const float alpha = m_alpha.value();

    ren.drawGradientRect({0.f, 0.f, screenW(), screenH()},
                         m_theme->background.withAlpha(0.97f * alpha),
                         m_theme->backgroundAccent.withAlpha(0.98f * alpha));

    ren.pushClipRect({0.f, rowY() - 40.f, screenW(), kTileSize + 80.f});
    for (int i = 0; i < tileCount(); ++i)
        if (i != m_selected)
            drawTile(ren, i, alpha);
    // Highlighted tile last so its enlarged frame overlaps its neighbours.
    drawTile(ren, m_selected, alpha);
    ren.popClipRect();

    drawBubble(ren, alpha);
    drawDetails(ren, alpha);
}

void ProfileCarouselScreen::drawAvatar(nxui::Renderer& ren, int index, const nxui::Rect& r,
                                       float radius, float alpha) {
    const HomeProfile* profile = tileProfile(index);
    if (profile && profile->avatar && profile->avatar->valid()) {
        ren.drawTextureRounded(profile->avatar.get(), r, radius,
                               nxui::Color::white().withAlpha(alpha));
        return;
    }
    ren.drawRoundedRect(r, m_theme->iconDefault.withAlpha(0.45f * alpha), radius);
    switchu::ui::drawProfileSilhouette(ren, r.shrunk(r.width * 0.14f),
                                       m_theme->textSecondary.withAlpha(0.60f * alpha));
}

void ProfileCarouselScreen::drawTile(nxui::Renderer& ren, int index, float alpha) {
    nxui::Rect tile = tileRect(index);
    if (tile.x + tile.width < -kSelectedGrow || tile.x > screenW() + kSelectedGrow)
        return;
    const bool selected = index == m_selected;
    if (selected)
        tile = tile.expanded(kSelectedGrow * m_focusPop.value());
    const bool dark = m_theme->mode == nxui::ThemeMode::Dark;
    const bool addUser = tileKind(index) == CardKind::AddUser;
    const float radius = addUser ? std::min(tile.width, tile.height) * 0.5f : kTileRadius;

    ren.drawFrostedInset(tile, m_theme->panelBase.withAlpha(dark ? 0.90f : 0.78f),
                         m_theme->panelBorder.withAlpha(0.40f),
                         m_theme->panelHighlight.withAlpha(0.22f),
                         radius, alpha);

    if (addUser) {
        // Circular "add user" entry, sized like the profile tiles.
        switchu::ui::drawPlusSign(ren, tile.shrunk(tile.width * 0.28f),
                                  m_theme->textSecondary.withAlpha(0.90f * alpha));
    } else {
        drawAvatar(ren, index, tile.shrunk(12.f), std::max(0.f, radius - 6.f), alpha);
    }

    if (selected) {
        ren.drawRoundedRectOutline(tile.expanded(4.f), m_theme->cursorNormal.withAlpha(alpha),
                                   radius + 4.f, 7.f);
    }
    if (!addUser && isLockedTile(index))
        drawCheckBadge(ren, tile, alpha);
}

void ProfileCarouselScreen::drawCheckBadge(nxui::Renderer& ren, const nxui::Rect& tile, float alpha) {
    const float radius = 18.f;
    const nxui::Vec2 center{tile.x + tile.width - 6.f, tile.y + 6.f};
    ren.drawCircle(center, radius + 3.f, m_theme->panelBase.withAlpha(alpha), 32);
    ren.drawCircle(center, radius, m_theme->cursorNormal.withAlpha(alpha), 32);
    switchu::ui::drawCheckmark(ren, {center.x - radius, center.y - radius, radius * 2.f, radius * 2.f},
                               nxui::Color::white().withAlpha(alpha));
}

void ProfileCarouselScreen::drawBubble(nxui::Renderer& ren, float alpha) {
    // Wii U style name tag above the highlighted tile.
    const std::string text = tileName(m_selected);

    const nxui::Rect tile = tileRect(m_selected).expanded(kSelectedGrow * m_focusPop.value());
    const float textScale = 1.f;
    const nxui::Vec2 size = m_font->measure(text);
    const float padX = 24.f;
    const float padY = 12.f;
    const float w = size.x * textScale + padX * 2.f;
    const float h = size.y * textScale + padY * 2.f;
    const float tailH = 12.f;
    const float centerX = tile.x + tile.width * 0.5f;
    const float x = std::clamp(centerX - w * 0.5f, 16.f, screenW() - 16.f - w);
    const float y = tile.y - 16.f - tailH - h;
    const float bubbleAlpha = alpha * m_focusPop.value();
    const nxui::Color fill = m_theme->panelBase.withAlpha(
        (m_theme->mode == nxui::ThemeMode::Dark ? 0.96f : 0.92f) * bubbleAlpha);

    ren.drawRoundedRect({x, y, w, h}, fill, h * 0.5f);
    ren.drawRoundedRectOutline({x, y, w, h}, m_theme->panelBorder.withAlpha(0.30f * bubbleAlpha),
                               h * 0.5f, 1.2f);
    ren.drawTriangle({centerX - 11.f, y + h - 1.f}, {centerX + 11.f, y + h - 1.f},
                     {centerX, y + h + tailH}, fill);
    ren.drawText(text, {x + padX, y + padY}, m_font,
                 m_theme->textPrimary.withAlpha(bubbleAlpha), textScale);
}

void ProfileCarouselScreen::drawDetails(nxui::Renderer& ren, float alpha) {
    // "Ask each time" isn't an account, and the "add user" tile has none either.
    const HomeProfile* profile = tileProfile(m_selected);
    if (!profile)
        return;

    // Landscape: avatar left + name column. Portrait: centered stack under the row.
    const float dy = detailY();
    const float avatarSize = isPortraitCanvas() ? kPortraitDetailAvatarSize : kDetailAvatarSize;
    const float avatarX = isPortraitCanvas()
        ? (screenW() - avatarSize) * 0.5f
        : kDetailAvatarX;
    const nxui::Rect avatar{avatarX, dy, avatarSize, avatarSize};
    ren.drawRoundedRect(avatar.expanded(6.f), m_theme->panelBase.withAlpha(0.70f * alpha), 30.f);
    drawAvatar(ren, m_selected, avatar, 26.f, alpha);

    const float nameScale = isPortraitCanvas() ? 1.15f : 1.4f;
    const nxui::Vec2 nameSize = m_font->measure(profile->nickname);
    const float nameX = isPortraitCanvas()
        ? (screenW() - nameSize.x * nameScale) * 0.5f
        : kDetailColumnX;
    const float nameY = isPortraitCanvas()
        ? avatar.bottom() + 28.f
        : dy + 40.f;
    ren.drawText(profile->nickname, {nameX, nameY}, m_font,
                 m_theme->textPrimary.withAlpha(alpha), nameScale);

    if (!isLockedTile(m_selected))
        return;
    nxui::Font* small = m_smallFont ? m_smallFont : m_font;
    const std::string tag = nxui::I18n::instance().tr("profilelock.default_badge", "Default");
    const nxui::Vec2 tagSize = small->measure(tag);
    const float padX = 16.f;
    const float padY = 6.f;
    const float pillW = tagSize.x + padX * 2.f;
    const float pillX = isPortraitCanvas()
        ? (screenW() - pillW) * 0.5f
        : kDetailColumnX;
    const nxui::Rect pill{pillX, nameY + nameSize.y * nameScale + 18.f,
                          pillW, tagSize.y + padY * 2.f};
    ren.drawRoundedRect(pill, m_theme->cursorNormal.withAlpha(alpha), pill.height * 0.5f);
    ren.drawText(tag, {pill.x + padX, pill.y + padY}, small, nxui::Color::white().withAlpha(alpha));
}
