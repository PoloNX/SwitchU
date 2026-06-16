#include "TutorialActivity.hpp"
#include "core/Config.hpp"
#include "core/DebugLog.hpp"
#include <nxui/Application.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/widgets/Box.hpp>
#include <nxui/widgets/GlassPanel.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

#ifdef SWITCHU_HOMEBREW
static constexpr const char* TUTORIAL_ASSETS = "romfs:";
#else
static constexpr const char* TUTORIAL_ASSETS = "sdmc:/switch/SwitchU";
#endif

namespace {

bool isTextChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0;
}

std::string utf8Codepoint(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string buttonGlyph(nxui::Button button) {
    switch (button) {
        case nxui::Button::A: return utf8Codepoint(0xE0E0);
        case nxui::Button::B: return utf8Codepoint(0xE0E1);
        case nxui::Button::X: return utf8Codepoint(0xE0E2);
        case nxui::Button::Plus: return utf8Codepoint(0xE0F1);
        default: return {};
    }
}

std::string dpadGlyph() {
    return utf8Codepoint(0xE0EA);
}

void configureTutorialGlass(const std::shared_ptr<nxui::GlassPanel>& panel,
                            const nxui::Theme& theme,
                            float radius,
                            float opacity = 0.94f) {
    panel->setCornerRadius(radius);
    panel->setBaseColor(theme.panelBase.withAlpha(0.28f));
    panel->setBorderColor(theme.panelBorder.withAlpha(0.30f));
    panel->setHighlightColor(theme.panelHighlight.withAlpha(0.45f));
    panel->setBorderWidth(1.2f);
    panel->setLiquidGlassEnabled(true);
    panel->setForceLiquidGlass(true);
    panel->setBlurEnabled(false);
    panel->setPanelOpacity(opacity);
    panel->setMaterialTextureEnabled(true);
    panel->setMaterialTextureIntensity(0.28f);
    panel->setLiquidGlassShade(0.025f);
}

void configureFlatPanel(const std::shared_ptr<nxui::GlassPanel>& panel,
                        const nxui::Theme& theme,
                        float radius,
                        float opacity = 0.94f) {
    panel->setCornerRadius(radius);
    panel->setBaseColor(theme.panelBase.withAlpha(0.54f));
    panel->setBorderColor(theme.panelBorder.withAlpha(0.36f));
    panel->setHighlightColor(theme.panelHighlight.withAlpha(0.35f));
    panel->setBorderWidth(1.2f);
    panel->setLiquidGlassEnabled(false);
    panel->setForceLiquidGlass(false);
    panel->setBlurEnabled(false);
    panel->setPanelOpacity(opacity);
    panel->setMaterialTextureEnabled(false);
    panel->setBackingEnabled(true);
    panel->setBackingColor(theme.panelBase.withAlpha(0.78f));
}

} // namespace

TutorialActivity::TutorialActivity(ActivityFactory nextFactory)
    : m_nextFactory(std::move(nextFactory)) {}

bool TutorialActivity::onCreate() {
    DebugLog::log("[tutorial] activity create");

    AppConfig savedConfig;
    savedConfig.load();
    auto& i18n = nxui::I18n::instance();
    i18n.initialize(std::string(TUTORIAL_ASSETS) + "/i18n", "en-US");
    if (savedConfig.uiLanguageOverride == "auto" || savedConfig.uiLanguageOverride.empty())
        i18n.setLanguageAuto();
    else
        i18n.setLanguage(savedConfig.uiLanguageOverride);
    m_accessibilityEnabled = savedConfig.accessibilityEnabled;
    m_accessibility.initialize(m_accessibilityEnabled, i18n.activeLanguageTag(), TUTORIAL_ASSETS);
    m_accessibility.setSpeakHints(savedConfig.accessibilitySpeakHints);
    m_accessibility.setSpeakContextEveryFocus(savedConfig.accessibilitySpeakContextEveryFocus);
    m_accessibility.setSpeechRate(savedConfig.accessibilitySpeechRate);

    const std::string fontPath = std::string(TUTORIAL_ASSETS) + "/fonts/DejaVuSans.ttf";
    m_fontTitle.load(app().gpu(), app().renderer(), fontPath, 34);
    m_fontBody.load(app().gpu(), app().renderer(), fontPath, 25);
    m_fontSmall.load(app().gpu(), app().renderer(), fontPath, 18);
    m_fontIcons.load(app().gpu(), app().renderer(), std::string(TUTORIAL_ASSETS) + "/fonts/switch_icons.ttf", 24);

    m_audio.initialize();
    m_audio.setVolume(0.34f);
    m_audio.loadTrack(std::string(TUTORIAL_ASSETS) + "/sounds/wiiu/music/bg4.mp3");
    m_audio.play();
    m_audio.setSfxVolume(0.52f);
    loadAnimaleseSfx();

    m_theme = nxui::Theme::light();
    buildSteps();
    buildUi();
    m_stepIndex = 0;
    m_visibleChars = 0;
    m_charTimer = 0.f;
    m_blinkTimer = 0.f;
    m_stepTime = 0.f;
    m_finishing = false;
    m_finishTimer = 0.f;
    m_transitionAlpha = 0.f;
    announceCurrentStep();
    return true;
}

void TutorialActivity::onDestroy() {
    m_accessibility.shutdown();
    m_audio.shutdown();
    DebugLog::log("[tutorial] activity destroy");
}

void TutorialActivity::buildSteps() {
    auto& i18n = nxui::I18n::instance();
    m_steps = {
        {
            StepKind::AccessibilityChoice,
            i18n.tr("tutorial.accessibility.title", "Voice guidance"),
            i18n.tr("tutorial.accessibility.body", "Voice guidance is enabled by default. It announces the selected item and the available actions whenever focus changes."),
            i18n.tr("tutorial.accessibility.objective", "Press X to disable the voice and use animalese sounds. Press A to continue.")
        },
        {
            StepKind::Text,
            i18n.tr("tutorial.welcome.title", "Welcome to SwitchU"),
            i18n.tr("tutorial.welcome.body", "SwitchU replaces the HOME menu with a fast interface for launching your games, homebrew apps, and system tools."),
            ""
        },
        {
            StepKind::NavigationTest,
            i18n.tr("tutorial.navigation.title", "Navigation"),
            i18n.tr("tutorial.navigation.body", "You can navigate with the directional pad, left stick, or right stick. Try one horizontal direction and one vertical direction."),
            i18n.tr("tutorial.navigation.objective", "Try left or right, then up or down.")
        },
        {
            StepKind::ConfirmTest,
            i18n.tr("tutorial.confirm.title", "Confirm"),
            i18n.tr("tutorial.confirm.body", "The A button confirms an action, launches a game, or opens the selected item."),
            i18n.tr("tutorial.confirm.objective", "Press A to validate this step.")
        },
        {
            StepKind::Text,
            i18n.tr("tutorial.themes.title", "Themes"),
            i18n.tr("tutorial.themes.body", "Themes can change colors, images, and sounds. Open Themes from the sidebar to install or apply a style."),
            ""
        },
        {
            StepKind::Text,
            i18n.tr("tutorial.cursor.title", "Cursor"),
            i18n.tr("tutorial.cursor.body", "You can use the virtual cursor with stick clicks when an action needs a pointer. Click the left stick to enable it, or the right stick to recenter it."),
            ""
        },
        {
            StepKind::Text,
            i18n.tr("tutorial.support.title", "Support"),
            i18n.tr("tutorial.support.body", "To report an issue, open the project GitHub: github.com/PoloNX/SwitchU"),
            ""
        },
    };
}

void TutorialActivity::loadAnimaleseSfx() {
    static constexpr char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const std::string base = std::string(TUTORIAL_ASSETS) + "/sounds/animalese/";
    for (const char* p = kChars; *p; ++p) {
        std::string id = "animalese_";
        id.push_back(*p);
        m_audio.loadNamedSfx(id, base + std::string(1, *p) + ".wav", 0.45f);
    }
}

void TutorialActivity::buildUi() {
    rootBox().clearChildren();

    m_background = std::make_shared<WaraWaraBackground>();
    m_background->setRect({0, 0, 1280.f, 720.f});
    m_background->setAccentColor(m_theme.backgroundAccent);
    m_background->setSecondaryColor(m_theme.background);
    m_background->setShapeColor(m_theme.shapeColor);
    WaraWaraBackground::Config bgConfig;
    bgConfig.layout = WaraWaraBackground::Layout::Floating;
    bgConfig.shapeSet = WaraWaraBackground::ShapeSet::Mixed;
    bgConfig.shapeCount = 30;
    bgConfig.sizeMin = 14.f;
    bgConfig.sizeMax = 54.f;
    bgConfig.speedMin = 6.f;
    bgConfig.speedMax = 28.f;
    bgConfig.wobble = 16.f;
    bgConfig.opacity = 1.f;
    m_background->setConfig(bgConfig);
    rootBox().addChild(m_background);

    m_progressTrack = std::make_shared<nxui::GlassPanel>();
    configureFlatPanel(m_progressTrack, m_theme, 8.f, 0.72f);
    m_progressTrack->setBaseColor(m_theme.textSecondary.withAlpha(0.16f));
    m_progressTrack->setBackingColor(m_theme.textSecondary.withAlpha(0.12f));
    m_progressTrack->setRect({76.f, 190.f, 520.f, 6.f});
    rootBox().addChild(m_progressTrack);

    m_progressFill = std::make_shared<nxui::GlassPanel>();
    configureFlatPanel(m_progressFill, m_theme, 8.f, 1.f);
    m_progressFill->setBaseColor(nxui::Color(0.0f, 0.36f, 1.0f, 1.f));
    m_progressFill->setBackingColor(nxui::Color(0.0f, 0.36f, 1.0f, 1.f));
    m_progressFill->setBorderColor(nxui::Color(0.0f, 0.36f, 1.0f, 1.f));
    m_progressFill->setRect({76.f, 190.f, 1.f, 6.f});
    rootBox().addChild(m_progressFill);

    m_mainPanel = std::make_shared<nxui::GlassPanel>();
    configureFlatPanel(m_mainPanel, m_theme, m_theme.panelCornerRadius, 0.96f);
    m_mainPanel->setRect({76.f, 228.f, 780.f, 292.f});
    rootBox().addChild(m_mainPanel);

    m_objectivePanel = std::make_shared<nxui::GlassPanel>();
    configureFlatPanel(m_objectivePanel, m_theme, m_theme.panelCornerRadius, 0.96f);
    m_objectivePanel->setRect({884.f, 228.f, 312.f, 292.f});
    m_objectivePanel->setVisible(false);
    rootBox().addChild(m_objectivePanel);

    m_navCursor = std::make_shared<SelectionCursor>();
    m_navCursor->setColor(m_theme.cursorNormal);
    m_navCursor->setBorderWidth(m_theme.cursorBorderWidth);
    m_navCursor->setVisible(false);
    m_navCursor->moveTo({928.f, 352.f, 107.f, 41.f}, 16.f, 0.01f);
}

void TutorialActivity::playAnimaleseFor(char ch) {
    if (m_accessibilityEnabled)
        return;
    if (!isTextChar(ch))
        return;

    std::string id = "animalese_";
    id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    m_audio.playNamedSfx(id);
}

void TutorialActivity::updateTyping(float dt) {
    if (m_steps.empty())
        return;

    const Step& step = m_steps[(size_t)m_stepIndex];
    if (m_visibleChars >= step.body.size())
        return;

    m_charTimer -= dt;
    while (m_charTimer <= 0.f && m_visibleChars < step.body.size()) {
        const char ch = step.body[m_visibleChars++];
        playAnimaleseFor(ch);
        if (ch == '.' || ch == ':' || ch == ';')
            m_charTimer += 0.15f;
        else if (ch == ',' || ch == '!')
            m_charTimer += 0.09f;
        else if (ch == ' ')
            m_charTimer += 0.012f;
        else
            m_charTimer += 0.033f;
    }
}

void TutorialActivity::updateCurrentTest() {
    if (m_steps.empty())
        return;

    Step& step = m_steps[(size_t)m_stepIndex];
    auto& input = app().input();

    if (step.kind == StepKind::NavigationTest) {
        int col = m_navGridIndex % 2;
        int row = m_navGridIndex / 2;
        if (input.isDown(nxui::Button::DLeft) || input.isDown(nxui::Button::DRight) ||
            input.isDown(nxui::Button::LStickL) || input.isDown(nxui::Button::LStickR) ||
            std::abs(input.leftStickX()) > 0.45f) {
            m_navLeftRight = true;
            if (input.isDown(nxui::Button::DLeft) || input.isDown(nxui::Button::LStickL) || input.leftStickX() < -0.45f)
                col = 0;
            if (input.isDown(nxui::Button::DRight) || input.isDown(nxui::Button::LStickR) || input.leftStickX() > 0.45f)
                col = 1;
        }
        if (input.isDown(nxui::Button::DUp) || input.isDown(nxui::Button::DDown) ||
            input.isDown(nxui::Button::LStickU) || input.isDown(nxui::Button::LStickD) ||
            std::abs(input.leftStickY()) > 0.45f) {
            m_navUpDown = true;
            if (input.isDown(nxui::Button::DUp) || input.isDown(nxui::Button::LStickU) || input.leftStickY() > 0.45f)
                row = 0;
            if (input.isDown(nxui::Button::DDown) || input.isDown(nxui::Button::LStickD) || input.leftStickY() < -0.45f)
                row = 1;
        }
        m_navGridIndex = std::clamp(row, 0, 1) * 2 + std::clamp(col, 0, 1);
        step.complete = m_navLeftRight && m_navUpDown;
    } else if (step.kind == StepKind::ConfirmTest) {
        if (input.isDown(nxui::Button::A))
            step.complete = true;
    }
}

void TutorialActivity::advanceStep() {
    if (m_steps.empty())
        return;

    Step& step = m_steps[(size_t)m_stepIndex];
    if (m_visibleChars < step.body.size()) {
        m_visibleChars = step.body.size();
        return;
    }

    if (step.kind != StepKind::Text && step.kind != StepKind::AccessibilityChoice && !step.complete)
        return;

    if (m_stepIndex + 1 >= (int)m_steps.size()) {
        finish();
        return;
    }

    ++m_stepIndex;
    m_visibleChars = 0;
    m_charTimer = 0.f;
    m_blinkTimer = 0.f;
    m_stepTime = 0.f;
    m_navLeftRight = false;
    m_navUpDown = false;
    m_navGridIndex = 0;
    if (m_objectivePanel)
        m_objectivePanel->setVisible(false);
    if (m_navCursor)
        m_navCursor->setVisible(false);
    announceCurrentStep();
}

void TutorialActivity::skipAll() {
    finish();
}

void TutorialActivity::toggleAccessibility() {
    m_accessibilityEnabled = !m_accessibilityEnabled;

    AppConfig config;
    config.load();
    config.accessibilityEnabled = m_accessibilityEnabled;
    config.save();

    if (m_accessibilityEnabled) {
        m_accessibility.setEnabled(true);
        m_accessibility.announce(nxui::I18n::instance().tr(
            "tutorial.accessibility.enabled_announcement",
            "Voice guidance enabled. Press X to disable it, or A to continue."));
    } else {
        m_accessibility.announce(nxui::I18n::instance().tr(
            "tutorial.accessibility.disabled_announcement",
            "Voice guidance disabled. Animalese sounds are active."));
        m_accessibility.setEnabled(false);
    }
}

void TutorialActivity::announceCurrentStep() {
    if (m_steps.empty() || m_stepIndex < 0 || m_stepIndex >= (int)m_steps.size())
        return;

    const Step& step = m_steps[(size_t)m_stepIndex];
    std::string text = step.title + ". " + step.body;
    if (!step.objective.empty())
        text += ". " + step.objective;
    if (step.kind == StepKind::Text)
        text += ". " + nxui::I18n::instance().tr(
            "tutorial.accessibility.text_actions",
            "A to continue. B to skip this step. Plus to skip everything.");
    m_accessibility.announce(text);
}

void TutorialActivity::finish() {
    if (m_finishing)
        return;
    m_finishing = true;
    m_finishTimer = 0.f;

    AppConfig config;
    config.load();
    config.tutorialCompleted = true;
    config.save();
}

void TutorialActivity::completeFinish() {
    if (m_nextFactory)
        app().requestActivity(m_nextFactory(true));
    else
        app().requestExit();
}

void TutorialActivity::onUpdate(float dt) {
    if (m_steps.empty())
        return;

    if (m_finishing) {
        m_finishTimer += dt;
        m_transitionAlpha = std::clamp(m_finishTimer / 0.28f, 0.f, 1.f);
        m_audio.setVolume(0.34f * (1.f - m_transitionAlpha));
        if (m_finishTimer >= 0.30f)
            completeFinish();
        return;
    }

    m_blinkTimer += dt;
    m_stepTime += dt;

    auto& input = app().input();
    if (input.isDown(nxui::Button::Plus)) {
        skipAll();
        return;
    }
    if (input.isDown(nxui::Button::X)) {
        if (m_steps[(size_t)m_stepIndex].kind == StepKind::AccessibilityChoice)
            toggleAccessibility();
        return;
    }
    if (input.isDown(nxui::Button::B)) {
        if (m_visibleChars < m_steps[(size_t)m_stepIndex].body.size())
            m_visibleChars = m_steps[(size_t)m_stepIndex].body.size();
        else {
            m_steps[(size_t)m_stepIndex].complete = true;
            advanceStep();
        }
        return;
    }

    updateTyping(dt);
    updateCurrentTest();

    const Step& step = m_steps[(size_t)m_stepIndex];
    const bool showObjective = step.kind != StepKind::Text
        && step.kind != StepKind::AccessibilityChoice
        && m_visibleChars >= step.body.size();
    if (m_objectivePanel)
        m_objectivePanel->setVisible(showObjective);
    if (m_progressFill) {
        const float progress = (m_stepIndex + 1) / (float)std::max(1, (int)m_steps.size());
        m_progressFill->setRect({76.f, 190.f, std::max(1.f, 520.f * progress), 6.f});
    }

    if (m_navCursor) {
        const bool showNavCursor = step.kind == StepKind::NavigationTest && m_visibleChars >= step.body.size();
        m_navCursor->setVisible(showNavCursor);
        if (showNavCursor && m_objectivePanel) {
            const nxui::Rect objective = m_objectivePanel->rect();
            const nxui::Rect grid{objective.x + 44.f, objective.y + 124.f, 224.f, 92.f};
            const float gap = 10.f;
            const float cellW = (grid.width - gap) * 0.5f;
            const float cellH = (grid.height - gap) * 0.5f;
            const int col = m_navGridIndex % 2;
            const int row = m_navGridIndex / 2;
            nxui::Rect cell{
                grid.x + col * (cellW + gap),
                grid.y + row * (cellH + gap),
                cellW,
                cellH
            };
            m_navCursor->moveTo(cell, 16.f, 0.12f);
            m_navCursor->update(dt);
        }
    }

    if (input.isDown(nxui::Button::A))
        advanceStep();

    nxui::AnimationManager::instance().update(dt);
}

std::vector<std::string> TutorialActivity::wrapText(const std::string& text,
                                                    nxui::Font& font,
                                                    float maxWidth,
                                                    float scale) const {
    std::vector<std::string> lines;
    std::string current;
    std::string word;
    auto flushWord = [&]() {
        if (word.empty())
            return;
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && font.measure(candidate).x * scale > maxWidth) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
        word.clear();
    };

    for (char ch : text) {
        if (ch == ' ') {
            flushWord();
        } else {
            word.push_back(ch);
        }
    }
    flushWord();
    if (!current.empty())
        lines.push_back(current);
    return lines;
}

std::vector<TutorialActivity::ActionHint> TutorialActivity::buildActionHints() const {
    std::vector<ActionHint> hints;
    if (m_steps.empty())
        return hints;

    const Step& step = m_steps[(size_t)m_stepIndex];
    const bool typing = m_visibleChars < step.body.size();
    const bool testPending = step.kind != StepKind::Text && !step.complete && !typing;

    if (step.kind == StepKind::AccessibilityChoice) {
        hints.push_back({buttonGlyph(nxui::Button::X),
                         m_accessibilityEnabled
                            ? nxui::I18n::instance().tr("tutorial.hints.disable_voice", "Disable voice")
                            : nxui::I18n::instance().tr("tutorial.hints.enable_voice", "Enable voice")});
        hints.push_back({buttonGlyph(nxui::Button::A), typing
                         ? nxui::I18n::instance().tr("tutorial.hints.reveal", "Reveal")
                         : nxui::I18n::instance().tr("tutorial.hints.continue", "Continue")});
    } else if (typing)
        hints.push_back({buttonGlyph(nxui::Button::A), nxui::I18n::instance().tr("tutorial.hints.reveal", "Reveal")});
    else if (testPending)
        hints.push_back({dpadGlyph(), nxui::I18n::instance().tr("tutorial.hints.test", "Test")});
    else
        hints.push_back({buttonGlyph(nxui::Button::A), nxui::I18n::instance().tr("tutorial.hints.continue", "Continue")});

    hints.push_back({buttonGlyph(nxui::Button::B), nxui::I18n::instance().tr("tutorial.hints.skip_step", "Skip step")});
    hints.push_back({buttonGlyph(nxui::Button::Plus), nxui::I18n::instance().tr("tutorial.hints.skip_all", "Skip all")});
    return hints;
}

void TutorialActivity::drawHintPanel(nxui::Renderer& ren) {
    std::vector<ActionHint> hints = buildActionHints();
    if (hints.empty())
        return;

    constexpr float kIconScale = 0.66f;
    constexpr float kTextScale = 0.54f;
    constexpr float kRowH = 22.f;
    constexpr float kRowGap = 3.f;
    constexpr float kPadX = 10.f;
    constexpr float kPadY = 8.f;
    constexpr float kIconTextGap = 6.f;
    constexpr float kScreenMargin = 18.f;

    int count = std::min((int)hints.size(), 5);
    float contentW = 0.f;
    for (int i = 0; i < count; ++i) {
        nxui::Vec2 iconSize = m_fontIcons.measure(hints[(size_t)i].icon);
        nxui::Vec2 labelSize = m_fontSmall.measure(hints[(size_t)i].label);
        contentW = std::max(contentW,
                            iconSize.x * kIconScale + kIconTextGap + labelSize.x * kTextScale);
    }

    float panelW = std::clamp(contentW + kPadX * 2.f, 104.f, 210.f);
    float panelH = kPadY * 2.f + count * kRowH + (count - 1) * kRowGap;
    nxui::Rect panel{
        1280.f - kScreenMargin - panelW,
        720.f - kScreenMargin - panelH,
        panelW,
        panelH
    };
    const float radius = 16.f;

    ren.drawRoundedRect(panel, m_theme.panelBase.withAlpha(0.72f), radius);
    ren.drawRoundedRectOutline(panel, m_theme.panelBorder.withAlpha(0.22f), radius, 1.1f);

    ren.pushClipRect(panel.shrunk(3.f));
    float y = panel.y + kPadY;
    for (int i = 0; i < count; ++i) {
        const auto& hint = hints[(size_t)i];
        nxui::Vec2 iconSize = m_fontIcons.measure(hint.icon);
        nxui::Vec2 labelSize = m_fontSmall.measure(hint.label);
        float iconX = panel.x + kPadX;
        float iconY = y + (kRowH - iconSize.y * kIconScale) * 0.5f;
        float labelX = iconX + iconSize.x * kIconScale + 7.f;
        float labelY = y + (kRowH - labelSize.y * kTextScale) * 0.5f;

        ren.drawText(hint.icon, {iconX, iconY}, &m_fontIcons,
                     m_theme.textPrimary.withAlpha(0.88f), kIconScale);
        ren.drawText(hint.label, {labelX, labelY}, &m_fontSmall,
                     m_theme.textSecondary.withAlpha(0.82f), kTextScale);
        y += kRowH + kRowGap;
    }
    ren.popClipRect();
}

void TutorialActivity::drawCheckRow(nxui::Renderer& ren, const std::string& text,
                                    bool complete, float x, float y, float alpha) {
    nxui::Color accent = complete
        ? m_theme.cursorNormal.withAlpha(alpha)
        : m_theme.textSecondary.withAlpha(alpha * 0.70f);
    ren.drawRoundedRect({x, y + 4.f, 18.f, 18.f}, accent.withAlpha(0.18f), 4.f);
    if (complete) {
        ren.drawLine({x + 4.f, y + 13.f}, {x + 8.f, y + 17.f}, accent, 2.2f);
        ren.drawLine({x + 8.f, y + 17.f}, {x + 15.f, y + 8.f}, accent, 2.2f);
    }
    ren.drawText(text, {x + 30.f, y}, &m_fontSmall,
                 m_theme.textSecondary.withAlpha(alpha), 0.76f);
}

void TutorialActivity::drawNavigationGrid(nxui::Renderer& ren) {
    if (!m_objectivePanel)
        return;

    const nxui::Rect objective = m_objectivePanel->rect();
    const nxui::Rect grid{objective.x + 44.f, objective.y + 124.f, 224.f, 92.f};
    const float gap = 10.f;
    const float cellW = (grid.width - gap) * 0.5f;
    const float cellH = (grid.height - gap) * 0.5f;

    for (int i = 0; i < 4; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        nxui::Rect cell{
            grid.x + col * (cellW + gap),
            grid.y + row * (cellH + gap),
            cellW,
            cellH
        };
        ren.drawRoundedRect(cell, m_theme.panelBase.withAlpha(0.30f), 16.f);
        ren.drawRoundedRectOutline(cell, m_theme.panelBorder.withAlpha(0.28f), 16.f, 1.1f);
    }

    if (m_navCursor && m_navCursor->isVisible())
        m_navCursor->render(ren);

    drawCheckRow(ren, "Gauche / droite", m_navLeftRight, objective.x + 34.f, objective.y + 232.f, 1.f);
    drawCheckRow(ren, "Haut / bas", m_navUpDown, objective.x + 34.f, objective.y + 266.f, 1.f);
}

void TutorialActivity::drawMainPanelContent(nxui::Renderer& ren,
                                            const Step& step,
                                            const std::string& visible) {
    if (!m_mainPanel)
        return;

    nxui::Rect panel = m_mainPanel->rect();
    ren.pushClipRect(panel.shrunk(20.f));

    auto lines = wrapText(visible, m_fontBody, panel.width - 68.f, 0.86f);
    float y = panel.y + 38.f;
    for (const auto& line : lines) {
        ren.drawText(line, {panel.x + 34.f, y}, &m_fontBody,
                     m_theme.textPrimary.withAlpha(0.94f), 0.86f);
        y += 38.f;
    }

    if (m_visibleChars < step.body.size() || std::fmod(m_blinkTimer, 0.8f) < 0.42f) {
        float cursorX = panel.x + 34.f;
        float cursorY = y - 30.f;
        if (!lines.empty()) {
            cursorX += m_fontBody.measure(lines.back()).x * 0.86f + 8.f;
            cursorY = y - 38.f;
        }
        ren.drawRoundedRect({cursorX, cursorY + 7.f, 9.f, 24.f},
                            m_theme.cursorNormal.withAlpha(0.92f), 3.f);
    }

    ren.popClipRect();
}

void TutorialActivity::drawObjectivePanelContent(nxui::Renderer& ren, const Step& step) {
    if (!m_objectivePanel || step.kind == StepKind::Text || m_visibleChars < step.body.size())
        return;

    nxui::Rect objective = m_objectivePanel->rect();
    ren.pushClipRect(objective.shrunk(16.f));
    ren.drawText("Test", {objective.x + 24.f, objective.y + 24.f}, &m_fontSmall,
                 m_theme.textSecondary.withAlpha(0.80f), 0.78f);

    auto objectiveLines = wrapText(step.objective, m_fontSmall, objective.width - 48.f, 0.70f);
    float objectiveY = objective.y + 62.f;
    for (const auto& line : objectiveLines) {
        ren.drawText(line, {objective.x + 24.f, objectiveY}, &m_fontSmall,
                     m_theme.textPrimary.withAlpha(0.88f), 0.70f);
        objectiveY += 25.f;
    }

    float cy = objective.y + 148.f;
    if (step.kind == StepKind::NavigationTest) {
        drawNavigationGrid(ren);
    } else if (step.kind == StepKind::ConfirmTest) {
        drawCheckRow(ren, "Bouton A detecte", step.complete, objective.x + 24.f, cy, 1.f);
    }
    ren.popClipRect();
}

void TutorialActivity::onRender(nxui::Renderer& ren) {
    if (m_steps.empty())
        return;

    const Step& step = m_steps[(size_t)m_stepIndex];
    const std::string visible = step.body.substr(0, std::min(m_visibleChars, step.body.size()));

    ren.drawText("Tutoriel SwitchU", {76.f, 50.f}, &m_fontBody,
                 m_theme.textSecondary.withAlpha(0.82f), 0.92f);
    ren.drawText(step.title, {76.f, 128.f}, &m_fontTitle,
                 m_theme.textPrimary, 1.f);

    drawMainPanelContent(ren, step, visible);
    drawObjectivePanelContent(ren, step);
    drawHintPanel(ren);

    if (m_transitionAlpha > 0.001f) {
        float eased = nxui::Easing::outCubic(std::clamp(m_transitionAlpha, 0.f, 1.f));
        ren.drawRect({0.f, 0.f, 1280.f, 720.f}, m_theme.background.withAlpha(eased));
    }
}
