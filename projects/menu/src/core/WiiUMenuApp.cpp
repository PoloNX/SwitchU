#include "WiiUMenuApp.hpp"
#include "widgets/GlossyIcon.hpp"
#include <nxui/core/Animation.hpp>
#include <nxui/core/I18n.hpp>
#include "DebugLog.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include <switch.h>
#if !defined(SWITCHU_HOMEBREW) && !defined(SWITCHU_MENU)
#include <nxtc.h>
#endif
#ifdef SWITCHU_MENU
#include "ipc_client.hpp"
#include "smi_commands.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>


WiiUMenuApp::WiiUMenuApp() {}
WiiUMenuApp::~WiiUMenuApp() {
    rootBox().clearChildren();
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::setStartupStatus(uint64_t suspendedTitleId, bool appRunning) {
    m_launcher.setStartupStatus(suspendedTitleId, appRunning);
}
#endif

bool WiiUMenuApp::onCreate() {
    m_config.load();

    nxui::I18n::instance().initialize(std::string(SD_ASSETS) + "/i18n", "en-US");
    applyUiLanguage();

    m_audioFuture = m_threadPool.submit([this]() {
        m_audio.initialize();
        m_availablePresets = scanAvailablePresets();
        loadSoundPreset(m_config.soundPreset);
    });
    DebugLog::log("[init] Audio loading started on background thread");

    bluetooth::Initialize();
    DebugLog::log("[init] Bluetooth manager initialized");

    DebugLog::log("[init] Config loaded (theme=%s, musicVol=%.2f, sfxVol=%.2f)",
                  m_config.themePreset.c_str(), m_config.musicVolume, m_config.sfxVolume);

    m_launcher.init({
        .playSfxModalHide = [this]() { m_audio.playSfx(Sfx::ModalHide); },
        .playSfxLaunchGame = [this]() { m_audio.playSfx(Sfx::LaunchGame); },
        .requestExit = [this]() { app().requestExit(); },
#ifdef SWITCHU_MENU
        .suspendForApp = [this]() {
            DebugLog::log("[suspend] entering keep-alive suspend");
            m_musicWasPlaying = m_audio.isPlaying();
            m_audio.stop();
            app().setRenderEnabled(false);
            switchu::menu::smi_cmd::menuSuspending();
            switchu::menu::smi_cmd::drainAllResponses();
            m_suspended = true;
            DebugLog::log("[suspend] now idle");
        },
#else
        .suspendForApp = nullptr,
#endif
        .waitGpuIdle = [this]() { app().gpu().waitIdle(); },
        .setRenderEnabled = [this](bool e) { app().setRenderEnabled(e); },
    });


    DebugLog::log("[init] loadResources...");
    loadResources();
    DebugLog::log("[init] buildGrid...");
    buildGrid();

#ifndef SWITCHU_HOMEBREW
    m_sysMsg.setCallback([this](SysAction a) { handleSystemAction(a); });
    DebugLog::log("[init] connecting IPC client...");
    switchu::menu::ipc::connect();
    switchu::menu::ipc::onMessage(switchu::smi::MenuMessage::HomeRequest,
        [this](const switchu::smi::MenuMessageContext&) {
            m_sysMsg.pushAction(SysAction::HomeButton);
        });
    switchu::menu::ipc::onMessage(switchu::smi::MenuMessage::ApplicationExited,
        [this](const switchu::smi::MenuMessageContext&) {
            DebugLog::log("[ipc] ApplicationExited");
            m_launcher.setAppRunning(false);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(0);
            m_sysMsg.pushAction(SysAction::HomeButton);
        });
    switchu::menu::ipc::onMessage(switchu::smi::MenuMessage::ApplicationSuspended,
        [this](const switchu::smi::MenuMessageContext& ctx) {
            DebugLog::log("[ipc] ApplicationSuspended tid=0x%016lX", ctx.app_id);
            m_launcher.setAppRunning(true);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(ctx.app_id);
            m_sysMsg.pushAction(SysAction::HomeButton);
        });
    switchu::menu::ipc::onMessage(switchu::smi::MenuMessage::AppRecordsChanged,
        [this](const switchu::smi::MenuMessageContext&) {
            DebugLog::log("[ipc] AppRecordsChanged");
            m_deferredRefreshFrames = 3;
        });
    switchu::menu::ipc::onMessage(switchu::smi::MenuMessage::GameCardMountFailure,
        [this](const switchu::smi::MenuMessageContext& ctx) {
            DebugLog::log("[ipc] GameCardMountFailure rc=0x%X", ctx.gc_mount_failure.mount_rc);
            m_deferredRefreshFrames = 3;
        });
    switchu::menu::smi_cmd::menuReady();
#endif

    DebugLog::log("[init] DONE");
    return true;
}

void WiiUMenuApp::onDestroy() {
    if (m_audioFuture.valid()) m_audioFuture.get();

    bluetooth::Finalize();

#ifndef SWITCHU_HOMEBREW
    switchu::menu::smi_cmd::menuClosing();
    switchu::menu::smi_cmd::drainAllResponses();
    switchu::menu::ipc::disconnect();
#endif
    m_audio.shutdown();
}

void WiiUMenuApp::loadResources() {
    std::string fontPath = std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf";
    m_fontNormal.load(app().gpu(), app().renderer(), fontPath, 24);
    m_fontSmall.load(app().gpu(), app().renderer(), fontPath, 18);

    std::string gameCardPath = std::string(SD_ASSETS) + "/icons/gamecard.png";
    m_gameCardTex.loadFromFile(app().gpu(), app().renderer(), gameCardPath);

    m_appLoader.load(m_model, m_iconStreamer);
}

std::shared_ptr<GlossyIcon> WiiUMenuApp::makeIcon(const AppEntry& entry) {
    auto icon = std::make_shared<GlossyIcon>();
    icon->setTag("glossy_icon");
    icon->setTitle(entry.title);
    icon->setTitleId(entry.titleId);
    // Texture is set by IconStreamer::onPageChanged() — not here.
    icon->setCornerRadius(m_theme.iconCornerRadius);
    icon->setIsGameCard(entry.isGameCard());
    icon->setGameCardTexture(&m_gameCardTex);
    icon->setNotLaunchable(!entry.isLaunchable());

#ifndef SWITCHU_HOMEBREW
    if (m_launcher.suspendedTitleId() != 0 &&
        entry.titleId == m_launcher.suspendedTitleId())
        icon->setSuspended(true);

    GlossyIcon* raw = icon.get();
    icon->setOnActivate([this, raw]() {
        uint64_t tid = raw->titleId();
        if (m_launcher.isAppSuspended(tid)) {
            m_audio.playSfx(Sfx::LaunchGame);
            nxui::Rect   fr   = raw->focusRect();
            const nxui::Texture* tex = raw->texture();
            float  cr   = raw->cornerRadius();
            nxui::Color  base = m_theme.panelBase;
            nxui::Color  bord = m_theme.panelBorder;
            m_launchAnim->start(fr, tex, cr, base, bord, 0, {},
                nullptr,
                [this]() { m_launcher.resumeApplication(); });
        } else {
            const AppEntry* entry = m_model.findByTitleId(tid);
            if (entry && !entry->isLaunchable()) {
                m_audio.playSfx(Sfx::ModalShow);
                m_dialogReturnFocus = raw;
                std::string reason;
                auto& i18n = nxui::I18n::instance();
                if (entry->isGameCardNotInserted())
                    reason = i18n.tr("error.gamecard_not_inserted", "Game card is not inserted.");
                else if (entry->needsVerify())
                    reason = i18n.tr("error.needs_verify", "Game data needs verification.");
                else if (entry->needsUpdate())
                    reason = i18n.tr("error.needs_update", "A required update is available.");
                else if (!entry->hasContents())
                    reason = i18n.tr("error.no_contents", "Game data is missing.");
                else
                    reason = i18n.tr("error.cannot_launch", "This game cannot be launched.");
                m_dialog->show(
                    i18n.tr("error.title", "Cannot Launch"),
                    reason,
                    {{i18n.tr("button.ok", "OK"), [this]() {}, true}},
                    0, {}
                );
                focusManager().setFocus(m_dialog.get());
                return;
            }

            m_audio.playSfx(Sfx::Activate);
            nxui::Rect   fr   = raw->focusRect();
            const nxui::Texture* tex = raw->texture();
            float  cr   = raw->cornerRadius();
            nxui::Color  base = m_theme.panelBase;
            nxui::Color  bord = m_theme.panelBorder;
            m_userSelect->show([this, fr, tex, cr, base, bord, tid](AccountUid uid) {
                m_audio.playSfx(Sfx::LaunchGame);
                m_launchAnim->start(fr, tex, cr, base, bord, tid, uid,
                    [this](uint64_t id, AccountUid u) { m_launcher.launchApplication(id, u); });
            });
            focusManager().setFocus(m_userSelect.get());
        }
    });
#else
    icon->setOnActivate([this]() {
        m_audio.playSfx(Sfx::Activate);
    });
#endif
    return icon;
}

void WiiUMenuApp::buildGrid() {
    m_allPresets = ThemePreset::builtInPresets();
    auto userPresets = ThemePreset::loadUserPresets();
    m_allPresets.insert(m_allPresets.end(), userPresets.begin(), userPresets.end());

    m_activePresetName = m_config.themePreset;
    ThemePreset* preset = findPresetPtr(m_activePresetName);
    if (!preset) {
        m_activePresetName = "Default Dark";
        preset = findPresetPtr(m_activePresetName);
    }

    m_activeColors = preset->colors;
    if (m_config.accentH >= 0.f) m_activeColors.accentH = m_config.accentH;
    if (m_config.accentS >= 0.f) m_activeColors.accentS = m_config.accentS;
    if (m_config.accentL >= 0.f) m_activeColors.accentL = m_config.accentL;
    if (m_config.bgH     >= 0.f) m_activeColors.bgH     = m_config.bgH;
    if (m_config.bgS     >= 0.f) m_activeColors.bgS     = m_config.bgS;
    if (m_config.bgL     >= 0.f) m_activeColors.bgL     = m_config.bgL;
    if (m_config.bgAccH  >= 0.f) m_activeColors.bgAccH  = m_config.bgAccH;
    if (m_config.bgAccS  >= 0.f) m_activeColors.bgAccS  = m_config.bgAccS;
    if (m_config.bgAccL  >= 0.f) m_activeColors.bgAccL  = m_config.bgAccL;
    if (m_config.shapeH  >= 0.f) m_activeColors.shapeH  = m_config.shapeH;
    if (m_config.shapeS  >= 0.f) m_activeColors.shapeS  = m_config.shapeS;
    if (m_config.shapeL  >= 0.f) m_activeColors.shapeL  = m_config.shapeL;

    if (m_config.themeMode == "dark")
        m_activeMode = nxui::ThemeMode::Dark;
    else if (m_config.themeMode == "light")
        m_activeMode = nxui::ThemeMode::Light;
    else
        m_activeMode = preset->mode;

    ThemePreset effective;
    effective.mode   = m_activeMode;
    effective.colors = m_activeColors;
    m_theme = effective.toTheme();

    std::vector<std::shared_ptr<GlossyIcon>> icons;
    for (int i = 0; i < m_model.count(); ++i)
        icons.push_back(makeIcon(m_model.at(i)));

    m_background = std::make_shared<WaraWaraBackground>();
    m_background->setRect({0, 0, 1280, 720});

    m_grid = std::make_shared<IconGrid>();
    m_grid->setRect({0, 90, 1280, 540});
    m_grid->setup(std::move(icons), 5, 3, 150, 150, 20, 16);

    m_cursor = std::make_shared<SelectionCursor>();

    m_clock = std::make_shared<DateTimeWidget>();
    m_clock->setSize(150, 62);
    m_clock->setMarginTop(14.f);
    m_clock->setMarginLeft(24.f);
    m_clock->setFont(&m_fontNormal);
    m_clock->setSmallFont(&m_fontSmall);
    m_clock->setCornerRadius(m_theme.cellCornerRadius);
    m_clock->setBlurEnabled(false);

    m_battery = std::make_shared<BatteryWidget>();
    m_battery->setMarginTop(14.f);
    m_battery->setMarginRight(24.f);
    m_battery->setSize(150, 62);
    m_battery->setFont(&m_fontSmall);
    m_battery->setCornerRadius(m_theme.cellCornerRadius);
    m_battery->setBlurEnabled(false);

    m_titlePill = std::make_shared<TitlePillWidget>();
    m_titlePill->setPosition(0, 630.f);
    m_titlePill->setFont(&m_fontNormal);
    m_titlePill->setPadding(9.f, 22.f, 9.f, 22.f);
    m_titlePill->setBlurEnabled(false);

    m_pageIndicator = std::make_shared<PageIndicator>();
    m_pageIndicator->setRect({0, 685.f, 1280.f, 28.f});
    m_pageIndicator->setTheme(&m_theme);

    m_launchAnim = std::make_shared<LaunchAnimation>();

    m_userSelect = std::make_shared<UserSelectScreen>();
    m_userSelect->setFont(&m_fontNormal);
    m_userSelect->setSmallFont(&m_fontSmall);
    m_userSelect->setAudio(&m_audio);
    m_userSelect->loadUsers(app().gpu(), app().renderer());

    m_dialog = std::make_shared<OverlayDialog>();
    m_dialog->setFont(&m_fontNormal);
    m_dialog->setSmallFont(&m_fontSmall);
    m_dialog->setTheme(&m_theme);
    m_dialog->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_dialog->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_dialog->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });

    app().renderer().setBoxWireframeEnabled(m_showWireframe);

    wireFocusCallback();
    m_grid->onPageSwitched([this]() {
        // Stream icon textures for the new page.
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
        auto* target = m_grid->focusManager().current();
        if (target)
            focusManager().setFocus(target);
        updateCursor();
    });

    // Load textures for the initial page (page 0).
    m_iconStreamer.onPageChanged(0, m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());

    m_grid->startAppearAnimation();

    SidebarManager::Actions sidebarActions;
#ifndef SWITCHU_HOMEBREW
    sidebarActions.onAlbum       = [this]() { m_launcher.launchAlbum(); };
    sidebarActions.onMiiEditor   = [this]() { m_launcher.launchMiiEditor(); };
    sidebarActions.onControllers = [this]() { m_launcher.launchControllerPairing(); };
#else
    sidebarActions.onAlbum       = [this]() { m_audio.playSfx(Sfx::Activate); };
    sidebarActions.onMiiEditor   = [this]() { m_audio.playSfx(Sfx::Activate); };
    sidebarActions.onControllers = [this]() { m_audio.playSfx(Sfx::Activate); };
#endif
    sidebarActions.onSettings = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (m_settings) {
            std::vector<std::string> presetNames;
            for (auto& p : m_allPresets) presetNames.push_back(p.name);
            m_settings->setThemePresetState(m_activePresetName, presetNames, m_activeColors,
                                             m_activeMode == nxui::ThemeMode::Dark);
            m_settings->show();
            focusManager().setFocus(m_settings.get());
        }
    };
    sidebarActions.onSleep = [this]() {
        if (!m_dialog) return;
        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            "Sleep",
            "Put the console into sleep mode?",
            {
                {"Cancel", [this]() {  }, true},
                {"Sleep", [this]() {
#ifndef SWITCHU_HOMEBREW
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.enterSleep();
#else
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    app().requestExit();
#endif
                }, true}
            },
            1,
            {}
        );
        focusManager().setFocus(m_dialog.get());
    };
    sidebarActions.onMiiverse = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (!m_dialog) return;
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            "Miiverse",
            "A miiverse recreation is in development, but not ready yet. Stay tuned!",
            {{"OK", [this]() { }, true}},
            0,
            {}
        );
        focusManager().setFocus(m_dialog.get());
    };

    m_sidebar.build(app().gpu(), app().renderer(), SD_ASSETS, sidebarActions);

    wireGlobalActions();
    applyTheme();

    auto& root = rootBox();
    root.clearChildren();

    m_bgLayer = std::make_shared<nxui::Box>();
    m_bgLayer->setRect({0, 0, 1280, 720});
    m_bgLayer->setTag("bgLayer");
    m_bgLayer->setWireframeEnabled(false);
    m_bgLayer->addChild(m_background);

    m_contentLayer = std::make_shared<nxui::Box>();
    m_contentLayer->setRect({0, 0, 1280, 720});
    m_contentLayer->setTag("contentLayer");
    m_contentLayer->setWireframeEnabled(false);

    m_topHud = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    m_topHud->setRect({0, 0, 1280, 90});
    m_topHud->setTag("topHud");
    m_topHud->setWireframeEnabled(false);
    m_topHud->setJustifyContent(nxui::JustifyContent::SPACE_BETWEEN);
    m_topHud->setAlignItems(nxui::AlignItems::FLEX_START);
    m_topHud->addChild(m_clock);
    m_topHud->addChild(m_battery);
    m_topHud->layout();

    m_leftSidebar = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
    m_leftSidebar->setTag("leftSidebar");
    m_leftSidebar->setWireframeEnabled(false);
    for (auto& btn : m_sidebar.leftButtons())
        m_leftSidebar->addChild(btn);

    m_rightSidebar = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
    m_rightSidebar->setTag("rightSidebar");
    m_rightSidebar->setWireframeEnabled(false);
    for (auto& btn : m_sidebar.rightButtons())
        m_rightSidebar->addChild(btn);

    m_contentLayer->addChild(m_grid);
    m_contentLayer->addChild(m_leftSidebar);
    m_contentLayer->addChild(m_rightSidebar);
    m_contentLayer->addChild(m_topHud);
    m_contentLayer->addChild(m_titlePill);
    m_contentLayer->addChild(m_pageIndicator);

    m_overlayLayer = std::make_shared<nxui::Box>();
    m_overlayLayer->setRect({0, 0, 1280, 720});
    m_overlayLayer->setTag("overlayLayer");
    m_overlayLayer->setWireframeEnabled(false);
    m_overlayLayer->addChild(m_cursor);
    m_overlayLayer->addChild(m_userSelect);

    createSettings();

    m_overlayLayer->addChild(m_dialog);
    m_overlayLayer->addChild(m_launchAnim);

    root.addChild(m_bgLayer);
    root.addChild(m_contentLayer);
    root.addChild(m_overlayLayer);

    if (auto* firstIcon = m_grid->focusManager().current())
        focusManager().setFocus(firstIcon);
}

void WiiUMenuApp::loadSoundPreset(const std::string& preset) {
    std::string base = std::string(SD_ASSETS) + "/sounds/" + preset;
    DebugLog::log("[audio] Loading preset '%s' from %s", preset.c_str(), base.c_str());

    m_audio.loadSfx(Sfx::Navigate,        base + "/sfx/navigation.wav");
    m_audio.loadSfx(Sfx::Activate,        base + "/sfx/activation.wav");
    m_audio.loadSfx(Sfx::PageChange,      base + "/sfx/tab_transition.wav");
    m_audio.loadSfx(Sfx::ModalShow,       base + "/sfx/show_modal.wav");
    m_audio.loadSfx(Sfx::ModalHide,       base + "/sfx/hide_modal.wav");
    m_audio.loadSfx(Sfx::LaunchGame,      base + "/sfx/launch_game.wav");
    m_audio.loadSfx(Sfx::ThemeToggle,     base + "/sfx/toggle_on.wav");
    m_audio.loadSfx(Sfx::ToggleOff,       base + "/sfx/toggle_off.wav");
    m_audio.loadSfx(Sfx::SliderUp,        base + "/sfx/slider_up.wav");
    m_audio.loadSfx(Sfx::SliderDown,      base + "/sfx/slider_down.wav");
    m_audio.loadSfx(Sfx::ConfirmPositive, base + "/sfx/confirm.wav");
    m_audio.loadSfx(Sfx::Volume,          base + "/sfx/volume.wav");

    std::string musicDir = base + "/music";
    DIR* dir = opendir(musicDir.c_str());
    if (dir) {
        std::vector<std::string> tracks;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.size() > 4 && name.substr(name.size() - 4) == ".mp3")
                tracks.push_back(name);
        }
        closedir(dir);
        std::sort(tracks.begin(), tracks.end());
        for (const auto& t : tracks)
            m_audio.loadTrack(musicDir + "/" + t);
        DebugLog::log("[audio] Loaded %zu music tracks", tracks.size());
    } else {
        DebugLog::log("[audio] No music directory for preset '%s'", preset.c_str());
    }
}

void WiiUMenuApp::changeSoundPreset(const std::string& preset) {
    m_audio.stop();
    m_audio.clearTracks();
    m_audio.clearSfx();

    m_presetChangePending = true;
    m_audioFuture = m_threadPool.submit([this, preset]() {
        loadSoundPreset(preset);
    });
}

std::vector<std::string> WiiUMenuApp::scanAvailablePresets() {
    std::vector<std::string> presets;
    std::string soundsDir = std::string(SD_ASSETS) + "/sounds";
    DIR* dir = opendir(soundsDir.c_str());
    if (!dir) return presets;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        std::string sub = soundsDir + "/" + name;
        struct stat st;
        if (stat(sub.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        std::string sfxSub = sub + "/sfx";
        std::string musicSub = sub + "/music";
        struct stat st2;
        bool hasSfx = (stat(sfxSub.c_str(), &st2) == 0 && S_ISDIR(st2.st_mode));
        bool hasMusic = (stat(musicSub.c_str(), &st2) == 0 && S_ISDIR(st2.st_mode));
        if (hasSfx || hasMusic)
            presets.push_back(name);
    }
    closedir(dir);
    std::sort(presets.begin(), presets.end());
    return presets;
}

void WiiUMenuApp::createSettings() {
    if (m_settings) return;

    m_settings = std::make_shared<SettingsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_settings);
    m_settings->setFont(&m_fontNormal);
    m_settings->setSmallFont(&m_fontSmall);
    m_settings->setTheme(&m_theme);
    m_settings->setMusicState(m_audio.isPlaying(), m_audio.volume(), m_audio.sfxVolume());
    m_settings->setWireframeState(m_showWireframe);
    m_settings->setUiLanguageOverride(m_config.uiLanguageOverride);
    m_settings->setSoundPresetState(m_config.soundPreset, m_availablePresets);

    {
        std::vector<std::string> presetNames;
        for (auto& p : m_allPresets) presetNames.push_back(p.name);
        m_settings->setThemePresetState(m_activePresetName, presetNames, m_activeColors,
                                       m_activeMode == nxui::ThemeMode::Dark);
    }

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
    m_settings->onUiLanguageChange([this](const std::string& tag) {
        m_config.uiLanguageOverride = tag;
        if (m_settings) m_settings->setUiLanguageOverride(tag);
        applyUiLanguage();
        // The font caches hold Texture objects whose GPU memory is still
        // referenced by in-flight command buffers.  Wait for the GPU to
        // finish before destroying them, otherwise we get use-after-free
        // artifacts (glitch screen / crash).
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
        else if (key == "bg_h")      { m_activeColors.bgH     = value; m_config.bgH     = value; }
        else if (key == "bg_s")      { m_activeColors.bgS     = value; m_config.bgS     = value; }
        else if (key == "bg_l")      { m_activeColors.bgL     = value; m_config.bgL     = value; }
        else if (key == "bg_acc_h")  { m_activeColors.bgAccH  = value; m_config.bgAccH  = value; }
        else if (key == "bg_acc_s")  { m_activeColors.bgAccS  = value; m_config.bgAccS  = value; }
        else if (key == "bg_acc_l")  { m_activeColors.bgAccL  = value; m_config.bgAccL  = value; }
        else if (key == "shape_h")   { m_activeColors.shapeH  = value; m_config.shapeH  = value; }
        else if (key == "shape_s")   { m_activeColors.shapeS  = value; m_config.shapeS  = value; }
        else if (key == "shape_l")   { m_activeColors.shapeL  = value; m_config.shapeL  = value; }
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
                // Cancel button — normal close behavior (plays ModalHide)
                dlgButtons.push_back({buttons[i].label, [cb]() { if (cb) cb(); }, true});
            } else {
                // Action button — play positive confirmation sound and close the dialog.
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

void WiiUMenuApp::wireFocusCallback() {
    focusManager().onFocusChanged([this](nxui::Widget*, nxui::Widget* cur) {
        updateCursor();

        if ((m_dialog && m_dialog->isActive()) ||
            (m_settings && m_settings->isActive()) ||
            (m_userSelect && m_userSelect->isActive()))
            return;

        bool suppressSfx = m_suppressNextNavigateSfx;
        m_suppressNextNavigateSfx = false;
        if (!suppressSfx)
            m_audio.playSfx(Sfx::Navigate);

        if (cur && cur->tag() == "glossy_icon") {
            m_grid->focusManager().setFocus(cur);
            auto* icon = static_cast<GlossyIcon*>(cur);
#ifndef SWITCHU_HOMEBREW
            if (m_launcher.isAppSuspended(icon->titleId()))
                m_titlePill->setText(std::string("\xe2\x96\xb6  ") + icon->title());
            else
#endif
                m_titlePill->setText(icon->title());
            m_titlePill->setVisible(true);
        } else if (cur) {
            for (auto& btn : m_sidebar.leftButtons()) {
                if (btn.get() == cur) { m_titlePill->setText(btn->label()); m_titlePill->setVisible(true); return; }
            }
            for (auto& btn : m_sidebar.rightButtons()) {
                if (btn.get() == cur) { m_titlePill->setText(btn->label()); m_titlePill->setVisible(true); return; }
            }
            m_titlePill->setVisible(false);
        } else {
            m_titlePill->setVisible(false);
        }
    });
    updateCursor();
    if (auto* cur = focusManager().current()) {
        if (cur->tag() == "glossy_icon")
            m_titlePill->setText(static_cast<GlossyIcon*>(cur)->title());
    }
}

bool WiiUMenuApp::isCurrentFocusableWidget(nxui::Widget* w) const {
    if (!w) return false;
    if (m_settings && m_settings.get() == w) return w->isFocusable();
    for (const auto& btn : m_sidebar.leftButtons())
        if (btn.get() == w) return w->isFocusable();
    for (const auto& btn : m_sidebar.rightButtons())
        if (btn.get() == w) return w->isFocusable();
    if (m_grid)
        for (const auto& icon : m_grid->allIcons())
            if (icon.get() == w) return w->isFocusable();
    return false;
}

nxui::Widget* WiiUMenuApp::focusRoot() {
    if (m_suspended) return nullptr;
    if (m_launchAnim && m_launchAnim->isPlaying()) return nullptr;
    if (m_dialog && m_dialog->isActive()) return m_dialog.get();
    if (m_settings && m_settings->isActive()) return m_settings.get();
    if (m_userSelect && m_userSelect->isActive()) return m_userSelect.get();
    return &rootBox();
}

void WiiUMenuApp::wireGlobalActions() {
    auto& root = rootBox();

    root.addAction(static_cast<uint64_t>(nxui::Button::L), [this]() {
        int p = m_grid->currentPage() - 1;
        if (p >= 0 && !m_grid->isTransitioning()) {
            m_grid->startWaveTransition(p);
            m_audio.playSfx(Sfx::PageChange);
        }
    });
    root.addAction(static_cast<uint64_t>(nxui::Button::R), [this]() {
        int p = m_grid->currentPage() + 1;
        if (p < m_grid->totalPages() && !m_grid->isTransitioning()) {
            m_grid->startWaveTransition(p);
            m_audio.playSfx(Sfx::PageChange);
        }
    });
    root.addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        m_showDebugOverlay = !m_showDebugOverlay;
    });
#ifdef SWITCHU_HOMEBREW
    root.addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        m_audio.playSfx(Sfx::ModalHide);
        app().requestExit();
    });
#endif

#ifndef SWITCHU_HOMEBREW
    root.addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (m_launcher.suspendedTitleId() == 0) return;
        auto* cur = focusManager().current();
        if (!cur || cur->tag() != "glossy_icon") return;
        auto* icon = static_cast<GlossyIcon*>(cur);
        if (!m_launcher.isAppSuspended(icon->titleId())) return;

        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = cur;
        m_dialog->show(
            "Close game",
            "Close " + icon->title() + "?\nUnsaved progress will be lost.",
            {
                {"Cancel", [this]() {}, true},
                {"Close",  [this]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.terminateApplication();
                    m_launcher.setAppRunning(false);
                    m_launcher.setAppHasForeground(false);
                    m_launcher.setSuspendedTitleId(0);
                    for (auto& ic : m_grid->allIcons())
                        ic->setSuspended(false);
                    if (auto* cur = m_grid->focusManager().current()) {
                        auto* icon = static_cast<GlossyIcon*>(cur);
                        m_titlePill->setText(icon->title());
                    }
                }, true}
            },
            0,
            {}
        );
        focusManager().setFocus(m_dialog.get());
    });
#endif
}

void WiiUMenuApp::handleTouch() {
    constexpr float kSwipeThreshold = 80.f;

    if (app().input().touchDown()) {
        float tx = app().input().touchX();
        float ty = app().input().touchY();
        int hit = m_grid->hitTest(tx, ty);
        m_touchHitIndex = hit;
        m_touchOnFocused = false;
        if (hit >= 0) {
            auto icons = m_grid->pageIcons();
            if (hit < (int)icons.size())
                m_touchOnFocused = (icons[hit] == focusManager().current());
        }
    }

    if (app().input().touchUp()) {
        float dx = app().input().touchDeltaX();
        float dy = app().input().touchDeltaY();
        if (std::abs(dx) > kSwipeThreshold && std::abs(dx) > std::abs(dy) * 1.5f) {
            int p = m_grid->currentPage() + (dx < 0 ? 1 : -1);
            if (p >= 0 && p < m_grid->totalPages() && !m_grid->isTransitioning()) {
                m_grid->startWaveTransition(p);
                m_audio.playSfx(Sfx::PageChange);
            }
        }
        m_touchHitIndex = -1;
    }
}

#ifndef SWITCHU_HOMEBREW

void WiiUMenuApp::handleSystemAction(SysAction a) {
    switch (a) {
        case SysAction::HomeButton:
            DebugLog::log("[pump] HomeButton -> UI update");
            m_launcher.setAppHasForeground(false);
            m_showLoadingScreen = false;

            for (auto& ic : m_grid->allIcons())
                ic->setSuspended(m_launcher.suspendedTitleId() != 0 &&
                                 ic->titleId() == m_launcher.suspendedTitleId());
            if (auto* cur = m_grid->focusManager().current()) {
                auto* icon = static_cast<GlossyIcon*>(cur);
                if (m_launcher.isAppSuspended(icon->titleId()))
                    m_titlePill->setText(std::string("\xe2\x96\xb6  ") + icon->title());
                else
                    m_titlePill->setText(icon->title());
            }
            break;
        default:
            break;
    }
}

void WiiUMenuApp::refreshAppList() {
    DebugLog::log("[refresh] starting async app list fetch");

    if (m_asyncRefreshPending) {
        DebugLog::log("[refresh] already in progress, skipping");
        return;
    }

    if (m_launchAnim && m_launchAnim->isPlaying()) m_launchAnim->stop();
    if (m_userSelect && m_userSelect->isActive()) m_userSelect->hide();

    m_refreshPrevPage = m_grid ? m_grid->currentPage() : 0;
    m_asyncRefreshPending = true;

    m_appLoader.startAsync(m_threadPool);
}

void WiiUMenuApp::finalizeRefresh() {
    DebugLog::log("[refresh] finalizing (GPU upload)");
    m_asyncRefreshPending = false;

    app().gpu().waitIdle();
    m_grid->clearChildren();
    m_model.clear();
    m_iconStreamer.clear();
    app().renderer().resetTextureSlots();
    m_fontNormal.clearCache();
    m_fontSmall.clearCache();
    app().gpu().resetImagePool();
    m_userSelect->loadUsers(app().gpu(), app().renderer());

    m_appLoader.finalize(m_model, m_iconStreamer);
    DebugLog::log("[refresh] found %d apps", m_model.count());

    std::vector<std::shared_ptr<GlossyIcon>> icons;
    for (int i = 0; i < m_model.count(); ++i) {
        auto icon = makeIcon(m_model.at(i));
        icon->setBaseColor(m_theme.iconDefault);
        icons.push_back(std::move(icon));
    }

    m_grid->setup(std::move(icons), 5, 3, 150, 150, 20, 16);
    if (m_refreshPrevPage > 0) m_grid->setPage(m_refreshPrevPage);
    wireFocusCallback();
    m_grid->onPageSwitched([this]() {
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
        auto* target = m_grid->focusManager().current();
        if (target) focusManager().setFocus(target);
        updateCursor();
    });

    // Load textures for the restored page.
    int page = m_refreshPrevPage > 0 ? m_refreshPrevPage : 0;
    m_iconStreamer.onPageChanged(page, m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());

    m_grid->startAppearAnimation();
    if (auto* firstIcon = m_grid->focusManager().current())
        focusManager().setFocus(firstIcon);

    SidebarManager::Actions actions;
    actions.onAlbum       = [this]() { m_launcher.launchAlbum(); };
    actions.onMiiEditor   = [this]() { m_launcher.launchMiiEditor(); };
    actions.onControllers = [this]() { m_launcher.launchControllerPairing(); };
    actions.onSettings    = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (m_settings) {
            std::vector<std::string> presetNames;
            for (auto& p : m_allPresets) presetNames.push_back(p.name);
            m_settings->setThemePresetState(m_activePresetName, presetNames, m_activeColors,
                                             m_activeMode == nxui::ThemeMode::Dark);
            m_settings->show();
            focusManager().setFocus(m_settings.get());
        }
    };
    actions.onSleep = [this]() {
        if (!m_dialog) return;
        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show("Sleep", "Put the console into sleep mode?",
            {{"Cancel", [this]() {}, true}, {"Sleep", [this]() { m_audio.playSfx(Sfx::ConfirmPositive); m_launcher.enterSleep(); }, true}},
            1, {});
        focusManager().setFocus(m_dialog.get());
    };
    actions.onMiiverse = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (!m_dialog) return;
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show("Miiverse", "A miiverse recreation is in development, but not ready yet. Stay tuned!",
            {{"OK", [this]() {}, true}}, 0, {});
        focusManager().setFocus(m_dialog.get());
    };

    m_sidebar.build(app().gpu(), app().renderer(), SD_ASSETS, actions);

    if (m_leftSidebar) {
        m_leftSidebar->clearChildren();
        for (auto& btn : m_sidebar.leftButtons()) m_leftSidebar->addChild(btn);
    }
    if (m_rightSidebar) {
        m_rightSidebar->clearChildren();
        for (auto& btn : m_sidebar.rightButtons()) m_rightSidebar->addChild(btn);
    }
    applyTheme();
    DebugLog::log("[refresh] done, %d icons on page %d", m_model.count(), m_grid->currentPage());
}

#endif

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
    m_userSelect->panel().setPanelOpacity(1.5f);
    m_userSelect->panel().setBlurEnabled(false);
    m_userSelect->titlePanel().setBaseColor(m_theme.panelBase);
    m_userSelect->titlePanel().setBorderColor(m_theme.panelBorder);
    m_userSelect->titlePanel().setHighlightColor(m_theme.panelHighlight);
    m_userSelect->titlePanel().setPanelOpacity(1.5f);
    m_userSelect->titlePanel().setBlurEnabled(false);
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

void WiiUMenuApp::onUpdate(float dt) {
    if (m_suspended) {
#ifdef SWITCHU_MENU
        AppletStorage wakeSt;
        if (R_SUCCEEDED(appletPopInteractiveInData(&wakeSt))) {
            switchu::smi::WakeSignal ws{};
            s64 sz = 0;
            appletStorageGetSize(&wakeSt, &sz);
            if (sz >= (s64)sizeof(ws))
                appletStorageRead(&wakeSt, 0, &ws, sizeof(ws));
            appletStorageClose(&wakeSt);

            if (ws.magic == switchu::smi::kWakeMagic) {
                DebugLog::log("[suspend] wake signal received (reason=%u tid=0x%016lX)",
                              ws.reason, ws.suspended_tid);
                m_wakeReason = ws.reason;
                m_wakeSuspendedTid = ws.suspended_tid;
                m_suspended = false;
            } else {
                return;
            }
        } else {
            m_sysMsg.pump();
            return;
        }
#else
        return;
#endif
    }

    if (m_returnFadeTimer > 0.f)
        m_returnFadeTimer = std::max(0.f, m_returnFadeTimer - dt);

    if (!app().renderEnabled()) {
        DebugLog::log("[suspend] resuming GPU on main thread");
        if (m_launchAnim && m_launchAnim->isPlaying()) m_launchAnim->stop();

#ifndef SWITCHU_HOMEBREW
        if (m_wakeReason == 0) {
            m_launcher.setAppRunning(true);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(m_wakeSuspendedTid);
        } else {
            m_launcher.setAppRunning(false);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(0);
        }

        {
            uint64_t sTid = m_launcher.suspendedTitleId();
            for (auto& ic : m_grid->allIcons())
                ic->setSuspended(sTid != 0 && ic->titleId() == sTid);
            if (auto* cur = m_grid->focusManager().current()) {
                auto* icon = static_cast<GlossyIcon*>(cur);
                if (m_launcher.isAppSuspended(icon->titleId()))
                    m_titlePill->setText(std::string("\xe2\x96\xb6  ") + icon->title());
                else
                    m_titlePill->setText(icon->title());
            }
            DebugLog::log("[suspend] icons updated (suspendedTid=0x%016lX)", sTid);
        }
#endif
        app().gpu().waitIdle();
        app().setRenderEnabled(true);
        if (m_musicWasPlaying && m_config.musicEnabled) m_audio.play();
        m_musicWasPlaying = false;
        m_returnFadeTimer = kReturnFadeInDur;
    }

    if (!m_audioStarted && m_audioFuture.valid() &&
        m_audioFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_audioFuture.get();
        m_audio.setVolume(m_config.musicVolume);
        m_audio.setSfxVolume(m_config.sfxVolume);
        if (m_config.musicEnabled) m_audio.play();
        m_audioStarted = true;
        DebugLog::log("[init] Audio ready (deferred)");
    }

    if (m_presetChangePending && m_audioFuture.valid() &&
        m_audioFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_audioFuture.get();
        m_audio.setVolume(m_config.musicVolume);
        m_audio.setSfxVolume(m_config.sfxVolume);
        if (m_config.musicEnabled)
            m_audio.play();
        m_presetChangePending = false;
        DebugLog::log("[audio] Preset change complete: %s", m_config.soundPreset.c_str());
    }

    if (m_settingsNeedRefresh && m_settings) {
        m_settingsNeedRefresh = false;
        m_settings->refreshTranslations();
    }

    if (m_pendingNetConnect) {
        m_pendingNetConnect = false;
        m_launcher.launchNetConnect();
        return;
    }

#ifndef SWITCHU_HOMEBREW
#ifdef SWITCHU_MENU
    {
        AppletStorage notifySt;
        while (R_SUCCEEDED(appletPopInteractiveInData(&notifySt))) {
            switchu::smi::DaemonNotification notif{};
            s64 sz = 0;
            appletStorageGetSize(&notifySt, &sz);
            if (sz >= (s64)sizeof(notif))
                appletStorageRead(&notifySt, 0, &notif, sizeof(notif));
            appletStorageClose(&notifySt);

            if (notif.magic != switchu::smi::kNotifyMagic) continue;
            DebugLog::log("[notify] msg=%u", (unsigned)notif.msg);

            switch (notif.msg) {
            case switchu::smi::MenuMessage::HomeRequest:
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::ApplicationExited:
                m_launcher.setAppRunning(false);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(0);
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::ApplicationSuspended:
                m_launcher.setAppRunning(true);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(notif.app_id);
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::AppRecordsChanged:
            case switchu::smi::MenuMessage::GameCardMountFailure:
                m_deferredRefreshFrames = 3;
                break;
            case switchu::smi::MenuMessage::AppViewFlagsUpdate: {
                uint64_t tid = notif.app_id;
                uint32_t flags = notif.payload;
                m_model.updateViewFlags(tid, flags);
                for (auto& icon : m_grid->allIcons()) {
                    if (icon->titleId() == tid) {
                        bool launchable = (flags == 0) ||
                            (flags & switchu::ns::AppViewFlag_CanLaunch);
                        icon->setNotLaunchable(!launchable);
                        icon->setIsGameCard(
                            flags & switchu::ns::AppViewFlag_IsGameCard);
                        break;
                    }
                }
                break;
            }
            default:
                break;
            }
        }
    }
#endif

    m_sysMsg.pump();
    if (m_deferredRefreshFrames > 0 && --m_deferredRefreshFrames == 0) {
        DebugLog::log("[update] deferred refresh triggered, calling refreshAppList");
        refreshAppList();
    }
    if (m_asyncRefreshPending && m_appLoader.isReady()) {
        finalizeRefresh();
    }
#endif

    if (m_showLoadingScreen && ++m_loadingScreenFrames > 60) {
        m_showLoadingScreen = false;
        m_loadingScreenFrames = 0;
    }

    if (!m_launchAnim->isPlaying()
        && !(m_dialog && m_dialog->isActive())
        && !(m_settings && m_settings->isActive())
        && !(m_userSelect && m_userSelect->isActive()))
    {
        handleTouch();
    }

    bool dialogActiveNow = (m_dialog && m_dialog->isActive());
    if (dialogActiveNow)
        m_dialog->handleTouch(app().input());

    if (m_settings && m_settings->isActive())
        m_settings->handleTouch(app().input());

    if (m_dialogWasActive && !dialogActiveNow) {
        if (isCurrentFocusableWidget(m_dialogReturnFocus)) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_dialogReturnFocus);
        }
        m_dialogReturnFocus = nullptr;
    }
    m_dialogWasActive = dialogActiveNow;

    if (m_userSelect && m_userSelect->isActive())
        m_userSelect->handleTouch(app().input());

    if (!(m_userSelect && m_userSelect->isActive())
        && !(m_dialog && m_dialog->isActive())
        && !m_launchAnim->isPlaying())
    {
        auto* cur = focusManager().current();
        if (!cur || !cur->isFocusable()) {
            auto* target = m_grid->focusManager().current();
            if (target)
                focusManager().setFocus(target);
        }
    }

    m_sidebar.update(dt, focusManager().current());

    nxui::AnimationManager::instance().update(dt);
}

void WiiUMenuApp::onRender(nxui::Renderer& ren) {
    if (m_showLoadingScreen && !m_launchAnim->isPlaying())
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, 1.f));

    if (m_returnFadeTimer > 0.f) {
        float alpha = m_returnFadeTimer / kReturnFadeInDur;
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, alpha));
    }

    if (m_touchHitIndex >= 0 && !m_touchOnFocused && app().input().isTouching()) {
        auto icons = m_grid->pageIcons();
        if (m_touchHitIndex < (int)icons.size()) {
            nxui::Rect r = icons[m_touchHitIndex]->focusRect();
            float cr = icons[m_touchHitIndex]->cornerRadius();
            ren.drawRoundedRect(r, nxui::Color(1.f, 1.f, 1.f, 0.18f), cr);
        }
    }

    m_pageIndicator->setPageCount(m_grid->totalPages());
    m_pageIndicator->setCurrentPage(m_grid->currentPage());

    if (m_showDebugOverlay) {
        nxui::Rect logBg = {0, 0, 500, 720};
        ren.drawRect(logBg, nxui::Color(0, 0, 0, 0.75f));
        auto lines = DebugLog::lines();
        float y = 8.f;
        for (const auto& line : lines) {
            ren.drawText(line, {8.f, y}, &m_fontSmall, nxui::Color(0.f, 1.f, 0.f, 1.f), 1.f);
            y += 22.f;
            if (y > 700.f) break;
        }
    }
}

void WiiUMenuApp::updateCursor() {
    if ((m_settings && m_settings->isActive()) ||
        (m_dialog && m_dialog->isActive()) ||
        (m_userSelect && m_userSelect->isActive()))
        return;

    auto* cur = focusManager().current();
    if (cur) {
        nxui::Rect fr = cur->focusRect();
        m_cursor->moveTo(fr.expanded(4.f));
        m_cursor->setVisible(true);
    } else {
        m_cursor->setVisible(false);
    }
}
