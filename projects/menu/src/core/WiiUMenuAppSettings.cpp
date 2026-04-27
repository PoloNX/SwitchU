#include "WiiUMenuApp.hpp"
#include "widgets/GlossyIcon.hpp"
#include "DebugLog.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <utility>

void WiiUMenuApp::createSettings() {
    if (m_settings) return;

    m_settings = std::make_shared<SettingsScreen>();
    if (m_overlayLayer) {
        m_overlayLayer->addChild(m_settings);
    }
    m_settings->setFont(&m_fontNormal);
    m_settings->setSmallFont(&m_fontSmall);
    m_settings->setTheme(&m_theme);
    m_settings->setMusicState(m_audio.isPlaying(), m_audio.volume(), m_audio.sfxVolume());
    m_settings->setWireframeState(m_showWireframe);
    m_settings->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_settings->setUiLanguageOverride(m_config.uiLanguageOverride);
    m_settings->setSoundPresetState(m_config.soundPreset, m_availablePresets);

    {
        std::vector<std::string> presetNames;
        for (auto& p : m_allPresets) presetNames.push_back(p.name);
        m_settings->setThemePresetState(m_activePresetName, presetNames, m_activeColors,
                                        m_activeMode == nxui::ThemeMode::Dark);
    }
    m_settings->warmup();

    m_settings->onMusicEnabledChange([this](bool enabled) {
        if (enabled) m_audio.play(); else m_audio.stop();
        m_config.musicEnabled = enabled;
    });
    m_settings->onMusicVolumeChange([this](float v) {
        m_audio.setVolume(v);
        m_config.musicVolume = v;
    });
    m_settings->onSfxVolumeChange([this](float v) {
        m_audio.setSfxVolume(v);
        m_config.sfxVolume = v;
    });
    m_settings->onNextTrack([this]() {
        m_audio.nextTrack();
        m_audio.playSfx(Sfx::ConfirmPositive);
    });
    m_settings->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_settings->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_settings->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_settings->onToggleSfx([this](bool on) {
        m_audio.playSfx(on ? Sfx::ThemeToggle : Sfx::ToggleOff);
    });
    m_settings->onSliderSfx([this](bool up) {
        m_audio.playSfx(up ? Sfx::SliderUp : Sfx::SliderDown);
    });
    m_settings->onWireframeChange([this](bool enabled) {
        m_showWireframe = enabled;
        app().renderer().setBoxWireframeEnabled(enabled);
    });
    m_settings->onGridColumnsChange([this](int cols) {
        cols = std::clamp(cols, 3, 8);
        if (m_config.gridColumns == cols)
            return;
        m_config.gridColumns = cols;
        m_refreshQueued = true;
        m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 1);
    });
    m_settings->onGridRowsChange([this](int rows) {
        rows = std::clamp(rows, 2, 5);
        if (m_config.gridRows == rows)
            return;
        m_config.gridRows = rows;
        m_refreshQueued = true;
        m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 1);
    });
    m_settings->onUiLanguageChange([this](const std::string& tag) {
        m_config.uiLanguageOverride = tag;
        if (m_settings) m_settings->setUiLanguageOverride(tag);
        applyUiLanguage();
        app().gpu().waitIdle();
        m_fontNormal.clearCache();
        m_fontSmall.clearCache();
        m_settingsNeedRefresh = true;
    });
    m_settings->onSoundPresetChange([this](const std::string& preset) {
        changeSoundPreset(preset);
        m_config.soundPreset = preset;
    });
    m_settings->onThemePresetChange([this](const std::string& name) {
        ThemePreset* preset = findPresetPtr(name);
        if (!preset) return;
        m_activePresetName = name;
        m_activeColors = preset->colors;
        m_activeMode = preset->mode;
        m_config.themePreset = name;
        m_config.themeMode = "";
        m_config.accentH = m_config.accentS = m_config.accentL = -1.f;
        m_config.bgH     = m_config.bgS     = m_config.bgL     = -1.f;
        m_config.bgAccH  = m_config.bgAccS  = m_config.bgAccL  = -1.f;
        m_config.shapeH  = m_config.shapeS  = m_config.shapeL  = -1.f;
        rebuildThemeFromColors();
        m_settings->updateThemeSliders(m_activeColors);
        m_audio.playSfx(Sfx::ThemeToggle);
    });
    m_settings->onThemeColorChange([this](const std::string& key, float value) {
        if      (key == "accent_h")  { m_activeColors.accentH = value; m_config.accentH = value; }
        else if (key == "accent_s")  { m_activeColors.accentS = value; m_config.accentS = value; }
        else if (key == "accent_l")  { m_activeColors.accentL = value; m_config.accentL = value; }
        else if (key == "bg_h")      { m_activeColors.bgH     = value; m_config.bgH = value; }
        else if (key == "bg_s")      { m_activeColors.bgS     = value; m_config.bgS = value; }
        else if (key == "bg_l")      { m_activeColors.bgL     = value; m_config.bgL = value; }
        else if (key == "bg_acc_h")  { m_activeColors.bgAccH  = value; m_config.bgAccH = value; }
        else if (key == "bg_acc_s")  { m_activeColors.bgAccS  = value; m_config.bgAccS = value; }
        else if (key == "bg_acc_l")  { m_activeColors.bgAccL  = value; m_config.bgAccL = value; }
        else if (key == "shape_h")   { m_activeColors.shapeH  = value; m_config.shapeH = value; }
        else if (key == "shape_s")   { m_activeColors.shapeS  = value; m_config.shapeS = value; }
        else if (key == "shape_l")   { m_activeColors.shapeL  = value; m_config.shapeL = value; }
        rebuildThemeFromColors();
    });
    m_settings->onThemeReset([this]() {
        ThemePreset* preset = findPresetPtr(m_activePresetName);
        if (!preset) return;
        m_activeColors = preset->colors;
        m_config.accentH = m_config.accentS = m_config.accentL = -1.f;
        m_config.bgH     = m_config.bgS     = m_config.bgL     = -1.f;
        m_config.bgAccH  = m_config.bgAccS  = m_config.bgAccL  = -1.f;
        m_config.shapeH  = m_config.shapeS  = m_config.shapeL  = -1.f;
        rebuildThemeFromColors();
        m_settings->updateThemeSliders(m_activeColors);
        m_audio.playSfx(Sfx::ThemeToggle);
    });
    m_settings->onThemeSave([this]() {
        auto userPresets = ThemePreset::loadUserPresets();
        int num = (int)userPresets.size() + 1;
        std::string name = "Custom " + std::to_string(num);
        while (findPresetPtr(name)) {
            ++num;
            name = "Custom " + std::to_string(num);
        }

        ThemePreset* base = findPresetPtr(m_activePresetName);
        ThemePreset newPreset;
        newPreset.name    = name;
        newPreset.mode    = base ? base->mode : nxui::ThemeMode::Dark;
        newPreset.colors  = m_activeColors;
        newPreset.builtIn = false;

        userPresets.push_back(newPreset);
        ThemePreset::saveUserPresets(userPresets);

        m_allPresets.push_back(newPreset);
        m_activePresetName = name;
        m_config.themePreset = name;
        m_config.accentH = m_config.accentS = m_config.accentL = -1.f;
        m_config.bgH     = m_config.bgS     = m_config.bgL     = -1.f;
        m_config.bgAccH  = m_config.bgAccS  = m_config.bgAccL  = -1.f;
        m_config.shapeH  = m_config.shapeS  = m_config.shapeL  = -1.f;

        std::vector<std::string> names;
        for (auto& p : m_allPresets) names.push_back(p.name);
        m_settings->updateThemePresetList(names, m_activePresetName);
        m_audio.playSfx(Sfx::ConfirmPositive);
    });
    m_settings->onThemeManage([this]() {
        auto& i18n = nxui::I18n::instance();
        ThemePreset* preset = findPresetPtr(m_activePresetName);
        if (!preset || preset->builtIn) {
            m_dialogReturnFocus = focusManager().current();
            m_dialog->show(
                i18n.tr("settings.theme.delete_preset", "Delete Preset"),
                i18n.tr("settings.theme.builtin_readonly", "Built-in presets cannot be modified."),
                {{ i18n.tr("button.ok", "OK"), {}, true }});
            focusManager().setFocus(m_dialog.get());
            return;
        }
        deleteActivePreset();
    });
    m_settings->onThemeModeChange([this](bool dark) {
        m_activeMode = dark ? nxui::ThemeMode::Dark : nxui::ThemeMode::Light;
        m_config.themeMode = dark ? "dark" : "light";
        rebuildThemeFromColors();
        m_audio.playSfx(Sfx::ThemeToggle);
    });
    m_settings->onNetConnect([this]() {
        m_pendingNetConnect = true;
        m_settings->hide();
    });
    m_settings->onDialogRequest([this](const std::string& title,
                                       const std::string& msg,
                                       std::vector<SettingsScreen::DialogButtonDef> buttons) {
        if (!m_dialog) return;
        std::vector<OverlayDialog::ButtonDef> dlgButtons;
        bool preserveReturnFocus = (m_dialog && m_dialog->isActive() && focusManager().current() == m_dialog.get() && m_dialogReturnFocus != nullptr);
        for (size_t i = 0; i < buttons.size(); ++i) {
            auto cb = buttons[i].onPress;
            bool isLast = (i == buttons.size() - 1);
            if (buttons.size() == 1) {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            } else if (isLast) {
                dlgButtons.push_back({buttons[i].label, [cb]() { if (cb) cb(); }, true});
            } else {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            }
        }
        if (!preserveReturnFocus)
            m_dialogReturnFocus = focusManager().current();
        m_dialog->show(title, msg, std::move(dlgButtons));
        focusManager().setFocus(m_dialog.get());
    });
    m_settings->onClosed([this]() {
        m_threadPool.submit([cfg = m_config]() {
            cfg.save();
        });
        DebugLog::log("[config] save queued");
        if (isCurrentFocusableWidget(m_sidebar.settingsButton())) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_sidebar.settingsButton());
        }
    });
}

void WiiUMenuApp::applyUiLanguage() {
    auto& i18n = nxui::I18n::instance();
    if (m_config.uiLanguageOverride == "auto" || m_config.uiLanguageOverride.empty())
        i18n.setLanguageAuto();
    else
        i18n.setLanguage(m_config.uiLanguageOverride);
}

void WiiUMenuApp::applyTheme() {
    m_background->setAccentColor(m_theme.backgroundAccent);
    m_background->setSecondaryColor(m_theme.background);
    m_background->setShapeColor(m_theme.shapeColor);

    for (auto& icon : m_grid->allIcons()) {
        icon->setBaseColor(m_theme.iconDefault);
        icon->setBorderColor(m_theme.panelBorder);
        icon->setHighlightColor(m_theme.panelHighlight);
        icon->setCornerRadius(m_theme.iconCornerRadius);
    }

    m_cursor->setColor(m_theme.cursorNormal);
    m_cursor->setCornerRadius(m_theme.cursorCornerRadius);
    m_cursor->setBorderWidth(m_theme.cursorBorderWidth);
    if (m_pointerCursor) {
        m_pointerCursor->setColor(m_theme.cursorNormal);
        m_pointerCursor->setCornerRadius(15.f);
        m_pointerCursor->setBorderWidth(2.5f);
    }

    m_clock->setBaseColor(m_theme.panelBase);
    m_clock->setBorderColor(m_theme.panelBorder);
    m_clock->setHighlightColor(m_theme.panelHighlight);
    m_clock->setTextColor(m_theme.textPrimary);
    m_clock->setSecondaryTextColor(m_theme.textSecondary);

    m_battery->setBaseColor(m_theme.panelBase);
    m_battery->setBorderColor(m_theme.panelBorder);
    m_battery->setHighlightColor(m_theme.panelHighlight);
    m_battery->setTextColor(m_theme.textPrimary);

    m_titlePill->setBaseColor(m_theme.panelBase);
    m_titlePill->setBorderColor(m_theme.panelBorder);
    m_titlePill->setHighlightColor(m_theme.panelHighlight);
    m_titlePill->setTextColor(m_theme.textPrimary);

    m_pageIndicator->setBaseColor(m_theme.panelBase);
    m_pageIndicator->setBorderColor(m_theme.panelBorder);
    m_pageIndicator->setHighlightColor(m_theme.panelHighlight);
    m_pageIndicator->setTheme(&m_theme);

    m_userSelect->panel().setBaseColor(m_theme.panelBase);
    m_userSelect->panel().setBorderColor(m_theme.panelBorder);
    m_userSelect->panel().setHighlightColor(m_theme.panelHighlight);
    m_userSelect->panel().setLiquidGlassEnabled(true);
    m_userSelect->panel().setPanelOpacity(1.5f);
    m_userSelect->panel().setBlurEnabled(true);
    m_userSelect->titlePanel().setBaseColor(m_theme.panelBase);
    m_userSelect->titlePanel().setBorderColor(m_theme.panelBorder);
    m_userSelect->titlePanel().setHighlightColor(m_theme.panelHighlight);
    m_userSelect->titlePanel().setLiquidGlassEnabled(true);
    m_userSelect->titlePanel().setPanelOpacity(1.5f);
    m_userSelect->titlePanel().setBlurEnabled(true);
    m_userSelect->setTextColor(m_theme.textPrimary);
    m_userSelect->setSecondaryTextColor(m_theme.textSecondary);
    m_userSelect->cursor().setColor(m_theme.cursorNormal);

    if (m_dialog) {
        m_dialog->setTheme(&m_theme);
        m_dialog->setBaseColor(m_theme.panelBase);
        m_dialog->setBorderColor(m_theme.panelBorder);
        m_dialog->setHighlightColor(m_theme.panelHighlight);
        m_dialog->cursor().setColor(m_theme.cursorNormal);
    }

    if (m_settings)
        m_settings->setTheme(&m_theme);

    m_sidebar.applyTheme(m_theme);
}

void WiiUMenuApp::rebuildThemeFromColors() {
    ThemePreset effective;
    effective.mode   = m_activeMode;
    effective.colors = m_activeColors;
    m_theme = effective.toTheme();
    applyTheme();
}

ThemePreset* WiiUMenuApp::findPresetPtr(const std::string& name) {
    for (auto& p : m_allPresets)
        if (p.name == name) return &p;
    return nullptr;
}

void WiiUMenuApp::deleteActivePreset() {
    std::string nameToDelete = m_activePresetName;

    m_allPresets.erase(
        std::remove_if(m_allPresets.begin(), m_allPresets.end(),
            [&](const ThemePreset& p) { return p.name == nameToDelete; }),
        m_allPresets.end());

    auto userPresets = ThemePreset::loadUserPresets();
    userPresets.erase(
        std::remove_if(userPresets.begin(), userPresets.end(),
            [&](const ThemePreset& p) { return p.name == nameToDelete; }),
        userPresets.end());
    ThemePreset::saveUserPresets(userPresets);

    m_activePresetName = "Default Dark";
    m_config.themePreset = "Default Dark";
    m_config.accentH = m_config.accentS = m_config.accentL = -1.f;
    m_config.bgH     = m_config.bgS     = m_config.bgL     = -1.f;
    m_config.bgAccH  = m_config.bgAccS  = m_config.bgAccL  = -1.f;
    m_config.shapeH  = m_config.shapeS  = m_config.shapeL  = -1.f;

    ThemePreset* fallback = findPresetPtr("Default Dark");
    if (fallback) {
        m_activeColors = fallback->colors;
        m_activeMode = fallback->mode;
        m_config.themeMode = "";
    }
    rebuildThemeFromColors();

    std::vector<std::string> names;
    for (auto& p : m_allPresets) names.push_back(p.name);
    m_settings->updateThemePresetList(names, m_activePresetName);
    m_settings->updateThemeSliders(m_activeColors);
    m_audio.playSfx(Sfx::ConfirmPositive);
}
