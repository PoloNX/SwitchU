#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/Theme.hpp>
#include <string>
#include <functional>

class GlossyIcon;

class GameInfoPanel : public nxui::GlassWidget {
public:
    GameInfoPanel();
    ~GameInfoPanel();

    void setFont(nxui::Font* f) { m_font = f; }
    void setSmallFont(nxui::Font* f) { m_smallFont = f; }
    void setTheme(const nxui::Theme* t) { m_theme = t; }
    void setI18n(nxui::I18n* i18n) { m_i18n = i18n; }

    void setGame(GlossyIcon* icon);

    void show();
    void hide();
    bool isActive() const { return m_active || m_animating; }
    bool isFullyVisible() const { return m_active && !m_animating; }

    void update(float dt);
    void render(nxui::Renderer& ren);
    void handleTouch(nxui::Input& input);

    using VoidCb = std::function<void()>;
    void onClosed(VoidCb cb) { m_closedCb = std::move(cb); }
    void onNavigateSfx(VoidCb cb) { m_navSfxCb = std::move(cb); }
    void onCloseSfx(VoidCb cb) { m_closeSfxCb = std::move(cb); }

private:
    void setupActions();
    void onPressB();
    void rebuildContent();
    std::string truncateText(const std::string& text, float maxWidth);
    void renderBackgroundEffect(nxui::Renderer& ren, const nxui::Rect& panel, float animScale);
    void renderContent(nxui::Renderer& ren, const nxui::Rect& panel);

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    nxui::I18n* m_i18n = nullptr;

    std::string m_title;
    std::string m_titleDisplay;
    std::string m_version;
    std::string m_sizeText;
    nxui::Texture* m_iconTex = nullptr;

    bool  m_active = false;
    bool  m_animating = false;
    bool  m_showing = false;
    float m_animT = 0.f;
    float m_bgAnimT = 0.f;

    VoidCb m_closedCb;
    VoidCb m_navSfxCb;
    VoidCb m_closeSfxCb;

    static constexpr float kAnimDuration = 0.28f;
    static constexpr float kPanelMargin = 60.f;
    static constexpr float kPanelRadius = 36.f;
    static constexpr float kInnerPad = 32.f;
    static constexpr float kIconSize = 160.f;
    static constexpr float kPanelWidth = 580.f;
    static constexpr float kPanelHeight = 380.f;

    nxui::Rect panelRect() const;
    nxui::Rect panelRect(float scale) const;
};