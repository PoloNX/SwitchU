#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include <functional>
#include <string>
#include <vector>

class ControllerTestScreen final : public nxui::GlassWidget {
public:
    using VoidCb = std::function<void()>;
    using StringCb = std::function<void(const std::string&)>;

    ControllerTestScreen();
    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setInput(nxui::Input* input) { m_input = input; }
    void onClosed(VoidCb cb) { m_closedCb = std::move(cb); }
    void onCloseSfx(VoidCb cb) { m_closeSfxCb = std::move(cb); }
    void onAccessibilityAnnouncement(StringCb cb) { m_accessibilityCb = std::move(cb); }

    void show();
    void hide();
    bool isActive() const { return m_active || m_animatingOut; }
    void handleTouch(nxui::Input& input);

    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    enum class ActiveStick { None, Left, Right };
    struct ButtonVisual { nxui::Button button; const char* label; };

    nxui::Rect panelRect(float scale = 1.f) const;
    nxui::Rect touchPanelRect(float scale = 1.f) const;
    void setupActions();
    void drawStickPanel(nxui::Renderer& ren, const nxui::Rect& rect, float alpha);
    void drawButtonsPanel(nxui::Renderer& ren, const nxui::Rect& rect, float alpha);
    void drawTouchPanel(nxui::Renderer& ren, const nxui::Rect& rect, float alpha);
    void drawFullscreenTouch(nxui::Renderer& ren, float alpha);
    void enterTouchFullscreen();
    void exitTouchFullscreen();
    void recordTouchPoint(float x, float y, const nxui::Rect& surface);
    void drawTouchTrace(nxui::Renderer& ren, const nxui::Rect& surface, float alpha, float thickness);
    std::string directionLabel(float x, float y) const;

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    nxui::Input* m_input = nullptr;
    bool m_active = false;
    bool m_animatingOut = false;
    nxui::AnimatedFloat m_alpha;
    nxui::AnimatedFloat m_scale;
    ActiveStick m_activeStick = ActiveStick::None;
    ActiveStick m_candidateStick = ActiveStick::None;
    float m_candidateTime = 0.f;
    float m_smoothX = 0.f;
    float m_smoothY = 0.f;
    bool m_recordingTouch = false;
    bool m_touchFullscreen = false;
    float m_bHoldTime = 0.f;
    nxui::AnimatedFloat m_touchFullscreenAnim;
    std::vector<nxui::Vec2> m_touchTrace;
    VoidCb m_closedCb;
    VoidCb m_closeSfxCb;
    StringCb m_accessibilityCb;
};
