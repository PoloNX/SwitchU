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
#include "core/AccessibilityManager.hpp"
#include "widgets/LaunchAnimation.hpp"
#include "widgets/OverlayDialog.hpp"
#include "widgets/ProgressDialog.hpp"
#include "widgets/AppletButton.hpp"
#include "widgets/PageIndicator.hpp"
#include "widgets/UserAvatarButton.hpp"
#include "widgets/FolderBackdrop.hpp"
#include "settings/SettingsScreen.hpp"
#include "settings/GameOptionsScreen.hpp"
#include "settings/FolderOptionsScreen.hpp"
#include "settings/ControllerTestScreen.hpp"
#include "themeshop/ThemeShopScreen.hpp"
#include "core/Config.hpp"
#include "core/FolderStore.hpp"
#include "core/ThemePreset.hpp"
#include "sidebar/SidebarManager.hpp"
#include "launcher/AppletLauncher.hpp"
#include "launcher/AppListLoader.hpp"
#include "launcher/IconStreamer.hpp"
#include "core/SystemMessages.hpp"
#include "navigation/MenuNavigator.hpp"
#include "services/ClockService.hpp"
#ifdef SWITCHU_DEBUG_UI
#include "debug/DebugImGuiOverlay.hpp"
#endif
#include <nxui/widgets/Background.hpp>
#include <nxui/widgets/Box.hpp>
#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/widgets/Label.hpp>
#include <cstdint>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <future>
#include <utility>
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

    void setTutorialStartupFade(bool enabled);

#ifdef SWITCHU_MENU
    void setStartupStatus(const switchu::smi::SystemStatus& status, bool fastReturn = false);
#endif

    bool onCreate() override;
    void onDestroy() override;
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

    nxui::Widget* focusRoot() override;

private:
    struct GridLayoutMetrics {
        float cellW = 150.f;
        float cellH = 150.f;
        float padX = 20.f;
        float padY = 16.f;
    };

    void loadResources();
    GridLayoutMetrics computeGridLayoutMetrics() const;
    GridLayoutMetrics computeGridLayoutMetrics(int columns, int rows) const;
    std::pair<int, int> folderGridDimensions(std::uint32_t folderId) const;
    void reflowHomeGrid();
    void buildGrid();
    void buildUserAvatarBar(bool loadImmediately = true);
    void loadNextUserAvatar();
    void appendAddUserButton();
    void wireUserAvatarNavigation();
    void composeRootPending(std::vector<PendingApp>& apps);
    GridModel buildRootFolderModel();
    GridModel buildOpenFolderModel(std::uint32_t folderId) const;
    void applyDisplayModel(GridModel model, std::uint64_t focusId, bool animate);
    void syncPageIndicator();
    void flipPageFromEdge(int dir);
    void requestOpenFolder(std::uint32_t folderId);
    void openCapturedFolder();
    void closeFolder(bool preserveEditMode = false);
    void createFolder(int targetSlot = -1);
    void renameFolder(std::uint32_t folderId);
    void showFolderContextMenu(std::uint32_t folderId);
    bool saveFoldersOrReport(const char* operation);
    std::string promptFolderName(const std::string& initial,
                                 const std::string& guide);
    void applyTheme();
    void applyThemeResources(const ThemePreset& preset);
    void retryPendingBackgroundImage();
    void applyUiLanguage();
    void rebuildThemeFromColors();
    ThemePreset buildEffectiveThemePreset();
    std::string resolveThemeAssetPath(const ThemePreset& preset, const std::string& rawPath) const;
    ThemePreset* findPresetPtr(const std::string& name);
    void deletePreset(const std::string& presetId);
    void startSoftwareDeletion(uint64_t titleId, const std::string& title,
                               bool closeGameOptionsOnSuccess);
    void syncSoftwareDeletion();
    void updateCursor();
    struct ActionHint {
        std::string icon;
        std::string label;
    };
    std::vector<ActionHint> buildActionHints();

    struct HintCapsule {
        std::string icon;
        std::string label;
        std::string outgoing;              // label fading out during a swap
        float width = 0.f;
        float widthFrom = 0.f, widthTo = 0.f;
        float widthT = 1.f;                // 0..1
        float swapT  = 1.f;                // 0..1
    };
    std::vector<HintCapsule> m_hintCapsules;
    void  syncHintCapsules(float dt);
    float hintCapsuleWidth(const std::string& icon, const std::string& label);
    void renderActionHintBar(nxui::Renderer& ren);
    void renderActionHintPanel(nxui::Renderer& ren);
    void renderPageArrows(nxui::Renderer& ren);
    bool pagingAvailable();

    struct PageArrowAnim {
        float show  = 0.f;   // 0..1
        float press = 0.f;   // 1 -> 0
    };
    PageArrowAnim m_arrowAnimLeft, m_arrowAnimRight;
    bool m_touchArrowLeft = false, m_touchArrowRight = false;

    nxui::Rect pageArrowRect(bool left);
    void kickPageArrow(int dir);
    bool flipPage(int dir);
    int findTitleIndex(uint64_t titleId) const;
    bool focusTitle(uint64_t titleId);
    void markSuspendedIcon(uint64_t titleId);
    void closeActiveOverlays();
    void handleTouch();
    std::shared_ptr<GlossyIcon> makeIcon(const AppEntry& entry);
    void wireFocusCallback();
    void wireGlobalActions();
    void toggleAccessibilitySpeech();
    bool handleAccessibilityToggleCombo();
    bool isCurrentFocusableWidget(nxui::Widget* w) const;
    std::string accessibilityPositionFor(nxui::Widget* w) const;
    void createSettings();
    void createThemeShop();
    void createGameOptions();
    void createFolderOptions();
    void createControllerTest();
    void reloadThemePresets();
    void refreshThemeShopState();
    std::vector<ThemeShopScreen::ThemeShopEntry> buildThemeShopEntries();
    void startThemePackageTransfer(const ThemeCatalogClient::Entry& entry, bool installMode);
    void syncThemePackageTransfer();
    void activateThemePreset(ThemePreset* preset, bool applyBundledSound);
    std::string resolveSoundPresetId(const std::string& preset) const;
    void loadSoundPreset(const std::string& preset);
    void changeSoundPreset(const std::string& preset);
    std::vector<std::string> scanAvailablePresets();
    void loadMenuLayout();
    void saveMenuLayout();
    bool quiesceWritersForPowerAction();
    void applyMenuLayoutToPending(std::vector<PendingApp>& apps);
    void startEditGhost(GlossyIcon* sourceIcon);
    void detachEditSourceIcon();
    void reattachEditSourceIcon();
    void stopEditGhost();
    void updateEditGhost(float dt);
    bool commitEditModePlacement();
    bool activateEditModeTarget();
    bool moveFocusedIcon(nxui::FocusDirection dir);
    void enterEditMode();
    void exitEditMode();
    void bindEditActions(GlossyIcon* icon);
    void unbindEditActions();
    bool isEditableIcon(nxui::Widget* w) const;
    void announceFocusedWidget(nxui::Widget* w);
    std::string accessibilityContextFor(nxui::Widget* w) const;
    std::string accessibilityActionsFor(nxui::Widget* w) const;

#ifdef SWITCHU_MENU
    void refreshAppList();
    void finalizeRefresh();
    void handleSystemAction(SysAction a);
    void showGameContextMenu(GlossyIcon* icon);
#endif

    nxui::Font  m_fontNormal;
    nxui::Font  m_fontSmall;
    nxui::Font  m_fontIcons;

    GridModel    m_model;
    nxui::Theme  m_theme;
    switchu::services::ClockService m_clockService;

    std::string              m_activePresetName = "Default Light";
    ThemeColorSet            m_activeColors;
    nxui::ThemeMode          m_activeMode = nxui::ThemeMode::Light;
    std::vector<ThemePreset> m_allPresets;
    ThemePreset              m_effectivePreset;

    std::shared_ptr<WaraWaraBackground> m_background;
    std::shared_ptr<IconGrid>          m_grid;
    std::shared_ptr<SelectionCursor>   m_cursor;
    std::shared_ptr<SelectionCursor>   m_pointerCursor;
    std::shared_ptr<DateTimeWidget>    m_clock;
    std::shared_ptr<BatteryWidget>     m_battery;
    std::shared_ptr<TitlePillWidget>   m_titlePill;
    std::shared_ptr<PageIndicator>     m_pageIndicator;
    std::shared_ptr<LaunchAnimation>   m_launchAnim;
    std::shared_ptr<OverlayDialog>     m_userSelect;
    std::shared_ptr<OverlayDialog>     m_dialog;
    std::shared_ptr<ProgressDialog>    m_progressDialog;
    std::shared_ptr<SettingsScreen>    m_settings;
    std::shared_ptr<ThemeShopScreen>   m_themeShop;
    std::shared_ptr<GameOptionsScreen> m_gameOptions;
    std::shared_ptr<FolderOptionsScreen> m_folderOptions;
    std::shared_ptr<ControllerTestScreen> m_controllerTest;

    nxui::Texture m_gameCardTex;
    nxui::Texture m_arrowTexLeft;
    nxui::Texture m_arrowTexRight;
    nxui::AnimatedFloat m_arrowCenterY;
    bool m_arrowCenterInit = false;

    std::shared_ptr<nxui::Box> m_bgLayer;
    std::shared_ptr<nxui::Box> m_contentLayer;
    std::shared_ptr<nxui::Box> m_overlayLayer;
    std::shared_ptr<nxui::Box> m_topHud;
    std::shared_ptr<nxui::Box> m_leftSidebar;
    std::shared_ptr<nxui::Box> m_rightSidebar;
    std::shared_ptr<nxui::Box> m_userAvatarBar;
    std::shared_ptr<FolderBackdrop> m_folderBackdrop;
    std::shared_ptr<nxui::GlassPanel> m_folderHeader;
    std::shared_ptr<nxui::Label> m_folderHeaderLabel;
    std::vector<std::shared_ptr<UserAvatarButton>> m_userAvatarButtons;

    AudioManager m_audio;
    AccessibilityManager m_accessibility;
    std::future<void>    m_audioFuture;
    std::future<void>    m_configSaveFuture;
    std::future<void>    m_themeDeleteFuture;
    bool                 m_audioStarted = false;
    bool                 m_musicFadeActive = false;
    std::vector<std::string> m_availablePresets;
    bool                 m_presetChangePending = false;
    std::string          m_loadedSoundPreset;
    std::string          m_pendingSoundPreset;

    struct ThemePackageTransferShared {
        std::mutex mutex;
        ThemeTransferState state;
        std::string themeId;
        bool installMode = false;
        std::string destinationPath;
        std::uint64_t revision = 0;
    };

    nxui::ThreadPool m_threadPool{2};
    SidebarManager  m_sidebar;
    AppletLauncher  m_launcher;
    AppListLoader   m_appLoader;
    IconStreamer    m_iconStreamer;
    SystemMessages  m_sysMsg;
    switchu::navigation::MenuNavigator m_navigator;

    bool m_showDebugOverlay  = false;
#ifdef SWITCHU_DEBUG_UI
    std::unique_ptr<DebugImGuiOverlay> m_debugOverlay;
#endif
    bool m_showWireframe     = false;
    bool m_editMode          = false;
    int  m_editSourceIndex   = -1;
    int  m_editOriginRootSlot = -1;
    int  m_editOriginFolderIndex = -1;
    std::uint32_t m_editOriginFolderId = 0;
    std::uint64_t m_editHeldTitleId = 0;
    std::string m_editHeldTitle;
    GlossyIcon* m_editBoundIcon = nullptr;
    GlossyIcon* m_editSourceIcon = nullptr;
    std::shared_ptr<GlossyIcon> m_editGhostIcon;
    nxui::Rect m_editGhostTargetRect {0.f, 0.f, 0.f, 0.f};
    float m_editGhostPulse = 0.f;
    std::vector<uint64_t> m_layoutSlots;
    bool m_layoutDirty = false;
    switchu::folders::FolderStore m_folderStore;
    std::vector<AppEntry> m_allApps;
    std::uint32_t m_openFolderId = 0;
    std::uint32_t m_requestedFolderId = 0;
    bool m_folderCaptureRequested = false;
    bool m_folderCaptureReady = false;
    bool m_gridSliding = false;

    int  m_touchHitIndex     = -1;
    bool m_touchOnFocused    = false;
    bool m_touchEditDragActive = false;
    UserAvatarButton* m_touchAvatarTarget = nullptr;
    bool m_touchAvatarWasFocused = false;
    int  m_deferredRefreshFrames = 0;
    bool m_refreshQueued         = false;
    int  m_refreshCooldownFrames = 0;
    bool m_asyncRefreshPending   = false;
    int  m_refreshPrevPage       = 0;

    AppConfig m_config;
    bool m_settingsNeedRefresh        = false;
    std::string m_loadedRegularFontPath;
    std::string m_loadedSmallFontPath;
    std::string m_loadedGameCardPath;
    std::string m_loadedBackgroundImagePath;
    bool m_backgroundImageLoaded      = false;
    std::string m_pendingBackgroundImagePath;
    int m_backgroundImageRetryFrames = 0;
    int m_backgroundImageRetryAttempts = 0;
    bool m_forceThemeResourceReload   = false;
    std::uint64_t m_gameOptionsTitleId = 0;
    std::uint32_t m_folderOptionsId = 0;
    nxui::Widget* m_dialogReturnFocus = nullptr;
    bool m_dialogWasActive            = false;
    bool m_suppressNextNavigateSfx    = false;
    bool m_pendingNetConnect          = false;
    int  m_deferredInitialAssetFrames = 0;
    int  m_deferredProfileFrames = 0;
    std::vector<AccountUid> m_pendingProfileUids;
    std::size_t m_pendingProfileIndex = 0;
    std::future<void> m_accessibilityFuture;
    bool m_accessibilityReady = false;
    std::uint64_t m_fastReturnStartupTick = 0;

    bool  m_audioInitPending = false;
    bool  m_audioHeldLogged  = false;
    bool m_fastReturnRequested = false;
    std::future<void> m_themePackageTransferFuture;
    std::future<void> m_softwareDeleteFuture;
    Result m_softwareDeleteResult = 0;
    std::string m_softwareDeleteTitle;
    bool m_softwareDeleteClosesGameOptions = false;
    std::shared_ptr<ThemePackageTransferShared> m_themePackageTransfer;
    std::uint64_t m_themePackageTransferUiRevision = 0;
    std::uint64_t m_themePackageTransferHandledRevision = 0;
#ifdef SWITCHU_MENU
    switchu::smi::OperationOutcome m_startupFailure{};
#endif
    int m_themeRenderDebugFrames = 0;

    float m_returnFadeTimer = 0.f;
    float m_tutorialStartupFadeTimer = 0.f;
    std::uint64_t m_tutorialStartupFadeDeadlineTick = 0;
    bool  m_tutorialStartupFade = false;
    bool m_hintPanelInitialized = false;
    bool m_hintCapsulesInitialized = false;
    bool m_accessibilityToggleComboHeld = false;
    bool m_plusExitPending = false;
    float m_plusExitPendingTimer = 0.f;
    nxui::AnimatedFloat m_hintPanelW{0.f};
    nxui::AnimatedFloat m_hintPanelH{0.f};
    nxui::AnimatedFloat m_hintContentReveal{1.f};
    std::string m_hintSignature;
    static constexpr float kReturnFadeInDur = 0.22f;
    static constexpr float kTutorialStartupFadeDur = 0.34f;
};
