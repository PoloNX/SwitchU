#pragma once

#include "ThemeCatalogClient.hpp"
#include "settings/TabbedOverlayScreen.hpp"

#include <nxui/core/Texture.hpp>

#include <cstdint>
#include <algorithm>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace themeshop::tabs {
class InstalledTab;
class CommunityTab;
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
    };

    ThemeShopScreen();
    ~ThemeShopScreen() override = default;

    void onMusicEnabledChange(BoolCb cb) { m_musicEnabledCb = std::move(cb); }
    void onMusicVolumeChange(FloatCb cb) { m_musicVolumeCb = std::move(cb); }
    void onSfxVolumeChange(FloatCb cb)   { m_sfxVolumeCb = std::move(cb); }
    void onGridColumnsChange(IntCb cb)   { m_gridColumnsCb = std::move(cb); }
    void onGridRowsChange(IntCb cb)      { m_gridRowsCb = std::move(cb); }
    void onActionHintStyleChange(IntCb cb) { m_actionHintStyleCb = std::move(cb); }
    void onNextTrack(VoidCb cb)          { m_nextTrackCb = std::move(cb); }
    void onThemeShopApply(StringCb cb)   { m_themeShopApplyCb = std::move(cb); }
    void onThemeShopDelete(StringCb cb)  { m_themeShopDeleteCb = std::move(cb); }
    void onThemeShopDownload(StringCb cb) { m_themeShopDownloadCb = std::move(cb); }
    void onThemeShopDownloadInstall(StringCb cb) { m_themeShopDownloadInstallCb = std::move(cb); }
    void onNetConnectRequest(VoidCb cb)  { m_netConnectCb = std::move(cb); }

    void setMusicState(bool enabled, float musicVol, float sfxVol) {
        m_musicEnabled = enabled;
        m_musicVolume = musicVol;
        m_sfxVolume = sfxVol;
    }
    void setGridLayoutState(int columns, int rows) {
        m_gridColumns = std::clamp(columns, 3, 8);
        m_gridRows = std::clamp(rows, 2, 5);
    }
    void setActionHintStyleState(const std::string& style) {
        m_actionHintStyle = style == "panel" ? 0 : 1;
    }

    void setThreadPool(nxui::ThreadPool* pool);
    void setRenderContext(nxui::GpuDevice* gpu, nxui::Renderer* renderer);
    void refreshCommunityCatalog();
    void requestRenderDiagnostics(int frames = 6) {
        if (frames > m_renderDebugFrames)
            m_renderDebugFrames = frames;
    }

    void setThemeShopState(const std::vector<ThemeShopEntry>& entries,
                           const std::string& activeId);
    void setPackageTransferState(const ThemeTransferState& state,
                                 std::string themeId,
                                 bool installMode);
    const ThemeShopEntry* selectedThemeShopEntry() const;
    const ThemeCatalogClient::Entry* selectedCommunityThemeEntry() const;
    const ThemeCatalogClient::Entry* findCommunityThemeEntry(const std::string& themeId) const;
    const std::string& communityCatalogUrl() const {
        return m_catalogClient.catalogUrl();
    }

protected:
    void buildTabs() override;
    bool usesCustomContentLayout() const override { return m_tabIndex < 2; }
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
        std::string url;
        std::vector<std::uint8_t> bytes;
        std::future<void> future;
        nxui::Texture texture;
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
        FullscreenPreview,
        DetailBackdrop,
    };

    enum class DetailFocusArea {
        Preview,
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
    PreviewPhase installedPreviewPhase(const std::string& previewPath) const;
    const nxui::Texture* installedPreviewTexture(const std::string& previewPath) const;
    void primeInstalledPreview(const std::string& previewPath);
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
    VoidCb m_nextTrackCb;
    StringCb m_themeShopApplyCb;
    StringCb m_themeShopDeleteCb;
    StringCb m_themeShopDownloadCb;
    StringCb m_themeShopDownloadInstallCb;
    VoidCb m_netConnectCb;

    bool m_musicEnabled = true;
    float m_musicVolume = 0.4f;
    float m_sfxVolume = 0.7f;
    int m_gridColumns = 5;
    int m_gridRows = 3;
    int m_actionHintStyle = 1;
    std::string m_searchQuery;
    std::vector<ThemeShopEntry> m_allThemeShopEntries;
    std::vector<ThemeShopEntry> m_themeShopEntries;
    std::string m_themeShopSelectedId;

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
    std::string m_communitySelectedId;
    std::string m_packageTransferThemeId;
    bool m_packageTransferInstallMode = false;
    bool m_hasPendingCommunityPreviewWork = false;
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
    int m_lastCustomTabIndex = -1;
    std::string m_lastPreviewPrimeKey;
    ThemeTouchTarget m_themeTouchTarget = ThemeTouchTarget::None;
    int m_themeTouchIndex = -1;
    float m_themeTouchStartX = 0.f;
    float m_themeTouchStartY = 0.f;
    int m_renderDebugFrames = 0;
};
