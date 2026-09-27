#include "FolderZoom.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace {

// outBack with a much smaller overshoot: the panel settles with a hint of
// spring instead of visibly bouncing past the grid.
float outBackSoft(float t) {
    const float c1 = 0.9f, c3 = c1 + 1.f;
    return 1.f + c3 * std::pow(t - 1.f, 3.f) + c1 * std::pow(t - 1.f, 2.f);
}

} // namespace

void FolderZoom::play(State state, const nxui::Rect& from, const nxui::Rect& to,
                      float dur, nxui::EasingFunc ease,
                      float radiusFrom, float radiusTo,
                      nxui::VoidCallback onDone)
{
    m_state  = state;
    m_onDone = std::move(onDone);
    m_timer  = 0.f;
    m_dur    = dur;
    setVisible(true);

    m_panel.setImmediate(from);
    m_panel.set(to, dur, ease);
    m_radius.setImmediate(radiusFrom);
    m_radius.set(radiusTo, dur, ease);
    m_progress.setImmediate(0.f);
    m_progress.set(1.f, dur, nxui::Easing::linear);
}

void FolderZoom::open(const nxui::Rect& tile, const nxui::Rect& panel,
                      const nxui::Color& tint, nxui::VoidCallback onDone)
{
    m_tint = tint;
    play(State::Opening, tile, panel, kOpenDur, outBackSoft,
         kTileRadius, kGridRadius, std::move(onDone));
}

void FolderZoom::close(const nxui::Rect& tile, const nxui::Color& tint,
                       nxui::VoidCallback onDone)
{
    m_tint = tint;
    play(State::Closing, m_panel.value(), tile, kCloseDur, nxui::Easing::inOutCubic,
         m_radius.value(), kTileRadius, std::move(onDone));
}

void FolderZoom::showStatic(const nxui::Rect& panel, const nxui::Color& tint) {
    m_tint   = tint;
    m_state  = State::Open;
    m_onDone = {};
    m_panel.setImmediate(panel);
    m_radius.setImmediate(kGridRadius);
    m_progress.setImmediate(1.f);
    setVisible(true);
}

void FolderZoom::retarget(const nxui::Rect& panel) {
    if (m_state == State::Open)
        m_panel.set(panel, 0.22f, nxui::Easing::outCubic);
}

void FolderZoom::hide() {
    m_state  = State::Hidden;
    m_onDone = {};
    setVisible(false);
}

void FolderZoom::onUpdate(float dt) {
    if (!isPlaying())
        return;
    m_timer += dt;
    if (m_timer < m_dur)
        return;

    if (m_state == State::Opening) {
        m_state = State::Open;
    } else {
        m_state = State::Hidden;
        setVisible(false);
    }
    auto done = std::move(m_onDone);
    m_onDone = {};
    if (done) done();
}

void FolderZoom::onRender(nxui::Renderer& ren) {
    if (m_state == State::Hidden)
        return;
    const float p = std::clamp(m_progress.value(), 0.f, 1.f);
    float fade = 1.f;
    if (m_state == State::Opening)
        fade = std::min(1.f, p / 0.15f);          // grow out of the tile, no pop
    else if (m_state == State::Closing)
        fade = std::min(1.f, (1.f - p) / 0.25f);  // melt into the tile
    fade *= opacity();
    if (fade <= 0.005f)
        return;

    const nxui::Rect r = m_panel.value();
    const float cr = m_radius.value();

    if (ren.gpu().offscreenReady())
        ren.drawLiquidGlass(2, r, cr, m_tint.withAlpha(0.22f), fade, 0.08f);
    else
        ren.drawRoundedRect(r, m_tint.withAlpha(0.28f * fade), cr);
    ren.drawRoundedRect(r, nxui::Color(1.f, 1.f, 1.f, 0.04f * fade), cr);
    ren.drawRoundedRectOutline(r.shrunk(1.5f), m_tint.withAlpha(0.35f * fade),
                               std::max(4.f, cr - 1.5f), 1.5f);
    ren.drawRoundedRectOutline(r, nxui::Color::white().withAlpha(0.38f * fade), cr, 1.f);
}
