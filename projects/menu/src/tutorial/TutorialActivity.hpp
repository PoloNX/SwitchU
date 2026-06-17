#pragma once

#include <nxui/Activity.hpp>
#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/widgets/GlassPanel.hpp>
#include "widgets/SelectionCursor.hpp"
#include "widgets/WaraWaraBackground.hpp"
#include "core/AudioManager.hpp"
#include "core/AccessibilityManager.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class TutorialActivity : public nxui::Activity {
public:
    using ActivityFactory = std::function<std::unique_ptr<nxui::Activity>(bool fromTutorial)>;

    explicit TutorialActivity(ActivityFactory nextFactory);
    ~TutorialActivity() override = default;

    bool onCreate() override;
    void onDestroy() override;
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;
    nxui::Widget* focusRoot() override { return nullptr; }

private:
    enum class StepKind {
        AccessibilityChoice,
        AccessibilitySettings,
        Text,
        NavigationTest,
        ConfirmTest,
    };

    struct Step {
        StepKind kind = StepKind::Text;
        std::string title;
        std::string body;
        std::string objective;
        bool complete = false;
    };

    void buildSteps();
    void loadAnimaleseSfx();
    void buildUi();
    void playAnimaleseFor(char ch);
    void advanceStep();
    void skipAll();
    void finish();
    void completeFinish();
    void toggleAccessibility();
    void announceCurrentStep();
    void updateTyping(float dt);
    void updateCurrentTest();
    std::vector<std::string> wrapText(const std::string& text, nxui::Font& font,
                                      float maxWidth, float scale) const;
    struct ActionHint {
        std::string icon;
        std::string label;
    };
    std::vector<ActionHint> buildActionHints() const;
    void drawHintPanel(nxui::Renderer& ren);
    void drawCheckRow(nxui::Renderer& ren, const std::string& text, bool complete,
                      float x, float y, float alpha);
    void drawNavigationGrid(nxui::Renderer& ren);
    void drawMainPanelContent(nxui::Renderer& ren, const Step& step, const std::string& visible);
    void drawObjectivePanelContent(nxui::Renderer& ren, const Step& step);

    ActivityFactory m_nextFactory;
    AudioManager m_audio;
    AccessibilityManager m_accessibility;
    nxui::Theme m_theme;
    nxui::Font m_fontTitle;
    nxui::Font m_fontBody;
    nxui::Font m_fontSmall;
    nxui::Font m_fontIcons;
    std::shared_ptr<WaraWaraBackground> m_background;
    std::shared_ptr<nxui::GlassPanel> m_mainPanel;
    std::shared_ptr<nxui::GlassPanel> m_objectivePanel;
    std::shared_ptr<nxui::GlassPanel> m_progressTrack;
    std::shared_ptr<nxui::GlassPanel> m_progressFill;
    std::shared_ptr<SelectionCursor> m_navCursor;

    std::vector<Step> m_steps;
    int m_stepIndex = 0;
    size_t m_visibleChars = 0;
    float m_charTimer = 0.f;
    float m_blinkTimer = 0.f;
    float m_stepTime = 0.f;
    bool m_finishing = false;
    float m_finishTimer = 0.f;
    float m_transitionAlpha = 0.f;

    bool m_navLeftRight = false;
    bool m_navUpDown = false;
    int m_navGridIndex = 0;
    bool m_accessibilityEnabled = true;
};
