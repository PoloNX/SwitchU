#pragma once

#include <nxui/core/Animation.hpp>
#include <nxui/widgets/Widget.hpp>

class FolderBackdrop final : public nxui::Widget {
public:
    // `anchor` is the folder tile: the blurred HOME snapshot pushes in
    // slightly towards it, in step with the folder panel growing out of it.
    void show(bool instant = false, const nxui::Rect& anchor = {}, float dur = 0.28f);
    void hide(float dur = 0.24f);
    bool active() const { return isVisible() || m_opacity.value() > 0.01f; }
    void setCanvasSize(float width, float height) {
        m_canvasW = width > 1.f ? width : 1280.f;
        m_canvasH = height > 1.f ? height : 720.f;
    }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;

private:
    nxui::AnimatedFloat m_opacity;
    nxui::Vec2          m_anchor{640.f, 360.f};
    float               m_canvasW = 1280.f;
    float               m_canvasH = 720.f;
};
