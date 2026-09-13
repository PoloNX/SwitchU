#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Texture.hpp>
#include <switch/services/acc.h>
#include <algorithm>
#include <functional>


using LaunchCallback = std::function<void(uint64_t titleId, AccountUid uid)>;

class LaunchAnimation : public nxui::Widget {
public:
    LaunchAnimation() = default;

    // Cold launch: zoom to center, hold, fade, then black, then launch.
    void start(const nxui::Rect& from, const nxui::Texture* tex, float cornerRadius,
               const nxui::Color& panelColor, const nxui::Color& borderColor,
               uint64_t titleId, AccountUid uid,
               LaunchCallback onLaunch = {}, nxui::VoidCallback onDone = {});

    // Resume an already-open title: brief fade to black, then onDone.
    void startResume(nxui::VoidCallback onDone);

    bool isPlaying() const { return m_playing; }
    void stop() { m_playing = false; m_tex = nullptr; m_resume = false; }

    float musicFadeProgress() const { // between 0 and 1, where 0 is the initial state
        if (!m_playing) return 1.f;
        const float duration = m_resume ? kResumeTotalDur : kMusicFadeDur;
        return std::min(m_timer / duration, 1.f);
    }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    bool  m_playing  = false;
    bool  m_resume   = false;
    float m_timer    = 0.f;

    static constexpr float kZoomDur   = 0.45f;
    static constexpr float kHoldDur   = 0.35f;
    static constexpr float kFadeDur   = 0.25f;
    static constexpr float kBlackDur  = 0.30f;
    static constexpr float kBlackHold = 0.10f;
    static constexpr float kPostLaunchBlackHold = 0.15f;
    static constexpr float kTotalDur  = kZoomDur + kHoldDur + kFadeDur
                                      + kBlackDur + kBlackHold;
    static constexpr float kMusicFadeDur = kZoomDur + kHoldDur + kFadeDur + kBlackDur;

    // Resume: soft blackout only — no zoom or icon flare.
    static constexpr float kResumeFadeDur = 0.16f;
    static constexpr float kResumeHoldDur = 0.06f;
    static constexpr float kResumeTotalDur = kResumeFadeDur + kResumeHoldDur;

    nxui::Rect     m_from;
    nxui::Rect     m_target;
    float    m_cornerRadius = 24.f;
    const nxui::Texture* m_tex = nullptr;
    nxui::Color   m_panelColor;
    nxui::Color   m_borderColor;
    uint64_t m_titleId = 0;
    AccountUid m_uid = {};
    LaunchCallback m_onLaunch;
    nxui::VoidCallback m_onDone;
    bool    m_launched = false;
    bool    m_doneCalled = false;
    bool    m_postLaunchHold = false;
};
