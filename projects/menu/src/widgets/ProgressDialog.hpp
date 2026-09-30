#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include <functional>
#include <string>

class ProgressDialog : public nxui::GlassWidget {
public:
    ProgressDialog();

    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }

    /// When true, B / A / on-screen Cancel invokes onCancel.
    void setCancellable(bool cancellable, std::function<void()> onCancel = {});

    void show(const std::string& title, const std::string& message, float progress01 = -1.f);
    void updateState(const std::string& message, float progress01);
    void hide();
    void handleTouch(nxui::Input& input);

    bool isActive() const { return m_active || m_animatingOut; }
    bool isCancellable() const { return m_cancellable; }

    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    nxui::Rect panelRect() const;
    nxui::Rect cancelButtonRect(const nxui::Rect& panel) const;
    void wireCancelAction();
    void requestCancel();

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;

    std::string m_title;
    std::string m_message;
    float m_progress01 = -1.f;
    bool m_active = false;
    bool m_animatingOut = false;
    bool m_cancellable = false;
    bool m_cancelRequested = false;
    bool m_touchOnCancel = false;
    std::function<void()> m_onCancel;

    nxui::AnimatedFloat m_overlayAlpha{0.f};
    nxui::AnimatedFloat m_panelScale{0.92f};
    nxui::AnimatedFloat m_progressAnim{0.f};
    float m_spinnerT = 0.f;

    static constexpr float kPanelW = 620.f;
    static constexpr float kPanelH = 300.f;
    static constexpr float kPanelRadius = 26.f;
};
