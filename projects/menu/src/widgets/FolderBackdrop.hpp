#pragma once

#include <nxui/core/Animation.hpp>
#include <nxui/widgets/Widget.hpp>

class FolderBackdrop final : public nxui::Widget {
public:
    void show(bool instant = false);
    void hide();
    bool active() const { return isVisible() || m_opacity.value() > 0.01f; }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;

private:
    nxui::AnimatedFloat m_opacity;
};
