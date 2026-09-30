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
    // 1 = normal dim; lower values keep SteamGridDB hero more visible.
    void setDimStrength(float strength);

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;

private:
    nxui::AnimatedFloat m_opacity;
    nxui::Vec2          m_anchor{640.f, 360.f};
    float               m_dimStrength = 1.f;
};
