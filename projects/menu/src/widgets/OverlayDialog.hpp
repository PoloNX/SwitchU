#pragma once
#include <nxui/widgets/GlassBox.hpp>
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/widgets/Label.hpp>
#include <nxui/widgets/Box.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include "SelectionCursor.hpp"
#include <vector>
#include <string>
#include <functional>

class OverlayDialog : public nxui::GlassWidget {
public:
    struct ButtonDef {
        std::string label;
        std::function<void()> onPress;
        bool closeOnPress = true;
    };

    using CancelCallback = std::function<void()>;
    using VoidCb = std::function<void()>;

    OverlayDialog();

    void setFont(nxui::Font* f)         { m_font = f; }
    void setSmallFont(nxui::Font* f)    { m_smallFont = f; }
    void setTheme(const nxui::Theme* t) { m_theme = t; }

    void show(const std::string& title,
              const std::string& message,
              std::vector<ButtonDef> buttons,
              int initialSelected = 0,
              CancelCallback onCancel = {});

    void hide();

    bool isActive() const { return m_active || m_animatingOut; }

    void handleTouch(nxui::Input& input);

    void onNavigateSfx(VoidCb cb)  { m_navSfxCb = std::move(cb); }
    void onActivateSfx(VoidCb cb)  { m_activateSfxCb = std::move(cb); }
    void onCloseSfx(VoidCb cb)     { m_closeSfxCb = std::move(cb); }

    SelectionCursor& cursor() { return m_cursor; }

    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    void buildWidgetTree();
    void animateButtonFocus(float duration, nxui::EasingFunc easing);
    void setupActions();
    void activateSelected();
    void cancel();
    void syncCursor();
    void syncChildOpacities();

    nxui::Rect panelRect() const;

    nxui::Font*        m_font      = nullptr;
    nxui::Font*        m_smallFont = nullptr;
    const nxui::Theme* m_theme     = nullptr;

    std::string              m_title;
    std::string              m_message;
    std::vector<ButtonDef>   m_buttons;
    int  m_selected     = 0;
    bool m_active       = false;
    bool m_animatingOut = false;

    std::shared_ptr<nxui::Label>                     m_titleLabel;
    std::shared_ptr<nxui::Label>                     m_messageLabel;
    std::shared_ptr<nxui::Box>                       m_buttonRow;
    std::vector<std::shared_ptr<nxui::GlassBox>>  m_btnWidgets;
    SelectionCursor m_cursor;

    nxui::AnimatedFloat m_overlayAlpha;
    nxui::AnimatedFloat m_panelScale;
    nxui::AnimatedFloat m_contentReveal;
    std::vector<nxui::AnimatedFloat> m_buttonFocus;

    float m_panelH = 0.f;
    bool  m_backdropCacheValid = false;
    float m_cachedPreBlurRadius = -1.f;
    int   m_cachedBlurIterations = -1;

    CancelCallback m_onCancel;
    VoidCb         m_navSfxCb;
    VoidCb         m_activateSfxCb;
    VoidCb         m_closeSfxCb;

    int  m_touchHitButton  = -1;
    bool m_touchOnSelected = false;
    bool m_ignoreInitialTouchRelease = false;

    static constexpr float kPanelW       = 560.f;
    static constexpr float kPanelPadX    = 40.f;
    static constexpr float kPanelPadY    = 34.f;
    static constexpr float kPanelRadius  = 26.f;
    static constexpr float kButtonH      = 50.f;
    static constexpr float kButtonRadius = 16.f;
    static constexpr float kButtonGap    = 14.f;
    static constexpr float kTitleMsgGap  = 14.f;
    static constexpr float kMsgBtnGap    = 24.f;
    static constexpr int   kBackdropCacheTarget = 1;
};
