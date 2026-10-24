#pragma once

#include "ThemeCatalogClient.hpp"
#include "settings/TabbedOverlayScreen.hpp"
#include "widgets/AdaptiveIconLoad.hpp"

#include <nxui/core/Input.hpp>
#include <nxui/core/Texture.hpp>

#include <cstdint>
#include <algorithm>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace themeshop::tabs {
class InstalledTab;
class CommunityTab;
class IconsTab;
class OptionsTab;
}

namespace nxui {
class GpuDevice;
class Renderer;
}

class ThemeShopScreen : public TabbedOverlayScreen {
public:
    struct ThemeShopEntry {
        std::string id;
        std::string name;
        std::string author;
        std::string version;
        std::string source;
        std::string soundPreset;
        std::string coverPath;
        bool active = false;
        bool removable = false;
        // Colour glyph pack (spectrum badge + custom tint controls in detail).
        bool colourPack = false;
        // Theme ships sidebar/chrome icons (Community/Installed badge).
        bool hasIcons = false;
        // Icon packs: present on the console / shipped with SwitchU / online.
        bool installed = true;
        bool builtin = false;
        bool catalog = false;
        // Remote preview for catalog packs that are not installed yet.
        std::string coverUrl;
        std::string description;
    };

    ThemeShopScreen();
    ~ThemeShopScreen() override = default;

    void onMusicEnabledChange(BoolCb cb) { m_musicEnabledCb = std::move(cb); }
    void onMusicVolumeChange(FloatCb cb) { m_musicVolumeCb = std::move(cb); }
    void onSfxVolumeChange(FloatCb cb)   { m_sfxVolumeCb = std::move(cb); }
    void onGridColumnsChange(IntCb cb)   { m_gridColumnsCb = std::move(cb); }
    void onGridRowsChange(IntCb cb)      { m_gridRowsCb = std::move(cb); }
    void onActionHintStyleChange(IntCb cb) { m_actionHintStyleCb = std::move(cb); }
    void onSortLargeTilesChange(BoolCb cb) { m_sortLargeTilesCb = std::move(cb); }
    void onIconContrastAdaptChange(BoolCb cb) { m_iconContrastAdaptCb = std::move(cb); }
    struct IconPackTintState {
        bool enabled = false;
        float hue = 0.5f;
        float sat = 0.5f;
        float light = 0.5f;
    };
    using IconPackTintCb = std::function<void(const std::string& packId, const IconPackTintState& tint)>;
    void onIconPackTintChange(IconPackTintCb cb) { m_iconPackTintCb = std::move(cb); }
    bool detailOpen() const { return m_detailOpen; }
    void setInputSource(const nxui::Input* input) { m_input = input; }
    void onAutoThemeOpen(VoidCb cb) { m_autoThemeOpenCb = std::move(cb); }
    void onCursorMotionModeChange(IntCb cb) { m_cursorMotionModeCb = std::move(cb); }
    void onPageTransitionModeChange(IntCb cb) { m_pageTransitionModeCb = std::move(cb); }
    void onNextTrack(VoidCb cb)          { m_nextTrackCb = std::move(cb); }
    void onThemeShopApply(StringCb cb)   { m_themeShopApplyCb = std::move(cb); }
    void onIconPackApply(StringCb cb)    { m_iconPackApplyCb = std::move(cb); }
    void onIconPackDownload(StringCb cb) { m_iconPackDownloadCb = std::move(cb); }
    void onIconPackDownloadApply(StringCb cb) { m_iconPackDownloadApplyCb = std::move(cb); }
    void onIconPackDelete(StringCb cb)   { m_iconPackDeleteCb = std::move(cb); }
    // Catalog icons finished (re)loading; the app rebuilds the pack list.
    void onIconCatalogUpdated(VoidCb cb) { m_iconCatalogUpdatedCb = std::move(cb); }
    void onThemeShopDelete(StringCb cb)  { m_themeShopDeleteCb = std::move(cb); }
    void onThemeShopDownload(StringCb cb) { m_themeShopDownloadCb = std::move(cb); }
    void onThemeShopDownloadInstall(StringCb cb) { m_themeShopDownloadInstallCb = std::move(cb); }
    void onNetConnectRequest(VoidCb cb)  { m_netConnectCb = std::move(cb); }
    void onSearchRequest(StringCb cb) { m_searchRequestCb = std::move(cb); }
    void setSearchQuery(std::string query);

    void setMusicState(bool enabled, float musicVol, float sfxVol) {
        m_musicEnabled = enabled;
        m_musicVolume = musicVol;
        m_sfxVolume = sfxVol;
    }
    void setGridLayoutState(int columns, int rows) {
        m_gridColumns = std::clamp(columns, 1, 8);
        m_gridRows = std::clamp(rows, 1, 5);
    }
    void setSortLargeTilesState(bool enabled) { m_sortLargeTiles = enabled; }
    void setIconContrastAdaptState(bool enabled) { m_iconContrastAdapt = enabled; }
    void setIconPackTintState(const std::unordered_map<std::string, IconPackTintState>& tints) {
        m_iconPackTints = tints;
    }
    void invalidateIconPackPreviews();
    // Persist draft tint (Theme Shop → config/HOME). Call before Apply.
    void commitDraftIconPackTint();
    void setActionHintStyleState(const std::string& style) {
        m_actionHintStyle = style == "panel" ? 0 : 1;
    }
    void setAutoThemeSummary(std::string summary) {
        m_autoThemeSummary = std::move(summary);
    }
    void setCursorMotionModeState(int mode) {
        m_cursorMotionMode = std::clamp(mode, 0, 1);
    }
    void setPageTransitionModeState(int mode) {
        m_pageTransitionMode = std::clamp(mode, 0, 1);
    }

    void setThreadPool(nxui::ThreadPool* pool);
    void setRenderContext(nxui::GpuDevice* gpu, nxui::Renderer* renderer);
    void refreshCommunityCatalog(bool forceNetwork = false);
    void requestRenderDiagnostics(int frames = 6) {
        if (frames > m_renderDebugFrames)
            m_renderDebugFrames = frames;
    }

    void setThemeShopState(const std::vector<ThemeShopEntry>& entries,
                           const std::string& activeId);
    void setIconPackState(const std::vector<ThemeShopEntry>& entries,
                          const std::string& activeId);
    void setPackageTransferState(const ThemeTransferState& state,
                                 std::string themeId,
                                 bool installMode);
    const ThemeShopEntry* selectedThemeShopEntry() const;
    const ThemeCatalogClient::Entry* selectedCommunityThemeEntry() const;
    const ThemeCatalogClient::Entry* findCommunityThemeEntry(const std::string& themeId) const;
    const std::vector<ThemeCatalogClient::IconEntry>& catalogIconEntries() const {
        return m_catalogIconEntries;
    }
    const std::string& communityCatalogUrl() const {
        return m_catalogClient.catalogUrl();
    }

protected:
    void buildTabs() override;
    bool usesCustomContentLayout() const override { return m_tabIndex < 3; }
    void drawCustomContent(nxui::Renderer& ren, const nxui::Rect& panel, const nxui::Rect& content, float opacity) override;
    void updateCustomContent(float dt) override;
    bool handleCustomPressA() override;
    bool handleCustomPressB() override;
    bool handleCustomPressX() override;
    bool handleCustomNavUp() override;
    bool handleCustomNavDown() override;
    bool handleCustomNavLeft() override;
    bool handleCustomNavRight() override;
    bool handleCustomTouch(nxui::Input& input, const nxui::Rect& panel, const nxui::Rect& tabs, const nxui::Rect& content) override;
    void currentAccessibilityParts(std::string& context,
                                   std::string& position,
                                   std::string& summary,
                                   bool& forceRepeat) const override;
    std::string currentAccessibilitySummary() const override;

private:
    friend class themeshop::tabs::InstalledTab;
    friend class themeshop::tabs::CommunityTab;
    friend class themeshop::tabs::IconsTab;
    friend class themeshop::tabs::OptionsTab;

    enum class PreviewPhase {
        Idle,
        Loading,
        Downloaded,
        Ready,
        Failed,
    };

    struct PreviewImageState {
        std::mutex mutex;
        PreviewPhase phase = PreviewPhase::Idle;
        int failureCount = 0;
        bool cancelled = false;
        bool adaptiveRequested = false; // load via AdaptiveIconLoad
        bool adaptive = false;          // mono result -> tint with chrome
        bool hasContrastMask = false;   // colour pack coverage twin for chrome follow
        std::uint32_t hslKey = 0;       // custom-tint fingerprint for cache reuse
        int maxSide = 0;                // decode budget used for this slot
        int pixelW = 0;
        int pixelH = 0;
        std::string url;
        std::vector<std::uint8_t> bytes;       // compressed community download
        std::vector<std::uint8_t> pixels;      // decoded RGBA (installed/icons)
        std::vector<std::uint8_t> maskPixels;  // optional contrast twin
        std::future<void> future;
        nxui::Texture texture;
        nxui::Texture contrastMask;
    };

    enum class ThemeTouchTarget {
        None,
        Search,
        Refresh,
        PagePrev,
        PageNext,
        GridCard,
        DetailPreviewControl,
        DetailButton,
        DetailTintToggle,
        DetailTintHue,
        DetailTintSat,
        DetailTintLight,
        FullscreenPreview,
        DetailBackdrop,
    };

    enum class DetailFocusArea {
        Preview,
        TintToggle,
        TintHue,
        TintSat,
        TintLight,
        Buttons,
    };

    enum class ContentFocusArea {
        Grid,
        Header,
        Pager,
    };

    bool pollCommunityCatalog();
    void syncCommunityCatalog(const ThemeCatalogClient::Snapshot& snapshot);
    bool isCommunityTab() const;
    bool isIconsTab() const;
    bool isInstalledTab() const;
    int currentEntryCount() const;
    int currentSelectedIndex() const;
    void setCurrentSelectedIndex(int idx);
    int currentPage() const;
    int pageCount() const;
    void setCurrentPage(int page);
    void stepPage(int delta);
    void applySearchFilter();
    bool promptSearchQuery();
    int detailScreenshotCount() const;
    std::string currentDetailCommunityPreviewPath() const;
    void clampDetailScreenshotIndex();
    void stepDetailScreenshot(int delta);
    int& currentScrollRowRef();
    void ensureSelectionVisible();
    void openDetail();
    void closeDetail();
    int detailButtonCount() const;
    void activateDetailButton(int buttonIndex);
    int hitTestGridCard(const nxui::Rect& content, float x, float y) const;
    std::string resolveCommunityPreviewUrl(const std::string& previewPath) const;
    std::string resolveCommunityPreviewUrl(const ThemeCatalogClient::Entry& entry) const;
    std::shared_ptr<PreviewImageState> communityPreviewState(const std::string& previewPath) const;
    std::shared_ptr<PreviewImageState> communityPreviewState(const ThemeCatalogClient::Entry& entry) const;
    PreviewPhase communityPreviewPhase(const std::string& previewPath) const;
    PreviewPhase communityPreviewPhase(const ThemeCatalogClient::Entry& entry) const;
    const nxui::Texture* communityPreviewTexture(const std::string& previewPath) const;
    const nxui::Texture* communityPreviewTexture(const ThemeCatalogClient::Entry& entry) const;
    std::string installedPreviewCacheKey(const std::string& previewPath,
                                         bool adaptiveLoad,
                                         const std::string& iconPackId,
                                         const switchu::icons::HslAdjust* hslOverride = nullptr) const;
    PreviewPhase installedPreviewPhase(const std::string& previewPath,
                                       bool adaptiveLoad = false,
                                       const std::string& iconPackId = {},
                                       const switchu::icons::HslAdjust* hslOverride = nullptr) const;
    const nxui::Texture* installedPreviewTexture(const std::string& previewPath,
                                                 bool adaptiveLoad = false,
                                                 const std::string& iconPackId = {},
                                                 const switchu::icons::HslAdjust* hslOverride = nullptr) const;
    bool installedPreviewAdaptive(const std::string& previewPath,
                                  bool adaptiveLoad = false,
                                  const std::string& iconPackId = {},
                                  const switchu::icons::HslAdjust* hslOverride = nullptr) const;
    const nxui::Texture* installedPreviewContrastMask(const std::string& previewPath,
                                                      bool adaptiveLoad = false,
                                                      const std::string& iconPackId = {},
                                                      const switchu::icons::HslAdjust* hslOverride = nullptr) const;
    void primeInstalledPreview(const std::string& previewPath,
                               bool adaptiveLoad = false,
                               const std::string& iconPackId = {},
                               const switchu::icons::HslAdjust* hslOverride = nullptr,
                               int maxSide = 0);
    void syncFinishedInstalledPreviewLoads();
    void trimInstalledPreviewCache();
    bool selectedIconPackIsColour() const;
    IconPackTintState iconPackTintFor(const std::string& packId) const;
    IconPackTintState& selectedIconPackTint();
    const IconPackTintState& selectedIconPackTint() const;
    bool selectedIconCustomTint() const { return selectedIconPackTint().enabled; }
    void beginIconPackTintDraft();
    void discardIconPackTintDraft();
    void setIconCustomTintEnabled(bool enabled);
    void setIconTintHue(float hue01);
    void setIconTintSat(float sat01);
    void setIconTintLight(float light01);
    void nudgeIconTintHue(float delta);
    void nudgeIconTintSat(float delta);
    void nudgeIconTintLight(float delta);
    void emitIconPackTint(const std::string& packId, const IconPackTintState& tint);
    switchu::icons::HslAdjust hslAdjustForPack(const std::string& packId) const;
    switchu::icons::HslAdjust hslAdjustFromTint(const IconPackTintState& tint) const;
    void updateTintSliderAnalog(float dt);
    void primeCommunityPreview(const std::string& previewPath);
    void primeCommunityPreview(const ThemeCatalogClient::Entry& entry);
    void primeVisibleCommunityPreviews();
    void syncFinishedCommunityPreviewLoads();
    void clearCommunityPreviewCache();
    void trimCommunityPreviewCache();

    BoolCb m_musicEnabledCb;
    FloatCb m_musicVolumeCb;
    FloatCb m_sfxVolumeCb;
    IntCb m_gridColumnsCb;
    IntCb m_gridRowsCb;
    IntCb m_actionHintStyleCb;
    BoolCb m_sortLargeTilesCb;
    BoolCb m_iconContrastAdaptCb;
    IconPackTintCb m_iconPackTintCb;
    VoidCb m_autoThemeOpenCb;
    IntCb m_cursorMotionModeCb;
    IntCb m_pageTransitionModeCb;
    VoidCb m_nextTrackCb;
    StringCb m_themeShopApplyCb;
    StringCb m_iconPackApplyCb;
    StringCb m_iconPackDownloadCb;
    StringCb m_iconPackDownloadApplyCb;
    StringCb m_iconPackDeleteCb;
    VoidCb m_iconCatalogUpdatedCb;
    StringCb m_themeShopDeleteCb;
    StringCb m_themeShopDownloadCb;
    StringCb m_themeShopDownloadInstallCb;
    VoidCb m_netConnectCb;
    StringCb m_searchRequestCb;

    bool m_musicEnabled = true;
    float m_musicVolume = 0.4f;
    float m_sfxVolume = 0.7f;
    int m_gridColumns = 5;
    int m_gridRows = 3;
    int m_actionHintStyle = 1;
    bool m_sortLargeTiles = true;
    bool m_iconContrastAdapt = true;
    // Committed tints (match config / HOME / Icons grid cards).
    std::unordered_map<std::string, IconPackTintState> m_iconPackTints;
    // Live HSL edit while the detail sheet is open - preview only until Apply.
    bool m_draftTintActive = false;
    std::string m_draftTintPackId;
    IconPackTintState m_draftTint;
    float m_tintSliderHoldTime = 0.f;
    const nxui::Input* m_input = nullptr;
    std::string m_autoThemeSummary;
    int m_cursorMotionMode = 0;
    int m_pageTransitionMode = 0;
    std::string m_searchQuery;
    std::vector<ThemeShopEntry> m_allThemeShopEntries;
    std::vector<ThemeShopEntry> m_themeShopEntries;
    std::string m_themeShopSelectedId;
    std::vector<ThemeShopEntry> m_allIconPackEntries;
    std::vector<ThemeShopEntry> m_iconPackEntries;
    std::string m_iconPackSelectedId;

    nxui::ThreadPool* m_threadPool = nullptr;
    nxui::GpuDevice* m_gpu = nullptr;
    nxui::Renderer* m_renderer = nullptr;
    ThemeCatalogClient m_catalogClient;
    ThemeTransferState m_communityTransferState;
    ThemeTransferState m_packageTransferState;
    std::unordered_map<std::string, std::shared_ptr<PreviewImageState>> m_communityPreviewCache;
    std::unordered_map<std::string, std::shared_ptr<PreviewImageState>> m_installedPreviewCache;
    std::vector<ThemeCatalogClient::Entry> m_allCommunityEntries;
    std::vector<ThemeCatalogClient::Entry> m_communityEntries;
    std::vector<ThemeCatalogClient::IconEntry> m_catalogIconEntries;
    std::string m_communitySelectedId;
    std::string m_packageTransferThemeId;
    bool m_packageTransferInstallMode = false;
    bool m_hasPendingCommunityPreviewWork = false;
    bool m_hasPendingInstalledPreviewWork = false;
    std::uint64_t m_communityRevision = 0;
    bool m_detailOpen = false;
    bool m_detailFullscreen = false;
    ContentFocusArea m_contentFocusArea = ContentFocusArea::Grid;
    DetailFocusArea m_detailFocusArea = DetailFocusArea::Buttons;
    int m_headerButtonIndex = 0;
    int m_pageButtonIndex = 1;
    int m_detailButtonIndex = 0;
    int m_detailPreviewButtonIndex = 0;
    int m_detailScreenshotIndex = 0;
    nxui::AnimatedFloat m_detailSheetAnim{0.f};
    nxui::AnimatedFloat m_detailFullscreenAnim{0.f};
    int m_installedScrollRow = 0;
    int m_communityScrollRow = 0;
    int m_iconPackScrollRow = 0;
    int m_lastCustomTabIndex = -1;
    std::string m_lastPreviewPrimeKey;
    ThemeTouchTarget m_themeTouchTarget = ThemeTouchTarget::None;
    int m_themeTouchIndex = -1;
    float m_themeTouchStartX = 0.f;
    float m_themeTouchStartY = 0.f;
    int m_renderDebugFrames = 0;
};
