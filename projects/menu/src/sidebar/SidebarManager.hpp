#pragma once
#include "SidebarAnimation.hpp"
#include "widgets/AdaptiveIconLoad.hpp"
#include "widgets/AppletButton.hpp"
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/Theme.hpp>
#include <nxui/widgets/Widget.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SidebarManager {
public:
    struct Actions {
        std::function<void()> onAlbum;
        std::function<void()> onMiiEditor;
        std::function<void()> onControllers;
        std::function<void()> onSleep;
        std::function<void()> onSettings;
        std::function<void()> onMiiverse;
    };

    void build(nxui::GpuDevice& gpu, nxui::Renderer& ren,
               const std::string& assetsBase, const Actions& actions);

    void reloadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                      const std::string& assetsBase,
                      const std::string& customIconsBase = std::string(),
                      const std::string& iconPackId = std::string());
    void invalidateAssetsCache();

    void update(float dt, nxui::Widget* focusedWidget);

    // The dynamic-line HOME layout turns the two side columns into one
    // horizontal control strip between the profile bar and the applications.
    void setDynamicLineLayout(bool enabled);
    void setDynamicLineDownAction(std::function<void()> action);
    void setDynamicLineUpTarget(nxui::Widget* target);
    void setDynamicLineProfileTarget(nxui::Widget* profile);

    void applyTheme(const nxui::Theme& theme);
    // Colour icon contrast adapt: follow HOME chrome while a single-row hero
    // is on screen (same light/dark flip as the rest of the UI).
    void setColourIconContrast(bool enabled);
    bool colourIconContrast() const { return m_colourContrastAdapt; }
    // Photoshop-style HSL adjust baked into colour glyphs on the next reload.
    void setIconHslAdjust(const switchu::icons::HslAdjust& adj);
    const switchu::icons::HslAdjust& iconHslAdjust() const { return m_hslAdjust; }

    std::vector<std::shared_ptr<AppletButton>>& leftButtons()  { return m_leftButtons; }
    std::vector<std::shared_ptr<AppletButton>>& rightButtons() { return m_rightButtons; }
    const std::vector<std::shared_ptr<AppletButton>>& leftButtons() const  { return m_leftButtons; }
    const std::vector<std::shared_ptr<AppletButton>>& rightButtons() const { return m_rightButtons; }

    AppletButton*  albumButton()    const { return m_albumButton; }
    nxui::Widget*  settingsButton() const { return m_settingsButton; }
    nxui::Widget*  themeShopButton() const { return m_themeShopButton; }

private:
    void loadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                    const std::string& assetsBase,
                    const std::string& customIconsBase,
                    const std::string& iconPackId);
    void tryLoadAnimation(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                          const std::string& webpPath, AppletButton* button,
                          nxui::Texture* staticIcon);

    std::vector<nxui::Texture> m_icons;
    // White×alpha twins for colour glyphs (empty/invalid when icon is mono).
    std::vector<nxui::Texture> m_iconContrastMasks;
    std::vector<float> m_iconMeanLumas;
    bool m_colourContrastAdapt = true;
    switchu::icons::HslAdjust m_hslAdjust;
    std::vector<std::shared_ptr<AppletButton>> m_leftButtons;
    std::vector<std::shared_ptr<AppletButton>> m_rightButtons;
    AppletButton*  m_albumButton    = nullptr;
    nxui::Widget*  m_settingsButton = nullptr;
    nxui::Widget*  m_themeShopButton = nullptr;

    struct AnimEntry {
        SidebarAnimation anim;
        AppletButton*    button    = nullptr;
        nxui::Texture*   staticTex = nullptr;
    };
    std::vector<AnimEntry> m_anims;
    std::string m_loadedAssetsBase;
    std::string m_loadedCustomIconsBase;
    std::string m_loadedIconPackId;
    bool m_assetsLoaded = false;
};
