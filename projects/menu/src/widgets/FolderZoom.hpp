#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Animation.hpp>


// Tinted glass panel behind an open folder. It grows out of the folder tile,
// stays up while the folder is open, and shrinks back into the tile on close.
class FolderZoom : public nxui::Widget {
public:
    FolderZoom() { setVisible(false); }

    void open(const nxui::Rect& tile, const nxui::Rect& panel,
              const nxui::Color& tint, nxui::VoidCallback onDone = {});
    void close(const nxui::Rect& tile, const nxui::Color& tint,
               nxui::VoidCallback onDone = {});
    void showStatic(const nxui::Rect& panel, const nxui::Color& tint);
    // Follow a folder grid relayout; only affects a settled, open panel.
    void retarget(const nxui::Rect& panel);
    void hide();

    bool isPlaying() const { return m_state == State::Opening || m_state == State::Closing; }

    static constexpr float kOpenDur  = 0.38f;
    static constexpr float kCloseDur = 0.28f;

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    enum class State { Hidden, Opening, Open, Closing };

    void play(State state, const nxui::Rect& from, const nxui::Rect& to,
              float dur, nxui::EasingFunc ease,
              float radiusFrom, float radiusTo,
              nxui::VoidCallback onDone);

    State m_state = State::Hidden;
    float m_timer = 0.f;
    float m_dur   = 0.f;

    nxui::AnimatedRect  m_panel;
    nxui::AnimatedFloat m_radius;
    nxui::AnimatedFloat m_progress;
    nxui::Color         m_tint;
    nxui::VoidCallback  m_onDone;

    static constexpr float kTileRadius = 16.f;
    static constexpr float kGridRadius = 28.f;
};
