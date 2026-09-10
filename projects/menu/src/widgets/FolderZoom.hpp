#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Animation.hpp>


class FolderZoom : public nxui::Widget {
public:
    FolderZoom() { setVisible(false); }

    void open(const nxui::Rect& tile, const nxui::Rect& gridRect,
              const nxui::Color& tint, nxui::VoidCallback onDone = {});
    void close(const nxui::Rect& gridRect, const nxui::Rect& tile,
               const nxui::Color& tint, nxui::VoidCallback onDone = {});

    bool isPlaying() const { return m_playing; }
    void stop();

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    void play(const nxui::Rect& from, const nxui::Rect& to, const nxui::Color& tint,
              float dur, nxui::EasingFunc ease,
              float radiusFrom, float radiusTo,
              float fadeDelay, float fadeDur,
              nxui::VoidCallback onDone);

    bool  m_playing = false;
    float m_timer   = 0.f;
    float m_dur     = 0.f;

    nxui::AnimatedRect  m_panel;
    nxui::AnimatedFloat m_radius;
    nxui::AnimatedFloat m_alpha;
    nxui::Color         m_tint;
    nxui::VoidCallback  m_onDone;

    static constexpr float kOpenDur   = 0.28f;
    static constexpr float kCloseDur  = 0.22f;
    static constexpr float kTileRadius = 16.f;
    static constexpr float kGridRadius = 28.f;
};
