#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Types.hpp>
#include <string>


class TitlePillWidget : public nxui::GlassWidget {
public:
    TitlePillWidget();

    void setFont(nxui::Font* f)              { m_font = f; }
    void setTextColor(const nxui::Color& c)  { m_textColor = c; }
    // Logical canvas width for centering (1280 landscape, 720 portrait).
    void setCanvasWidth(float width) { m_canvasWidth = width > 0.f ? width : 1280.f; }
    float canvasWidth() const { return m_canvasWidth; }
    // Re-center after orientation change without clearing the label.
    void relayoutForCanvas();

    void setText(const std::string& text, float screenWidth = -1.f);
    void hideAnimated(float screenWidth = -1.f);

protected:
    void onRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;

private:
    nxui::Font*       m_font = nullptr;
    std::string m_sourceText;
    std::string m_text;
    nxui::Color       m_textColor {1.f, 1.f, 1.f, 1.f};
    nxui::AnimatedFloat m_animX{0.f};
    nxui::AnimatedFloat m_animW{0.f};
    nxui::AnimatedFloat m_textReveal{1.f};
    bool m_layoutInitialized = false;
    bool m_hideOnCollapse = false;
    float m_canvasWidth = 1280.f;
};
