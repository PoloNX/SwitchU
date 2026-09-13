#include "LaunchAnimation.hpp"
#include <nxui/core/Renderer.hpp>


void LaunchAnimation::start(const nxui::Rect& from, const nxui::Texture* tex, float cornerRadius,
                            const nxui::Color& panelColor, const nxui::Color& borderColor,
                            uint64_t titleId, AccountUid uid,
                            LaunchCallback onLaunch, nxui::VoidCallback onDone)
{
    m_from         = from;
    m_tex          = tex;
    m_cornerRadius = cornerRadius;
    m_panelColor   = panelColor;
    m_borderColor  = borderColor;
    m_titleId      = titleId;
    m_uid          = uid;
    m_onLaunch     = std::move(onLaunch);
    m_onDone       = std::move(onDone);
    m_timer        = 0.f;
    m_playing      = true;
    m_resume       = false;
    m_launched     = false;
    m_doneCalled   = false;
    m_postLaunchHold = false;

    float side = std::min(from.width, from.height) * 1.8f;
    m_target = {(1280 - side) * 0.5f, (720 - side) * 0.5f, side, side};
}

void LaunchAnimation::startResume(nxui::VoidCallback onDone) {
    m_from = {};
    m_target = {};
    m_tex = nullptr;
    m_titleId = 0;
    m_uid = {};
    m_onLaunch = {};
    m_onDone = std::move(onDone);
    m_timer = 0.f;
    m_playing = true;
    m_resume = true;
    m_launched = false;
    m_doneCalled = false;
    m_postLaunchHold = false;
}

void LaunchAnimation::onUpdate(float dt) {
    if (!m_playing) return;
    m_timer += dt;

    if (m_resume) {
        if (m_timer >= kResumeTotalDur) {
            if (!m_doneCalled) {
                m_doneCalled = true;
                if (m_onDone)
                    m_onDone();
                m_postLaunchHold = true;
            }
            // Keep black briefly after the resume command so the menu does not
            // flash back while the applet is handing off.
            if (!m_postLaunchHold || m_timer >= kResumeTotalDur + 0.08f)
                m_playing = false;
        }
        return;
    }

    if (m_timer >= kTotalDur) {
        if (!m_launched) {
            m_launched = true;
            if (m_titleId != 0 && m_onLaunch) {
                m_onLaunch(m_titleId, m_uid);
                m_postLaunchHold = true;
            }
        }
        if (!m_doneCalled) {
            m_doneCalled = true;
            if (m_onDone) {
                m_onDone();
                m_postLaunchHold = true;
            }
        }
        if (!m_postLaunchHold || m_timer >= kTotalDur + kPostLaunchBlackHold) {
            m_playing = false;
        }
    }
}

void LaunchAnimation::onRender(nxui::Renderer& ren) {
    if (!m_playing) return;

    if (m_resume) {
        float alpha = 1.f;
        if (m_timer < kResumeFadeDur)
            alpha = nxui::Easing::outCubic(std::min(m_timer / kResumeFadeDur, 1.f));
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, alpha));
        return;
    }

    float t = m_timer;
    constexpr float kIconEnd = kZoomDur + kHoldDur + kFadeDur;

    if (t >= kIconEnd) {
        float bt = (t - kIconEnd) / kBlackDur;
        float blackAlpha = std::min(bt, 1.f);
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, blackAlpha));
        return;
    }


    float zoomT = std::min(t / kZoomDur, 1.f);
    float ez = nxui::Easing::outCubic(zoomT);

    float x = m_from.x + (m_target.x - m_from.x) * ez;
    float y = m_from.y + (m_target.y - m_from.y) * ez;
    float w = m_from.width  + (m_target.width  - m_from.width)  * ez;
    float h = m_from.height + (m_target.height - m_from.height) * ez;
    float cr = m_cornerRadius * (1.f + 0.3f * ez);

    float pulseScale = 1.f;
    if (t > kZoomDur && t < kZoomDur + kHoldDur) {
        float pt = (t - kZoomDur) / kHoldDur;
        pulseScale = 1.f + 0.04f * std::sin(pt * 3.14159f * 2.f);
    }

    float fadeAlpha = 1.f;
    if (t > kZoomDur + kHoldDur) {
        float ft = (t - kZoomDur - kHoldDur) / kFadeDur;
        fadeAlpha = 1.f - nxui::Easing::outCubic(std::min(ft, 1.f));
    }

    float pw = w * pulseScale;
    float ph = h * pulseScale;
    float px = x + (w - pw) * 0.5f;
    float py = y + (h - ph) * 0.5f;
    nxui::Rect animRect = {px, py, pw, ph};

    float dimAlpha = 0.5f * ez * fadeAlpha;
    ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, dimAlpha));

    nxui::Color body = m_panelColor.withAlpha(m_panelColor.a * fadeAlpha);
    ren.drawRoundedRect(animRect, body, cr);

    nxui::Rect shine = {animRect.x, animRect.y, animRect.width, animRect.height * 0.4f};
    ren.drawRoundedRect(shine, nxui::Color(1, 1, 1, 0.06f * fadeAlpha), cr);

    nxui::Color border = m_borderColor.withAlpha(m_borderColor.a * fadeAlpha);
    ren.drawRoundedRectOutline(animRect, border, cr, 1.f);

    if (m_tex && m_tex->valid()) {
        nxui::Rect texRect = animRect.shrunk(8.f);
        ren.drawTextureRounded(m_tex, texRect, cr - 4.f,
                               nxui::Color::white().withAlpha(fadeAlpha));
    }
}
