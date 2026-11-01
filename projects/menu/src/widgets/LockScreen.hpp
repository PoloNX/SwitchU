#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include <nxui/core/Texture.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace switchu::services { class ClockService; }

/// Lock *view* content. Theme / WaraWara stay on the bg layer while idle;
/// the curtain plane can re-draw that wallpaper so it slides with unlock.
/// Optional suspended 1×1 tile + Resume / Unlock.
class LockScreen : public nxui::GlassWidget {
public:
    using BackdropDraw = std::function<void(nxui::Renderer& ren, float dy, float opacity)>;

    LockScreen();

    void setFonts(nxui::Font* body, nxui::Font* small) { m_font = body; m_smallFont = small; }
    void setTimeFontSource(nxui::GpuDevice* gpu, nxui::Renderer* ren, std::string path) {
        if (path != m_timeFontPath)
            m_timeFont.reset();
        m_gpu = gpu;
        m_ren = ren;
        m_timeFontPath = std::move(path);
    }
    void invalidateTimeFont() { m_timeFont.reset(); }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setClockService(switchu::services::ClockService* clock) { m_clock = clock; }
    void setUse12HourClock(bool enabled) { m_use12Hour = enabled; m_lastMinute = -1; }
    void setBatteryStatus(int percent, bool charging) { m_battery = percent; m_charging = charging; }
    void setBackdropDrawer(BackdropDraw cb) { m_onDrawBackdrop = std::move(cb); }

    /// Fired when the unlock curtain starts (HOME chrome should show under it).
    void setOnUnlocking(std::function<void()> cb) { m_onUnlocking = std::move(cb); }
    /// Fired when unlock curtain finishes (restore focus / clear lock view).
    void setOnUnlocked(std::function<void()> cb) { m_onUnlocked = std::move(cb); }
    /// Leave Lock by resuming the suspended title (lock stays up until exit).
    void setOnResume(std::function<void()> cb) { m_onResume = std::move(cb); }

    void warmTimeFont();
    void show(bool instant = false);
    void hide();

    void setGameArt(std::vector<std::uint8_t> heroRgba) { m_pendingHero = std::move(heroRgba); }
    /// Square title icon (decoded RGBA). Empty clears.
    void setGameIcon(std::vector<std::uint8_t> rgba, int width, int height) {
        m_pendingIcon = std::move(rgba);
        m_pendingIconW = width;
        m_pendingIconH = height;
    }
    void setSuspendedTitle(std::string title, bool canResume) {
        m_suspendedTitle = std::move(title);
        m_canResume = canResume;
    }
    bool hasHero() const { return m_hasHero; }
    bool canResume() const { return m_canResume; }

    void tickRelease(float dt);
    void rearmInput() { m_age = 0.f; }
    void refreshNow() { m_lastMinute = -1; refreshClock(); }
    void handleTouch(nxui::Input& input);
    void requestUnlock() { unlock(); }

    bool isActive() const { return m_active; }
    bool isUnlocking() const { return m_unlocking; }

    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    void wireActions();
    void releaseArt();
    void unlock();
    void resumeGame();
    void beginCurtainUnlock();
    void refreshClock();
    void loadPendingHero();
    void loadPendingIcon();
    void resetSwipe();

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    std::unique_ptr<nxui::Font> m_timeFont;
    nxui::GpuDevice* m_gpu = nullptr;
    nxui::Renderer* m_ren = nullptr;
    std::string m_timeFontPath;

    const nxui::Theme* m_theme = nullptr;
    switchu::services::ClockService* m_clock = nullptr;
    bool m_use12Hour = false;

    std::string m_unlockText;
    std::string m_resumeText;
    std::string m_suspendedTitle;
    std::string m_timeStr = "--:--";
    std::string m_dateStr;
    int m_lastMinute = -1;
    int m_battery = -1;
    bool m_charging = false;
    bool m_canResume = false;

    nxui::Texture m_heroTex;
    std::vector<std::uint8_t> m_pendingHero;
    bool m_hasHero = false;

    nxui::Texture m_iconTex;
    std::vector<std::uint8_t> m_pendingIcon;
    int m_pendingIconW = 0;
    int m_pendingIconH = 0;
    bool m_hasIcon = false;
    float m_releaseTimer = 0.f;

    bool m_active = false;
    bool m_unlocking = false;
    float m_age = 0.f;
    float m_clockTimer = 0.f;
    nxui::AnimatedFloat m_curtain{0.f};
    nxui::AnimatedFloat m_fade{0.f};
    bool m_swipeActive = false;
    bool m_swipeDragging = false;
    bool m_curtainHomeShown = false;
    float m_swipeStartY = 0.f;
    float m_suspendPulse = 0.f;
    BackdropDraw m_onDrawBackdrop;
    std::function<void()> m_onUnlocking;
    std::function<void()> m_onUnlocked;
    std::function<void()> m_onResume;
};
