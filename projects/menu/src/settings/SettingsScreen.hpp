#pragma once
#include <nxui/core/Types.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/widgets/GlassWidget.hpp>
#include "widgets/SelectionCursor.hpp"
#include "core/ThemePreset.hpp"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

class OverlayDialog;

namespace settings::tabs {
class SystemTab;
class AudioTab;
class DisplayTab;
class InternetTab;
class ControllersTab;
class BluetoothTab;
class SleepTab;
class MusicTab;
class ThemeTab;
class AboutTab;
}

class SettingsScreen : public nxui::GlassWidget {
public:
    SettingsScreen();
    ~SettingsScreen();

    void setFont(nxui::Font* f)      { m_font = f; }
    void setSmallFont(nxui::Font* f)  { m_smallFont = f; }
    void setTheme(const nxui::Theme* t);

    void show();
    void hide();
    bool isActive() const { return m_active || m_animating; }
    bool isFullyVisible() const { return m_active && !m_animating; }

    void rebuildCurrentTab();
    void handleTouch(nxui::Input& input);
    void warmup();

    struct DialogButtonDef {
        std::string label;
        std::function<void()> onPress;
    };
    using DialogRequestCb = std::function<void(const std::string& title,
                                               const std::string& msg,
                                               std::vector<DialogButtonDef> buttons)>;
    void onDialogRequest(DialogRequestCb cb) { m_dialogRequestCb = std::move(cb); }
    void requestDialog(const std::string& title, const std::string& msg,
                       std::vector<DialogButtonDef> buttons);
    void requestToast(const std::string& msg, float holdSeconds = 2.5f);

    using ThemeToggleCb = std::function<void(bool dark)>;
    using BoolCb = std::function<void(bool)>;
    using FloatCb = std::function<void(float)>;
    using IntCb = std::function<void(int)>;
    using VoidCb = std::function<void()>;
    using StringCb = std::function<void(const std::string&)>;
    using ColorChangeCb = std::function<void(const std::string& key, float value)>;
    using PresetChangeCb = std::function<void(const std::string& presetName)>;
    void onThemeToggle(ThemeToggleCb cb) { m_themeToggleCb = std::move(cb); }
    void onThemeColorChange(ColorChangeCb cb) { m_themeColorChangeCb = std::move(cb); }
    void onThemePresetChange(PresetChangeCb cb) { m_themePresetChangeCb = std::move(cb); }
    void onThemeReset(VoidCb cb) { m_themeResetCb = std::move(cb); }
    void onThemeSave(VoidCb cb)  { m_themeSaveCb = std::move(cb); }
    void onThemeManage(VoidCb cb) { m_themeManageCb = std::move(cb); }
    void onThemeModeChange(BoolCb cb) { m_themeModeCb = std::move(cb); }
    void onMusicEnabledChange(BoolCb cb) { m_musicEnabledCb = std::move(cb); }
    void onMusicVolumeChange(FloatCb cb) { m_musicVolumeCb = std::move(cb); }
    void onSfxVolumeChange(FloatCb cb)   { m_sfxVolumeCb = std::move(cb); }
    void onNextTrack(VoidCb cb)          { m_nextTrackCb = std::move(cb); }
    void onNavigateSfx(VoidCb cb)        { m_navSfxCb = std::move(cb); }
    void onActivateSfx(VoidCb cb)        { m_activateSfxCb = std::move(cb); }
    void onCloseSfx(VoidCb cb)           { m_closeSfxCb = std::move(cb); }
    void onToggleSfx(BoolCb cb)          { m_toggleSfxCb = std::move(cb); }
    void onSliderSfx(BoolCb cb)          { m_sliderSfxCb = std::move(cb); }
    void onWireframeChange(BoolCb cb)    { m_wireframeCb = std::move(cb); }
    void onGridColumnsChange(IntCb cb)   { m_gridColumnsCb = std::move(cb); }
    void onGridRowsChange(IntCb cb)      { m_gridRowsCb = std::move(cb); }
    void onUiLanguageChange(StringCb cb)  { m_uiLanguageCb = std::move(cb); }
    void onSoundPresetChange(StringCb cb) { m_soundPresetCb = std::move(cb); }
    void onClosed(VoidCb cb)             { m_closedCb = std::move(cb); }
    void onNetConnect(VoidCb cb)         { m_netConnectCb = std::move(cb); }
    void setMusicState(bool enabled, float musicVol, float sfxVol) {
        m_musicEnabled = enabled;
        m_musicVolume = musicVol;
        m_sfxVolume = sfxVol;
    }
    void setWireframeState(bool enabled) { m_wireframeEnabled = enabled; }
    void setGridLayoutState(int columns, int rows) {
        m_gridColumns = std::clamp(columns, 3, 8);
        m_gridRows = std::clamp(rows, 2, 5);
    }
    void setUiLanguageOverride(const std::string& tag) { m_uiLanguageOverride = tag.empty() ? "auto" : tag; }
    void setSoundPresetState(const std::string& current, const std::vector<std::string>& available) {
        m_soundPreset = current;
        m_soundPresets = available;
    }
    void refreshTranslations();

    void setThemePresetState(const std::string& name,
                             const std::vector<std::string>& names,
                             const ThemeColorSet& colors,
                             bool modeDark) {
        m_themePresetName = name;
        m_themePresetNames = names;
        m_themeColors = colors;
        m_themeModeDark = modeDark;
    }
    void updateThemeSliders(const ThemeColorSet& colors);
    void updateThemePresetList(const std::vector<std::string>& names, const std::string& activeName);

    enum class ItemType { Info, Toggle, Slider, Progress, Selector, Action, Section, ColorPicker };

    struct SettingItem {
        std::string label;
        ItemType    type = ItemType::Info;
        std::string description;

        bool                     boolVal   = false;
        float                    floatVal  = 0.f;
        int                      intVal    = 0;
        float                    colorH = 0.f, colorS = 0.f, colorL = 0.f;
        std::vector<std::string> options;
        std::string              infoText;
        float                    anim01 = 0.f;

        std::function<void(SettingItem&)> onChange;

        bool focusable() const {
            return type == ItemType::Toggle || type == ItemType::Slider
                || type == ItemType::Selector || type == ItemType::Action
                || type == ItemType::ColorPicker;
        }
    };

    struct Tab {
        std::string              name;
        std::vector<SettingItem> items;
        std::function<void(Tab&, SettingsScreen&)> onUpdate;
    };

protected:
    void onRender(nxui::Renderer& ren) override;
    void onContentRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;

private:
    friend class settings::tabs::SystemTab;
    friend class settings::tabs::AudioTab;
    friend class settings::tabs::DisplayTab;
    friend class settings::tabs::InternetTab;
    friend class settings::tabs::ControllersTab;
    friend class settings::tabs::BluetoothTab;
    friend class settings::tabs::SleepTab;
    friend class settings::tabs::MusicTab;
    friend class settings::tabs::ThemeTab;
    friend class settings::tabs::AboutTab;

    void buildTabs();

    void setupActions();
    void onPressB();
    void onPressA();
    void onNavUp();
    void onNavDown();
    void onNavLeft();
    void onNavRight();
    void scrollToFocused();

    std::shared_ptr<nxui::Box> m_tabBar;
    std::shared_ptr<nxui::Box> m_tabContent;
    std::vector<std::vector<std::shared_ptr<nxui::Box>>> m_cachedTabContentWidgets;

    void rebuildTabBar();
    void rebuildContentItems();
    std::shared_ptr<nxui::Box> makeItemWidget(SettingItem& item);

    bool  m_active    = false;
    bool  m_animating = false;
    bool  m_showing   = false;
    float m_animT     = 0.f;

    static constexpr float kAnimDuration = 0.22f;

    enum class FocusArea { Tabs, Content };
    FocusArea m_focusArea   = FocusArea::Tabs;
    int       m_tabIndex    = 0;
    int       m_contentIdx  = 0;
    float     m_scrollY     = 0.f;
    float     m_scrollTarget = 0.f;
    bool      m_backdropCacheValid = false;
    float     m_cachedPreBlurRadius = -1.f;
    int       m_cachedBlurIterations = -1;

    int rawIndexFromFocusable(int focIdx) const;
    int focusableCount() const;
    void clampContentIdx();
    float visibilityProgress() const;
    void syncPanelState(float eased);
    void invalidateBackdropCache();

    static constexpr float kPanelMargin   = 32.f;
    static constexpr float kTabWidth      = 248.f;
    static constexpr float kRowHeight     = 60.f;
    static constexpr float kSectionHeight = 44.f;
    static constexpr float kTabRowHeight  = 52.f;
    static constexpr float kPanelRadius   = 26.f;
    static constexpr float kInnerPad      = 30.f;

    nxui::Rect panelRect() const;
    nxui::Rect panelRect(float scale) const;
    nxui::Rect tabsRect() const;
    nxui::Rect tabsRect(const nxui::Rect& panel) const;
    nxui::Rect contentRect() const;
    nxui::Rect contentRect(const nxui::Rect& panel) const;
    float      contentTotalHeight() const;

    void drawBackground(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawTabs(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawContent(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawDropdown(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawTrackChangedToast(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void syncDebugWireframeRects(const nxui::Rect& panel);

    std::vector<Tab> m_tabs;
    nxui::Font*       m_font      = nullptr;
    nxui::Font*       m_smallFont = nullptr;
    const nxui::Theme* m_theme    = nullptr;

    SelectionCursor m_focusCursor;
    nxui::AnimatedFloat m_tabGlowY;
    nxui::AnimatedFloat m_contentGlowY;
    nxui::AnimatedFloat m_tabReveal;
    nxui::AnimatedFloat m_dropdownAnim;
    nxui::AnimatedFloat m_trackToastAnim;
    float m_trackToastHold = 0.f;
    bool  m_trackToastFading = false;
    std::string m_toastText;
    float m_uiTime = 0.f;

    int   m_tabSwitchDir    = 0;
    nxui::AnimatedFloat m_contentSlideAnim;
    nxui::AnimatedFloat m_tabAccentW;

    bool m_dropdownOpen = false;
    int  m_dropdownRawIdx = -1;
    int  m_dropdownHover = 0;

    bool  m_colorPickerOpen = false;
    int   m_colorPickerRawIdx = -1;
    int   m_colorPickerSlider = 0;
    nxui::AnimatedFloat m_colorPickerAnim;
    void drawColorPicker(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);

    ThemeToggleCb m_themeToggleCb;
    ColorChangeCb m_themeColorChangeCb;
    PresetChangeCb m_themePresetChangeCb;
    VoidCb  m_themeResetCb;
    VoidCb  m_themeSaveCb;
    VoidCb  m_themeManageCb;
    BoolCb  m_themeModeCb;
    BoolCb  m_musicEnabledCb;
    FloatCb m_musicVolumeCb;
    FloatCb m_sfxVolumeCb;
    VoidCb  m_nextTrackCb;
    VoidCb  m_navSfxCb;
    VoidCb  m_activateSfxCb;
    VoidCb  m_closeSfxCb;
    VoidCb  m_closedCb;
    VoidCb  m_netConnectCb;
    DialogRequestCb m_dialogRequestCb;
    BoolCb  m_wireframeCb;
    IntCb   m_gridColumnsCb;
    IntCb   m_gridRowsCb;
    BoolCb  m_toggleSfxCb;
    BoolCb  m_sliderSfxCb;
    StringCb m_uiLanguageCb;
    StringCb m_soundPresetCb;

    bool  m_musicEnabled = true;
    float m_musicVolume = 0.4f;
    float m_sfxVolume = 0.7f;
    bool  m_wireframeEnabled = false;
    int   m_gridColumns = 5;
    int   m_gridRows = 3;
    std::string m_uiLanguageOverride = "auto";
    std::string m_soundPreset = "wiiu";
    std::vector<std::string> m_soundPresets;
    std::string m_themePresetName = "Default Dark";
    std::vector<std::string> m_themePresetNames;
    ThemeColorSet m_themeColors;
    bool m_themeModeDark = true;
    int m_i18nListenerId = -1;
    bool m_deferredRefresh = false;

    // Touch tracking
    enum class TouchTarget { None, Tab, Content, Dropdown, ColorPicker };
    TouchTarget m_touchTarget = TouchTarget::None;
    int   m_touchHitIndex = -1;
    bool  m_touchOnSelected = false;
    bool  m_touchDirectControl = false;
    float m_touchStartX = 0.f;
    float m_touchStartY = 0.f;
    float m_touchStartScroll = 0.f;
    bool  m_touchScrolling = false;
    bool  m_touchDraggingSlider = false;
    bool  m_touchDraggingColor = false;
    bool  m_ignoreInitialTouchRelease = false;
};
