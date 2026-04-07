#pragma once
#include <nxui/Activity.hpp>
#include <nxui/Application.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/Theme.hpp>
#include "widgets/IconGrid.hpp"
#include "core/GridModel.hpp"
#include "widgets/SelectionCursor.hpp"
#include "widgets/WaraWaraBackground.hpp"
#include "widgets/DateTimeWidget.hpp"
#include "widgets/BatteryWidget.hpp"
#include "widgets/TitlePillWidget.hpp"
#include "core/AudioManager.hpp"
#include "widgets/LaunchAnimation.hpp"
#include "widgets/UserSelectScreen.hpp"
#include "widgets/OverlayDialog.hpp"
#include "widgets/AppletButton.hpp"
#include "widgets/PageIndicator.hpp"
#include "settings/SettingsScreen.hpp"
#include "core/Config.hpp"
#include "core/ThemePreset.hpp"
#include "sidebar/SidebarManager.hpp"
#include "launcher/AppletLauncher.hpp"
#include "launcher/AppListLoader.hpp"
#include "launcher/IconStreamer.hpp"
#include "core/SystemMessages.hpp"
#include <nxui/widgets/Background.hpp>
#include <nxui/widgets/Box.hpp>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <future>
#include <switch.h>
#ifdef SWITCHU_MENU
#include <switchu/smi_protocol.hpp>
#endif


#ifdef SWITCHU_HOMEBREW
static constexpr const char* SD_ASSETS = "romfs:";
#else
static constexpr const char* SD_ASSETS = "sdmc:/switch/SwitchU";
#endif

class WiiUMenuApp : public nxui::Activity {
public:
    WiiUMenuApp();
    ~WiiUMenuApp();

#ifdef SWITCHU_MENU
    void setStartupStatus(uint64_t suspendedTitleId, bool appRunning);
#endif

    bool onCreate() override;
    void onDestroy() override;
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

    nxui::Widget* focusRoot() override;

private:
    void loadResources();
    void buildGrid();
    void applyTheme();
    void applyUiLanguage();
    void rebuildThemeFromColors();
    ThemePreset* findPresetPtr(const std::string& name);
    void deleteActivePreset();
    void updateCursor();
    void handleTouch();
    std::shared_ptr<GlossyIcon> makeIcon(const AppEntry& entry);
    void wireFocusCallback();
    void wireGlobalActions();
    bool isCurrentFocusableWidget(nxui::Widget* w) const;
    void createSettings();
    void loadSoundPreset(const std::string& preset);
    void changeSoundPreset(const std::string& preset);
    std::vector<std::string> scanAvailablePresets();

#ifndef SWITCHU_HOMEBREW
    void refreshAppList();
    void finalizeRefresh();
    void handleSystemAction(SysAction a);
#endif

    nxui::Font  m_fontNormal;
    nxui::Font  m_fontSmall;

    GridModel    m_model;
    nxui::Theme  m_theme;

    std::string              m_activePresetName = "Default Dark";
    ThemeColorSet            m_activeColors;
    nxui::ThemeMode          m_activeMode = nxui::ThemeMode::Dark;
    std::vector<ThemePreset> m_allPresets;

    std::shared_ptr<nxui::Background> m_background;
    std::shared_ptr<IconGrid>          m_grid;
    std::shared_ptr<SelectionCursor>   m_cursor;
    std::shared_ptr<DateTimeWidget>    m_clock;
    std::shared_ptr<BatteryWidget>     m_battery;
    std::shared_ptr<TitlePillWidget>   m_titlePill;
    std::shared_ptr<PageIndicator>     m_pageIndicator;
    std::shared_ptr<LaunchAnimation>   m_launchAnim;
    std::shared_ptr<UserSelectScreen>  m_userSelect;
    std::shared_ptr<OverlayDialog>     m_dialog;
    std::shared_ptr<SettingsScreen>    m_settings;

    nxui::Texture m_gameCardTex;

    std::shared_ptr<nxui::Box> m_bgLayer;
    std::shared_ptr<nxui::Box> m_contentLayer;
    std::shared_ptr<nxui::Box> m_overlayLayer;
    std::shared_ptr<nxui::Box> m_topHud;
    std::shared_ptr<nxui::Box> m_leftSidebar;
    std::shared_ptr<nxui::Box> m_rightSidebar;

    AudioManager m_audio;
    std::future<void>    m_audioFuture;
    bool                 m_audioStarted = false;
    std::vector<std::string> m_availablePresets;
    bool                 m_presetChangePending = false;

    nxui::ThreadPool m_threadPool{2};
    SidebarManager  m_sidebar;
    AppletLauncher  m_launcher;
    AppListLoader   m_appLoader;
    IconStreamer    m_iconStreamer;
    SystemMessages  m_sysMsg;

    bool m_showDebugOverlay  = false;
    bool m_showLoadingScreen = false;
    bool m_showWireframe     = false;
    bool m_suspended         = false;
    bool m_musicWasPlaying   = false;
    uint64_t m_wakeSuspendedTid = 0;
    uint32_t m_wakeReason       = 0;

    int  m_touchHitIndex     = -1;
    bool m_touchOnFocused    = false;
    int  m_deferredRefreshFrames = 0;
    int  m_loadingScreenFrames   = 0;
    bool m_asyncRefreshPending   = false;
    int  m_refreshPrevPage       = 0;

    AppConfig m_config;
    bool m_settingsNeedRefresh        = false;
    nxui::Widget* m_dialogReturnFocus = nullptr;
    bool m_dialogWasActive            = false;
    bool m_suppressNextNavigateSfx    = false;
    bool m_pendingNetConnect          = false;

    float m_returnFadeTimer = 0.f;
    static constexpr float kReturnFadeInDur = 0.45f;
};
