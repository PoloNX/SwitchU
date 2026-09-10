#include "FolderZoom.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>


void FolderZoom::play(const nxui::Rect& from, const nxui::Rect& to, const nxui::Color& tint,
                      float dur, nxui::EasingFunc ease,
                      float radiusFrom, float radiusTo,
                      float fadeDelay, float fadeDur,
                      nxui::VoidCallback onDone)
{
    m_tint    = tint;
    m_onDone  = std::move(onDone);
    m_timer   = 0.f;
    m_dur     = std::max(dur, fadeDelay + fadeDur);
    m_playing = true;
    setVisible(true);

    m_panel.setImmediate(from);
    m_panel.set(to, dur, ease);
    m_radius.setImmediate(radiusFrom);
    m_radius.set(radiusTo, dur, ease);
    m_alpha.setImmediate(1.f);
    m_alpha.set(0.f, fadeDur, nxui::Easing::outQuad, fadeDelay);
}

void FolderZoom::open(const nxui::Rect& tile, const nxui::Rect& gridRect,
                      const nxui::Color& tint, nxui::VoidCallback onDone)
{
    play(tile, gridRect, tint, kOpenDur, nxui::Easing::outCubic,
         kTileRadius, kGridRadius, kOpenDur * 0.45f, kOpenDur * 0.65f, std::move(onDone));
}

void FolderZoom::close(const nxui::Rect& gridRect, const nxui::Rect& tile,
                       const nxui::Color& tint, nxui::VoidCallback onDone)
{
    play(gridRect, tile, tint, kCloseDur, nxui::Easing::inOutCubic,
         kGridRadius, kTileRadius, kCloseDur - 0.08f, 0.08f, std::move(onDone));
}

void FolderZoom::stop() {
    m_playing = false;
    m_onDone = {};
    setVisible(false);
}

void FolderZoom::onUpdate(float dt) {
    if (!m_playing)
        return;
    m_timer += dt;
    if (m_timer < m_dur)
        return;

    m_playing = false;
    setVisible(false);
    auto done = std::move(m_onDone);
    m_onDone = {};
    if (done) done();
}

void FolderZoom::onRender(nxui::Renderer& ren) {
    if (!m_playing)
        return;
    const float a = m_alpha.value() * opacity();
    if (a <= 0.005f)
        return;

    const nxui::Rect r = m_panel.value();
    const float cr = m_radius.value();

    ren.drawRoundedRect({r.x + 1.f, r.y + 6.f, r.width, r.height},
                        nxui::Color(0.02f, 0.04f, 0.06f, 0.18f * a), cr + 1.f);
    ren.drawRoundedRect(r, m_tint.withAlpha(0.34f * a), cr);
    ren.drawRoundedRect({r.x, r.y, r.width, r.height * 0.4f},
                        nxui::Color(1.f, 1.f, 1.f, 0.05f * a), cr);
    ren.drawRoundedRectOutline(r, m_tint.withAlpha(0.55f * a), cr, 2.f);
}
