#include "FolderZoom.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>


void FolderZoom::play(const nxui::Rect& from, const nxui::Rect& to, const nxui::Color& tint,
                      float dur, nxui::EasingFunc ease,
                      float radiusFrom, float radiusTo,
                      nxui::VoidCallback onDone)
{
    m_tint    = tint;
    m_onDone  = std::move(onDone);
    m_timer   = 0.f;
    m_dur     = dur;
    m_playing = true;
    setVisible(true);

    m_panel.setImmediate(from);
    m_panel.set(to, dur, ease);
    m_radius.setImmediate(radiusFrom);
    m_radius.set(radiusTo, dur, ease);
    m_progress.setImmediate(0.f);
    m_progress.set(1.f, dur, nxui::Easing::linear);
}

void FolderZoom::open(const nxui::Rect& tile, const nxui::Rect& gridRect,
                      const nxui::Color& tint, nxui::VoidCallback onDone)
{
    play(tile, gridRect, tint, kOpenDur, nxui::Easing::outCubic,
         kTileRadius, kGridRadius, std::move(onDone));
}

void FolderZoom::close(const nxui::Rect& gridRect, const nxui::Rect& tile,
                       const nxui::Color& tint, nxui::VoidCallback onDone)
{
    play(gridRect, tile, tint, kCloseDur, nxui::Easing::inOutCubic,
         kGridRadius, kTileRadius, std::move(onDone));
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
    const float p = std::clamp(m_progress.value(), 0.f, 1.f);
    const float fade = std::pow(1.f - p, 1.6f) * opacity();
    if (fade <= 0.005f)
        return;

    const nxui::Rect r = m_panel.value();
    const float cr = m_radius.value();

    ren.drawRoundedRect(r, nxui::Color(1.f, 1.f, 1.f, 0.10f * fade), cr);
    ren.drawRoundedRectOutline(r.shrunk(2.f), m_tint.withAlpha(0.30f * fade),
                               std::max(4.f, cr - 2.f), 1.f);
    ren.drawRoundedRectOutline(r, nxui::Color::white().withAlpha(0.85f * fade), cr, 2.f);
}
