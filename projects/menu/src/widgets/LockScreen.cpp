#include "LockScreen.hpp"
#include "BatteryDrawing.hpp"
#include "services/ClockService.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float kScreenW = 1280.f;
constexpr float kScreenH = 720.f;
constexpr float kMargin = 56.f;
constexpr float kCurtainTravel = 740.f;
constexpr float kInputGuard = 0.35f;
constexpr int kTimeFontPt = 108;
constexpr float kResumeBtnW = 280.f;
constexpr float kResumeBtnH = 52.f;
constexpr float kResumeBtnY = 520.f;
constexpr float kIconSize = 148.f;
constexpr float kIconRadius = 16.f; // match GlossyIcon grid tiles
// Vertically center the tile between the clock block and Resume.
constexpr float kClockBottom = 230.f;
constexpr float kIconY = (kClockBottom + kResumeBtnY) * 0.5f - kIconSize * 0.5f;
constexpr float kSwipeCommit = 0.28f;
constexpr float kSwipeFlickPx = 140.f;
constexpr float kSwipeDeadzone = 18.f;

std::string replaceAll(std::string text, const std::string& token, const std::string& value) {
    for (std::size_t pos = text.find(token); pos != std::string::npos;
         pos = text.find(token, pos + value.size()))
        text.replace(pos, token.size(), value);
    return text;
}

void drawCentered(nxui::Renderer& ren, const std::string& text, float y,
                  nxui::Font* font, const nxui::Color& color, float scale, float dy) {
    if (!font || text.empty())
        return;
    const nxui::Vec2 sz = font->measure(text);
    const float x = (kScreenW - sz.x * scale) * 0.5f;
    ren.drawText(text, {x, y + dy}, font, color, scale);
}

nxui::Rect resumeButtonRect(float dy) {
    return {(kScreenW - kResumeBtnW) * 0.5f, kResumeBtnY + dy, kResumeBtnW, kResumeBtnH};
}

nxui::Rect iconRect(float dy) {
    return {(kScreenW - kIconSize) * 0.5f, kIconY + dy, kIconSize, kIconSize};
}
}

LockScreen::LockScreen() {
    setRect({0.f, 0.f, kScreenW, kScreenH});
    setVisible(false);
    setFocusable(true);
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setLiquidGlassShaderEnabled(false);
    setBlurEnabled(false);
    setFrameworkTouchEnabled(false);
    wireActions();
}

void LockScreen::wireActions() {
    clearActions();
    auto primary = [this]() {
        if (m_canResume)
            resumeGame();
        else
            unlock();
    };
    for (auto button : {nxui::Button::A, nxui::Button::Plus})
        addAction(static_cast<std::uint64_t>(button), primary);

    auto secondary = [this]() { unlock(); };
    for (auto button : {nxui::Button::B, nxui::Button::X, nxui::Button::Y,
                        nxui::Button::L, nxui::Button::R, nxui::Button::ZL, nxui::Button::ZR,
                        nxui::Button::Minus,
                        nxui::Button::DUp, nxui::Button::DDown,
                        nxui::Button::DLeft, nxui::Button::DRight})
        addAction(static_cast<std::uint64_t>(button), secondary);
}

void LockScreen::warmTimeFont() {
    if (m_timeFont || !m_gpu || !m_ren || m_timeFontPath.empty())
        return;
    auto font = std::make_unique<nxui::Font>();
    if (font->load(*m_gpu, *m_ren, m_timeFontPath, kTimeFontPt))
        m_timeFont = std::move(font);
}

void LockScreen::loadPendingHero() {
    if (m_hasHero) {
        m_heroTex = nxui::Texture();
        m_hasHero = false;
    }
    if (!m_gpu || !m_ren || m_pendingHero.empty() ||
        m_pendingHero.size() != 1280u * 720u * 4u)
        return;
    constexpr std::uint64_t kTextReserve = 6u * 1024u * 1024u;
    if (m_gpu->imageMemoryAvailable() <= kTextReserve + 640u * 360u * 4u)
        return;
    std::vector<std::uint8_t> small(640u * 360u * 4u);
    for (unsigned y = 0; y < 360u; ++y) {
        for (unsigned x = 0; x < 640u; ++x) {
            for (unsigned c = 0; c < 4u; ++c) {
                const std::uint8_t* src =
                    m_pendingHero.data() + ((y * 2u) * 1280u + x * 2u) * 4u + c;
                small[(y * 640u + x) * 4u + c] = static_cast<std::uint8_t>(
                    (src[0] + src[4] + src[1280u * 4u] + src[1280u * 4u + 4]) / 4u);
            }
        }
    }
    m_hasHero = m_heroTex.loadFromPixels(*m_gpu, *m_ren, small.data(), 640, 360);
}

void LockScreen::loadPendingIcon() {
    if (m_hasIcon) {
        m_iconTex = nxui::Texture();
        m_hasIcon = false;
    }
    if (!m_gpu || !m_ren || m_pendingIcon.empty() || m_pendingIconW <= 0 || m_pendingIconH <= 0)
        return;
    if (m_pendingIcon.size() != static_cast<size_t>(m_pendingIconW) * m_pendingIconH * 4u)
        return;
    constexpr std::uint64_t kTextReserve = 4u * 1024u * 1024u;
    if (m_gpu->imageMemoryAvailable() <= kTextReserve +
        static_cast<std::uint64_t>(m_pendingIconW) * m_pendingIconH * 4u)
        return;
    m_hasIcon = m_iconTex.loadFromPixels(*m_gpu, *m_ren, m_pendingIcon.data(),
                                         m_pendingIconW, m_pendingIconH);
}

void LockScreen::show(bool instant) {
    auto& i18n = nxui::I18n::instance();
    if (m_canResume) {
        m_unlockText = i18n.tr("lock.unlock_home", "Press B to unlock");
        m_resumeText = i18n.tr("lock.resume", "Press A to resume");
    } else {
        m_unlockText = i18n.tr("lock.enter", "Press A to enter");
        m_resumeText.clear();
    }

    warmTimeFont();
    loadPendingHero();
    loadPendingIcon();
    m_pendingHero.clear();
    m_pendingHero.shrink_to_fit();
    m_pendingIcon.clear();
    m_pendingIcon.shrink_to_fit();
    m_pendingIconW = 0;
    m_pendingIconH = 0;
    m_releaseTimer = 0.f;

    m_active = true;
    m_unlocking = false;
    m_age = 0.f;
    m_clockTimer = 0.f;
    m_suspendPulse = 0.f;
    m_lastMinute = -1;
    resetSwipe();
    m_curtain.setImmediate(0.f);
    if (instant)
        m_fade.setImmediate(1.f);
    else {
        m_fade.setImmediate(0.f);
        m_fade.set(1.f, 0.28f, nxui::Easing::outCubic);
    }
    refreshClock();
    setVisible(true);
}

void LockScreen::hide() {
    m_active = false;
    m_unlocking = false;
    setVisible(false);
    m_releaseTimer = 0.f;
    resetSwipe();
    m_curtain.setImmediate(0.f);
}

void LockScreen::releaseArt() {
    if (m_hasHero)
        m_heroTex = nxui::Texture();
    m_hasHero = false;
    if (m_hasIcon)
        m_iconTex = nxui::Texture();
    m_hasIcon = false;
}

void LockScreen::tickRelease(float dt) {
    if (m_active || (!m_hasHero && !m_hasIcon))
        return;
    m_releaseTimer += dt;
    if (m_releaseTimer >= 1.5f)
        releaseArt();
}

void LockScreen::resetSwipe() {
    m_swipeActive = false;
    m_swipeDragging = false;
    m_curtainHomeShown = false;
    m_swipeStartY = 0.f;
}

void LockScreen::beginCurtainUnlock() {
    if (!m_active || m_unlocking)
        return;
    m_unlocking = true;
    m_swipeActive = false;
    m_swipeDragging = false;
    if (!m_curtainHomeShown && m_onUnlocking) {
        m_onUnlocking();
        m_curtainHomeShown = true;
    }
    m_fade.setImmediate(1.f);
    const float remaining = std::clamp(1.f - m_curtain.value(), 0.05f, 1.f);
    m_curtain.set(1.f, 0.20f + 0.35f * remaining, nxui::Easing::inOutCubic);
}

void LockScreen::unlock() {
    if (!m_active || m_unlocking || m_age < kInputGuard)
        return;
    beginCurtainUnlock();
}

void LockScreen::resumeGame() {
    if (!m_active || !m_canResume || m_unlocking || m_age < kInputGuard)
        return;
    // Keep Lock painted until the suspended title takes the foreground —
    // hiding here flashed HOME for a frame.
    if (m_onResume)
        m_onResume();
}

void LockScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_unlocking)
        return;

    if (input.touchDown()) {
        if (m_age < kInputGuard)
            return;
        m_swipeActive = true;
        m_swipeDragging = false;
        m_swipeStartY = input.touchY();
        return;
    }

    if (!m_swipeActive)
        return;

    if (input.isTouching()) {
        const float dy = m_swipeStartY - input.touchY(); // up = positive
        if (!m_swipeDragging) {
            if (std::fabs(input.touchDeltaX()) > std::fabs(dy) + 12.f &&
                std::fabs(input.touchDeltaX()) > kSwipeDeadzone) {
                return;
            }
            if (dy < kSwipeDeadzone)
                return;
            m_swipeDragging = true;
        }
        const float progress = std::clamp(dy / kCurtainTravel, 0.f, 1.f);
        m_curtain.setImmediate(progress);
        m_fade.setImmediate(1.f);
        return;
    }

    if (!input.touchUp())
        return;

    const bool wasDragging = m_swipeDragging;
    const float progress = m_curtain.value();
    const float flick = m_swipeStartY - input.touchY();
    m_swipeActive = false;
    m_swipeDragging = false;

    if (wasDragging) {
        if (progress >= kSwipeCommit || flick >= kSwipeFlickPx) {
            beginCurtainUnlock();
        } else {
            m_curtain.set(0.f, 0.28f, nxui::Easing::outCubic);
        }
        return;
    }

    if (m_canResume) {
        const auto r = resumeButtonRect(0.f);
        const float x = input.touchX();
        const float y = input.touchY();
        if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height) {
            resumeGame();
            return;
        }
        const auto ir = iconRect(0.f);
        if (x >= ir.x && x <= ir.x + ir.width && y >= ir.y && y <= ir.y + ir.height) {
            resumeGame();
            return;
        }
    }
    unlock();
}

void LockScreen::refreshClock() {
    if (!m_clock)
        return;
    const auto snapshot = m_clock->refresh();
    if (!snapshot.valid)
        return;
    const auto& cal = snapshot.calendar;
    if (static_cast<int>(cal.hour) * 60 + cal.minute == m_lastMinute)
        return;
    m_lastMinute = static_cast<int>(cal.hour) * 60 + cal.minute;

    char buf[32];
    if (m_use12Hour) {
        int hour = cal.hour % 12;
        if (hour == 0) hour = 12;
        std::snprintf(buf, sizeof(buf), "%d:%02d", hour, cal.minute);
    } else {
        std::snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(cal.hour),
                      static_cast<unsigned>(cal.minute));
    }
    m_timeStr = buf;

    auto& i18n = nxui::I18n::instance();
    static const char* const kWeekdayFallback[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* const kMonthFallback[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    const unsigned wday = std::min<unsigned>(snapshot.additional.wday, 6u);
    const unsigned month = std::clamp<unsigned>(cal.month, 1u, 12u);
    std::string weekday = i18n.tr("lock.weekday." + std::to_string(wday), kWeekdayFallback[wday]);
    std::string monthName = i18n.tr("lock.month." + std::to_string(month), kMonthFallback[month - 1]);
    std::string out = i18n.tr("lock.date_format", "{weekday}, {month} {day}");
    out = replaceAll(out, "{weekday}", weekday);
    out = replaceAll(out, "{month}", monthName);
    out = replaceAll(out, "{day}", std::to_string(static_cast<unsigned>(cal.day)));
    out = replaceAll(out, "{year}", std::to_string(static_cast<unsigned>(cal.year)));
    m_dateStr = out;
}

void LockScreen::update(float dt) {
    if (!m_active)
        return;
    m_age += dt;
    if (m_canResume)
        m_suspendPulse += dt * 2.8f;
    m_curtain.update(dt);
    m_fade.update(dt);
    m_clockTimer += dt;
    if (m_clockTimer >= 1.f) {
        m_clockTimer = 0.f;
        refreshClock();
    }
    if (m_unlocking && m_curtain.value() >= 0.999f) {
        m_active = false;
        m_unlocking = false;
        setVisible(false);
        if (m_onUnlocked)
            m_onUnlocked();
    }
}

void LockScreen::render(nxui::Renderer& ren) {
    if (!m_active || !m_theme || !m_font)
        return;

    const float dy = -m_curtain.value() * kCurtainTravel;
    if (dy <= -kScreenH)
        return;
    auto at = [dy](nxui::Rect r) { r.y += dy; return r; };
    const nxui::Theme& th = *m_theme;
    const float fade = std::clamp(m_fade.value(), 0.f, 1.f);
    const bool curtainMoving = m_unlocking || m_swipeDragging || m_curtain.value() > 0.001f;

    // Idle without hero: live bg layer shows through. Once the curtain moves
    // (or a hero is up), paint theme wallpaper on the lock plane so it slides.
    if (m_hasHero || curtainMoving) {
        if (m_onDrawBackdrop)
            m_onDrawBackdrop(ren, dy, fade);
        else
            ren.drawRect(at({0.f, 0.f, kScreenW, kScreenH}), th.background.withAlpha(1.f));
        if (m_hasHero) {
            ren.drawTexture(&m_heroTex, at({0.f, 0.f, kScreenW, kScreenH}));
            ren.drawRect(at({0.f, 0.f, kScreenW, kScreenH}),
                         nxui::Color(0.f, 0.f, 0.f, (m_unlocking ? 0.55f : 0.42f) * fade));
        }
    }

    const bool lightInk = m_hasHero;
    const nxui::Color ink = (lightInk ? nxui::Color(1.f, 1.f, 1.f, 1.f) : th.textPrimary)
                                .withAlpha(fade);
    const nxui::Color inkSoft = (lightInk ? nxui::Color(1.f, 1.f, 1.f, 1.f) : th.textSecondary)
                                    .withAlpha((lightInk ? 0.82f : 0.90f) * fade);
    nxui::Font* small = m_smallFont ? m_smallFont : m_font;

    if (m_battery >= 0) {
        const std::string pct = std::to_string(m_battery) + "%";
        const nxui::Vec2 psz = small->measure(pct);
        const float right = kScreenW - kMargin;
        ren.drawText(pct, {right - psz.x, 44.f + dy}, small, inkSoft, 1.f);
        const nxui::Rect body = at({right - psz.x - 52.f, 48.f, 40.f, 20.f});
        ren.drawRoundedRectOutline(body, inkSoft, 4.f, 1.6f);
        ren.drawRect({body.right() + 2.f, body.y + 5.f, 3.f, 10.f}, inkSoft);
        const float fill = std::clamp(m_battery / 100.f, 0.f, 1.f);
        ren.drawRoundedRect({body.x + 2.5f, body.y + 2.5f, (body.width - 5.f) * fill, body.height - 5.f},
                            ink.withAlpha(fade * 0.92f), 2.5f);
        if (m_charging)
            switchu::battery_drawing::drawLightningBolt(
                ren, {body.x + 10.f, body.y - 2.f, 18.f, 22.f}, ink, th.background.withAlpha(fade), 1.f);
    }

    drawCentered(ren, m_dateStr, 78.f, small, inkSoft, 0.98f, dy);
    if (m_timeFont) {
        const nxui::Vec2 tsz = m_timeFont->measure(m_timeStr);
        ren.drawText(m_timeStr, {(kScreenW - tsz.x) * 0.5f, 118.f + dy}, m_timeFont.get(), ink, 1.f);
    } else {
        drawCentered(ren, m_timeStr, 128.f, m_font, ink, 2.6f, dy);
    }

    if (m_canResume) {
        if (m_hasIcon) {
            const nxui::Rect ir = iconRect(dy);
            ren.drawTextureRounded(&m_iconTex, ir, kIconRadius);

            // Match GlossyIcon suspended chrome: breathing green ring + play badge.
            const float pulse = 0.5f + 0.5f * std::sin(m_suspendPulse);
            const float outerAlpha = (0.28f + 0.42f * pulse) * fade;
            const float innerAlpha = (0.55f + 0.35f * pulse) * fade;
            const nxui::Color glow(0.20f, 0.92f, 0.50f, 1.f);
            ren.drawRoundedRectOutline(ir.expanded(5.f),
                                       glow.withAlpha(outerAlpha * 0.55f),
                                       kIconRadius + 5.f, 4.5f);
            ren.drawRoundedRectOutline(ir.expanded(2.f),
                                       glow.withAlpha(innerAlpha),
                                       kIconRadius + 2.f, 3.2f);

            const float badgeSize = 28.f;
            const float badgeX = ir.x + ir.width - badgeSize - 4.f;
            const float badgeY = ir.y + ir.height - badgeSize - 4.f;
            const nxui::Vec2 badgeCenter = {badgeX + badgeSize * 0.5f,
                                            badgeY + badgeSize * 0.5f};
            ren.drawCircle(badgeCenter, badgeSize * 0.58f,
                           glow.withAlpha((0.20f + 0.25f * pulse) * fade), 20);
            ren.drawCircle(badgeCenter, badgeSize * 0.5f,
                           nxui::Color(0.06f, 0.10f, 0.08f, 0.90f * fade), 16);
            const float triH = badgeSize * 0.45f;
            const float triW = triH * 0.85f;
            const nxui::Vec2 p1 = {badgeCenter.x - triW * 0.35f, badgeCenter.y - triH * 0.5f};
            const nxui::Vec2 p2 = {badgeCenter.x - triW * 0.35f, badgeCenter.y + triH * 0.5f};
            const nxui::Vec2 p3 = {badgeCenter.x + triW * 0.65f, badgeCenter.y};
            ren.drawTriangle(p1, p2, p3, glow.withAlpha(0.98f * fade));
        } else if (!m_suspendedTitle.empty()) {
            drawCentered(ren, m_suspendedTitle, 300.f, m_font, inkSoft, 1.0f, dy);
        }

        const nxui::Rect btn = resumeButtonRect(dy);
        ren.drawRoundedRect(btn, ink.withAlpha(fade * 0.22f), 12.f);
        ren.drawRoundedRectOutline(btn, inkSoft, 12.f, 1.5f);
        const nxui::Vec2 rsz = m_font->measure(m_resumeText);
        ren.drawText(m_resumeText,
                     {btn.x + (btn.width - rsz.x) * 0.5f, btn.y + (btn.height - rsz.y) * 0.5f},
                     m_font, ink, 1.f);
        drawCentered(ren, m_unlockText, 600.f, small, inkSoft, 1.0f, dy);
    } else {
        drawCentered(ren, m_unlockText, 348.f, m_font, inkSoft, 1.05f, dy);
    }
}
