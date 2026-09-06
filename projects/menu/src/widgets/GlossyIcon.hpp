#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Animation.hpp>
#include "core/GridModel.hpp"
#include "sidebar/SidebarAnimation.hpp"
#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <future>

namespace nxui { class Font; class ThreadPool; }
struct WidgetGifDecodeState;



class GlossyIcon : public nxui::GlassWidget {
public:
    GlossyIcon();
    ~GlossyIcon() override;

    void setTitle(const std::string& t) { m_title = t; }
    const std::string& title() const    { return m_title; }

    void setTexture(nxui::Texture* tex) { m_tex = tex; }
    nxui::Texture* texture() const      { return m_tex; }

    void setGameCardTexture(nxui::Texture* tex) { m_gameCardTex = tex; }
    nxui::Texture* gameCardTexture() const      { return m_gameCardTex; }

    void setTitleId(uint64_t id)  { m_titleId = id; }
    uint64_t titleId() const      { return m_titleId; }

    void setSuspended(bool s)     { m_suspended = s; }
    bool isSuspended() const      { return m_suspended; }

    void setIsGameCard(bool gc)     { m_isGameCard = gc; }
    bool isGameCard() const         { return m_isGameCard; }

    void setNotLaunchable(bool nl)  { m_notLaunchable = nl; }
    bool isNotLaunchable() const    { return m_notLaunchable; }
    void setLoadingColor(const nxui::Color& color) { m_loadingColor = color; }
    void setEntryKind(GridEntryKind kind) { m_entryKind = kind; }
    GridEntryKind entryKind() const { return m_entryKind; }
    void setFolderPreviewCount(int count) { m_folderPreviewCount = count; }
    void setFolderVisualSeed(std::uint32_t seed) { m_folderVisualSeed = seed; }
    void setFolderColorIndex(int index) { m_folderColorIndex = index; }
    void setFolderStyleIndex(int index) { m_folderStyleIndex = index; }
    void setFont(nxui::Font* font) { m_font = font; }  // folder tiles carry their name
    void setWidgetData(switchu::widgets::WidgetType type, int columns, int rows,
                       std::string primary, std::string secondary,
                       const std::string& assetPath,
                       nxui::GpuDevice* gpu, nxui::Renderer* renderer,
                       bool deferAssetLoad = false);
    bool hasWidgetImageAsset() const { return !m_widgetAssetPath.empty(); }
    const std::string& widgetImageAssetPath() const { return m_widgetAssetPath; }
    bool isWidgetImageAssetLoaded() const;
    bool widgetImageAssetLoadAttempted() const { return m_widgetAssetLoadAttempted; }
    bool loadWidgetImageAsset(nxui::GpuDevice& gpu, nxui::Renderer& renderer);
    bool startWidgetImageAssetLoad(nxui::ThreadPool& threadPool,
                                   nxui::GpuDevice& gpu,
                                   nxui::Renderer& renderer);
    bool pollWidgetImageAssetLoad(nxui::GpuDevice& gpu,
                                  nxui::Renderer& renderer);
    bool isWidgetImageAssetLoading() const;
    bool unloadWidgetImageAsset();
    void allowWidgetImageAssetRetry();
    void setWidgetHeader(std::string header) { m_widgetHeader = std::move(header); }
    void setWidgetGameAssets(std::uint64_t titleId,
                             const std::string& heroPath,
                             const std::string& logoPath,
                             const std::vector<std::uint8_t>& iconData,
                             nxui::GpuDevice* gpu, nxui::Renderer* renderer);
    void setWidgetGameTextures(std::uint64_t titleId,
                               nxui::Texture* hero,
                               nxui::Texture* logo,
                               nxui::Texture* gameIcon);
    switchu::widgets::WidgetType widgetType() const { return m_widgetType; }
    std::uint64_t widgetGameTitleId() const { return m_widgetGameTitleId; }
    void copyWidgetPresentationFrom(GlossyIcon& source);
    void setGridSpan(int columns, int rows) {
        m_widgetColumns = std::max(1, columns);
        m_widgetRows = std::max(1, rows);
    }
    void setConsoleBattery(int percentage, bool charging) {
        m_consoleBatteryPercent = std::clamp(percentage, 0, 100);
        m_consoleBatteryCharging = charging;
    }
    void setBatteryIconTextures(nxui::Texture* console,
                                nxui::Texture* joyconLeft,
                                nxui::Texture* joyconRight) {
        m_batteryConsoleIcon = console;
        m_batteryJoyconLeftIcon = joyconLeft;
        m_batteryJoyconRightIcon = joyconRight;
    }
    void setWideGameTextures(nxui::Texture* hero, nxui::Texture* logo) {
        m_wideGameHero = hero;
        m_wideGameLogo = logo;
    }
    int gridSpanColumns() const { return m_widgetColumns; }
    int gridSpanRows() const { return m_widgetRows; }

    void startAppear(float delay);
    void forceVisible();

    void setFocusable(bool f) { m_focusable = f; }
    bool isFocusable() const override { return m_focusable; }
    void onFocusGained() override;
    void onFocusLost() override;

protected:
    void onRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;

private:
    std::string m_title;
    nxui::Texture*    m_tex = nullptr;
    nxui::Texture*    m_gameCardTex = nullptr;
    nxui::Texture*    m_wideGameHero = nullptr;
    nxui::Texture*    m_wideGameLogo = nullptr;
    uint64_t    m_titleId = 0;
    bool        m_focused = false;
    bool        m_focusable = true;
    bool        m_suspended = false;
    bool        m_isGameCard = false;
    bool        m_notLaunchable = false;
    nxui::Color m_loadingColor = nxui::Color::white();
    float       m_suspendPulse = 0.f;
    float       m_batteryRefreshTimer = 0.f;
    int         m_consoleBatteryPercent = 0;
    bool        m_consoleBatteryCharging = false;
    struct ControllerBattery {
        int percent = 0;
        bool charging = false;
        std::string label;
    };
    std::vector<ControllerBattery> m_controllerBatteries;
    nxui::Texture* m_batteryConsoleIcon = nullptr;
    nxui::Texture* m_batteryJoyconLeftIcon = nullptr;
    nxui::Texture* m_batteryJoyconRightIcon = nullptr;

    nxui::AnimatedFloat m_animScale;
    nxui::AnimatedFloat m_appearOpacity;
    nxui::AnimatedFloat m_focusScale;
    nxui::AnimatedFloat m_focusGlow;
    float         m_appearDelay = 0.f;
    float         m_appearTimer = 0.f;
    bool          m_appearing   = false;
    GridEntryKind m_entryKind = GridEntryKind::Application;
    int           m_folderPreviewCount = 0;
    std::uint32_t m_folderVisualSeed = 0;
    int           m_folderColorIndex = 0;
    int           m_folderStyleIndex = 1;
    nxui::Font*   m_font = nullptr;
    switchu::widgets::WidgetType m_widgetType = switchu::widgets::WidgetType::Clock;
    int m_widgetColumns = 1;
    int m_widgetRows = 1;
    std::string m_widgetPrimary;
    std::string m_widgetSecondary;
    std::string m_widgetHeader;
    std::string m_widgetAssetPath;
    bool m_widgetAssetLoadAttempted = false;
    std::shared_ptr<WidgetGifDecodeState> m_widgetGifDecode;
    std::future<void> m_widgetGifDecodeFuture;
    std::size_t m_widgetGifUploadIndex = 0;
    std::unique_ptr<nxui::Texture> m_widgetTexture;
    nxui::Texture* m_widgetExternalTexture = nullptr;
    std::unique_ptr<nxui::Texture> m_widgetHeroTexture;
    std::unique_ptr<nxui::Texture> m_widgetLogoTexture;
    std::unique_ptr<nxui::Texture> m_widgetGameIconTexture;
    nxui::Texture* m_widgetHero = nullptr;
    nxui::Texture* m_widgetLogo = nullptr;
    nxui::Texture* m_widgetGameIcon = nullptr;
    std::uint64_t m_widgetGameTitleId = 0;
    SidebarAnimation m_widgetAnimation;
};
