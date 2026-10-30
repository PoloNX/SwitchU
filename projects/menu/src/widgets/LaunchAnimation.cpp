#include "LaunchAnimation.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace {

nxui::Rect centeredCoverSource(const nxui::Texture& texture,
                               const nxui::Rect& destination) {
    const float sourceWidth = static_cast<float>(texture.width());
    const float sourceHeight = static_cast<float>(texture.height());
    if (sourceWidth <= 0.f || sourceHeight <= 0.f ||
        destination.width <= 0.f || destination.height <= 0.f)
        return {0.f, 0.f, sourceWidth, sourceHeight};

    const float sourceAspect = sourceWidth / sourceHeight;
    const float targetAspect = destination.width / destination.height;
    if (sourceAspect > targetAspect) {
        const float croppedWidth = sourceHeight * targetAspect;
        return {(sourceWidth - croppedWidth) * 0.5f, 0.f,
                croppedWidth, sourceHeight};
    }
    const float croppedHeight = sourceWidth / targetAspect;
    return {0.f, (sourceHeight - croppedHeight) * 0.5f,
            sourceWidth, croppedHeight};
}

nxui::Rect posterFrameInTile(const nxui::Rect& tile) {
    // Match single-row poster cells: 2:3, centered inside the source tile.
    constexpr float kPosterAspect = 2.f / 3.f;
    if (tile.width <= 1.f || tile.height <= 1.f)
        return tile;
    float w = tile.width;
    float h = w / kPosterAspect;
    if (h > tile.height) {
        h = tile.height;
        w = h * kPosterAspect;
    }
    return {tile.x + (tile.width - w) * 0.5f,
            tile.y + (tile.height - h) * 0.5f, w, h};
}

} // namespace


void LaunchAnimation::start(const nxui::Rect& from, const nxui::Texture* tex, float cornerRadius,
                            const nxui::Color& panelColor, const nxui::Color& borderColor,
                            uint64_t titleId, AccountUid uid,
                            LaunchCallback onLaunch, nxui::VoidCallback onDone,
                            bool preservePosterAspect)
{
    m_from         = preservePosterAspect ? posterFrameInTile(from) : from;
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

    // Keep the source aspect (square icons stay square; posters stay 2:3).
    // Posters get a larger zoom so they can cover the clock and title caption.
    const bool poster = preservePosterAspect
        || m_from.height > m_from.width * 1.15f;
    const float zoomScale = poster ? 2.55f : 1.8f;
    const float maxW = 1280.f * (poster ? 0.42f : 0.55f);
    const float maxH = 720.f * (poster ? 0.92f : 0.72f);
    float tw = std::max(1.f, m_from.width) * zoomScale;
    float th = std::max(1.f, m_from.height) * zoomScale;
    const float fit = std::min(1.f, std::min(maxW / tw, maxH / th));
    tw *= fit;
    th *= fit;
    m_target = {(1280.f - tw) * 0.5f, (720.f - th) * 0.5f, tw, th};
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
        // Cover-fit like GlossyIcon posters — never stretch into the frame.
        ren.drawTextureSubRounded(
            m_tex, centeredCoverSource(*m_tex, texRect), texRect, cr - 4.f,
            nxui::Color::white().withAlpha(fadeAlpha));
    }
}
