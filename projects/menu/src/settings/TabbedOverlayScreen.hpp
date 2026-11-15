#pragma once
#include <nxui/core/Types.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/widgets/GlassWidget.hpp>
#include "widgets/SelectionCursor.hpp"
#include <cstdint>
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
class ThemeShopInstalledTab;
class ThemeShopCommunityTab;
class AboutTab;
}

class TabbedOverlayScreen : public nxui::GlassWidget {
public:
    friend class settings::tabs::SystemTab;

    enum class ScreenMode {
        Settings,
        ThemeShop,
        GameOptions,
        FolderOptions,
    };

    explicit TabbedOverlayScreen(ScreenMode mode = ScreenMode::Settings);
    virtual ~TabbedOverlayScreen();

    void setFont(nxui::Font* f)      { m_font = f; }
    void setSmallFont(nxui::Font* f)  { m_smallFont = f; }
    void setTheme(const nxui::Theme* t);
    void setInstantCursorMotion(bool instant) { m_focusCursor.setInstantMotion(instant); }
    // Logical canvas (1280×720 landscape, 720×1280 portrait). Settings /
    // Game/Folder options use a top tab rail + full-width pages when portrait.
    void setCanvasSize(float width, float height);

    // Compact glyphs for portrait Settings (icon-only top rail).
    enum class TabGlyph : std::uint8_t {
        None = 0,
        System,
        Accessibility,
        Storage,
        Audio,
        Display,
        Internet,
        SteamGridDb,
        Controllers,
        Bluetooth,
        Sleep,
        About,
        // Theme Shop portrait rail (peer icons, including inset shops).
        Themes,
        ThemeShop,
        ThemeIcons,
        IconShop,
        Sounds,
        Interface,
        LockScreen,
    };

    virtual void show();
    void hide();
    bool isActive() const { return m_active || m_animating; }
    bool isFullyVisible() const { return m_active && !m_animating; }

    void rebuildCurrentTab();
    void refreshCurrentTabWidgets(); // place: keeps the other tabs loaded and the in-flight loaders untouched.
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

    struct DateTimeEditorValue {
        int year = 2000;
        int month = 1;
        int day = 1;
        int hour = 0;
        int minute = 0;
    };
    using DateTimeCommitCb = std::function<bool(const DateTimeEditorValue&)>;
    using DateTimeEditorRequestCb =
        std::function<void(const DateTimeEditorValue&, DateTimeCommitCb)>;
    void onDateTimeEditorRequest(DateTimeEditorRequestCb cb) {
        m_dateTimeEditorRequestCb = std::move(cb);
    }
    void requestDateTimeEditor(const DateTimeEditorValue& initial,
                               DateTimeCommitCb onCommit);
    void requestToast(const std::string& msg, float holdSeconds = 2.5f);

    using TimezoneSelectCb = std::function<void(const std::string& zone)>;
    using TimezoneSearchCb = std::function<void()>;
    using TimezonePickerRequestCb = std::function<void(
        const std::vector<std::string>& zones,
        int selectedIndex,
        TimezoneSelectCb onSelect,
        TimezoneSearchCb onSearch)>;
    void onTimezonePickerRequest(TimezonePickerRequestCb cb) {
        m_timezonePickerRequestCb = std::move(cb);
    }
    void requestTimezonePicker(const std::vector<std::string>& zones,
                               int selectedIndex,
                               TimezoneSelectCb onSelect,
                               TimezoneSearchCb onSearch);

    using TextEntryAcceptCb = std::function<void(const std::string&)>;
    using TextEntryRequestCb = std::function<void(const std::string& title,
                                                  const std::string& guide,
                                                  const std::string& initial,
                                                  int maxLength,
                                                  TextEntryAcceptCb onAccept)>;
    void onTextEntryRequest(TextEntryRequestCb cb) { m_textEntryRequestCb = std::move(cb); }
    void requestTextEntry(const std::string& title, const std::string& guide,
                          const std::string& initial, int maxLength,
                          TextEntryAcceptCb onAccept);

    using BoolCb = std::function<void(bool)>;
    using FloatCb = std::function<void(float)>;
    using IntCb = std::function<void(int)>;
    using VoidCb = std::function<void()>;
    using StringCb = std::function<void(const std::string&)>;
    using AccessibilityStructuredCb = std::function<void(const std::string& context,
                                                         const std::string& position,
                                                         const std::string& summary,
                                                         bool forceRepeat,
                                                         bool forceContext)>;
    void onNavigateSfx(VoidCb cb)        { m_navSfxCb = std::move(cb); }
    void onActivateSfx(VoidCb cb)        { m_activateSfxCb = std::move(cb); }
    void onCloseSfx(VoidCb cb)           { m_closeSfxCb = std::move(cb); }
    void onToggleSfx(BoolCb cb)          { m_toggleSfxCb = std::move(cb); }
    void onSliderSfx(BoolCb cb)          { m_sliderSfxCb = std::move(cb); }
    void onClosed(VoidCb cb)             { m_closedCb = std::move(cb); }
    void onAccessibilityAnnouncement(StringCb cb) { m_accessibilityCb = std::move(cb); }
    void onAccessibilityStructuredAnnouncement(AccessibilityStructuredCb cb) {
        m_accessibilityStructuredCb = std::move(cb);
    }
    void setAccessibilitySpeechPreferences(bool speakHints, bool speakPosition) {
        m_accessibilitySpeakHints = speakHints;
        m_accessibilitySpeakPosition = speakPosition;
    }
    void setAccessibilityVoiceEnabled(bool enabled) {
        m_accessibilityVoiceEnabled = enabled;
    }
    void refreshTranslations();
    ScreenMode screenMode() const { return m_mode; }

    enum class ItemType { Info, Toggle, Slider, Progress, Selector, Action, Section };

    struct SettingItem {
        std::string label;
        std::string buttonLabel;
        // Optional second action drawn beside the primary button (e.g. Select + ×).
        std::string secondaryButtonLabel;
        ItemType    type = ItemType::Info;
        std::string description;

        bool                     boolVal   = false;
        bool                     suppressToggleSfx = false;
        bool                     enabled   = true;
        // When true, the row takes no height and is skipped for focus.
        bool                     hidden    = false;
        int                      indentLevel = 0;
        float                    floatVal  = 0.f;
        int                      intVal    = 0;
        int                      sliderSteps = 20;
        std::vector<std::string> options;
        std::string              infoText;
        float                    anim01 = 0.f;
        bool                     wrapLabel = false;
        // Optional large sample under the row (Theme Shop lock clock fonts).
        float                    extraHeight = 0.f;
        nxui::Font*              previewFont = nullptr;
        std::string              previewText;

        std::function<void(SettingItem&)> onChange;
        std::function<void(SettingItem&)> onSecondary;

        // 0 = primary (Select), 1 = secondary (×). Used for controller focus.
        int actionButtonFocus = 0;
        // Set each frame by the overlay when this row owns content focus.
        bool contentFocused = false;
        // Fade the rest of the panel so HOME (e.g. a hero) shows through
        // while this row is focused. Used by the hero blur slider.
        bool peekBackground = false;

        // Filled by ActionRowWidget for touch hit-testing.
        nxui::Rect primaryHit{};
        nxui::Rect secondaryHit{};

        const std::string& effectiveButtonLabel() const {
            return buttonLabel.empty() ? label : buttonLabel;
        }

        bool focusable() const {
            if (hidden || !enabled)
                return false;
            return type == ItemType::Toggle || type == ItemType::Slider
                || type == ItemType::Selector || type == ItemType::Action;
        }

        bool hasSecondaryAction() const {
            return enabled && onSecondary && !secondaryButtonLabel.empty();
        }
    };

    struct Tab {
        std::string              name;
        TabGlyph                 glyph = TabGlyph::None;
        std::vector<SettingItem> items;
        std::function<void(Tab&, TabbedOverlayScreen&)> onUpdate;
    };

protected:
    virtual void buildTabs() = 0;
    virtual void ensureTabLoaded(int tabIndex) { (void)tabIndex; }
    virtual bool usesCustomContentLayout() const { return false; }
    virtual void drawCustomContent(nxui::Renderer&, const nxui::Rect&, const nxui::Rect&, float) {}
    virtual void updateCustomContent(float) {}
    virtual bool handleCustomPressA() { return false; }
    virtual bool handleCustomPressB() { return false; }
    virtual bool handleCustomPressX() { return false; }
    virtual bool handleCustomNavUp() { return false; }
    virtual bool handleCustomNavDown() { return false; }
    virtual bool handleCustomNavLeft() { return false; }
    virtual bool handleCustomNavRight() { return false; }
    virtual bool handleCustomTouch(nxui::Input&, const nxui::Rect&, const nxui::Rect&, const nxui::Rect&) { return false; }
    virtual float overlayHeaderHeight() const { return 0.f; }
    virtual float overlayTabWidth() const { return kTabWidth; }
    virtual void drawOverlayHeader(nxui::Renderer&, const nxui::Rect&, float) {}

    // Optional nested row under a tab (e.g. Theme Shop "Shop" under Themes/Icons).
    virtual float tabBranchExtraHeight(int tabIndex) const { (void)tabIndex; return 0.f; }
    // Portrait top rail: extra strip under the parent row for inset shop children.
    virtual float topTabRailBranchStripHeight() const { return 0.f; }
    virtual void drawTabBranch(nxui::Renderer&, int tabIndex, const nxui::Rect& branchRect, float opacity) {
        (void)tabIndex; (void)branchRect; (void)opacity;
    }
    virtual bool tabBranchConsumesFocus() const { return false; }
    virtual bool tabBranchHitTest(float x, float y, int& outTabIndex) const {
        (void)x; (void)y; (void)outTabIndex;
        return false;
    }
    virtual bool tabFocusCursorRect(nxui::Rect& outRect) const { (void)outRect; return false; }
    virtual void onTabRailParentActivated(int tabIndex) { (void)tabIndex; }
    virtual bool onTabRailBranchActivated(int parentTabIndex) {
        (void)parentTabIndex;
        return false;
    }
    // Portrait icon rails may expose more slots than m_tabs (Theme Shop + Icon Shop).
    virtual int topRailSlotCount() const {
        return std::max(1, (int)m_tabs.size());
    }
    virtual TabGlyph topRailSlotGlyph(int slot) const {
        if (slot < 0 || slot >= (int)m_tabs.size())
            return TabGlyph::None;
        return m_tabs[(size_t)slot].glyph;
    }
    virtual std::string topRailSlotTitle(int slot) const {
        if (slot < 0 || slot >= (int)m_tabs.size())
            return {};
        return m_tabs[(size_t)slot].name;
    }
    virtual int topRailFocusIndex() const { return m_tabIndex; }
    virtual std::string selectedOverlayTabTitle() const {
        return topRailSlotTitle(topRailFocusIndex());
    }
    virtual void activateTopRailSlot(int slot);
    float tabRailContentHeight() const;

    void onRender(nxui::Renderer& ren) override;
    void onContentRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;

protected:
    void setupActions();
    void onPressB();
    void onPressA();
    void onPressX();
    void onNavUp();
    void onNavDown();
    void onNavLeft();
    void onNavRight();
    void scrollToFocused();
    void announceCurrentFocus();
    void announceCurrentValue();
    virtual void currentAccessibilityParts(std::string& context,
                                           std::string& position,
                                           std::string& summary,
                                           bool& forceRepeat) const;
    virtual std::string currentAccessibilitySummary() const;

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
    float     m_tabScrollY = 0.f;
    float     m_tabScrollTarget = 0.f;
    bool      m_backdropCacheValid = false;
    float     m_cachedPreBlurRadius = -1.f;
    int       m_cachedBlurIterations = -1;

    int rawIndexFromFocusable(int focIdx) const;
    int focusableCount() const;
    bool itemFocusable(const SettingItem& item) const;
    bool focusedItemPeeksBackground() const;
    float backgroundPeekAmount() const;
    bool tabIsTextOnly() const;
    void clampContentIdx();
    float visibilityProgress() const;
    void syncPanelState(float eased);
    void invalidateBackdropCache();

    static constexpr float kPanelMargin   = 32.f;
    static constexpr float kTabWidth      = 260.f;
    float itemHeight(const SettingItem& item, float contentWidth) const;
    static constexpr float kRowHeight     = 68.f;
    static constexpr float kSectionHeight = 48.f;
    static constexpr float kTabRowHeight  = 58.f;
    static constexpr float kTabRailInset  = 14.f;
    static constexpr float kPanelRadius   = 26.f;
    static constexpr float kInnerPad      = 30.f;

    // Staggered reveal of the content rows after a tab switch.
    static constexpr float kRowStaggerDelay = 0.035f;   // between two rows
    static constexpr float kRowRevealDur    = 0.24f;    // per row
    static constexpr float kContentStaggerDone = 99.f;

    nxui::Rect panelRect() const;
    nxui::Rect panelRect(float scale) const;
    nxui::Rect tabsRect() const;
    nxui::Rect tabsRect(const nxui::Rect& panel) const;
    nxui::Rect contentRect() const;
    nxui::Rect contentRect(const nxui::Rect& panel) const;
    float      contentTotalHeight() const;
    bool isPortraitCanvas() const {
        return m_canvasH > m_canvasW + 0.5f;
    }
    // Portrait Settings / Game / Folder / Theme Shop: horizontal tabs under the header.
    bool useTopTabRail() const {
        return isPortraitCanvas()
            && (m_mode == ScreenMode::Settings
                || m_mode == ScreenMode::ThemeShop
                || m_mode == ScreenMode::GameOptions
                || m_mode == ScreenMode::FolderOptions);
    }
    // Portrait Settings / Theme Shop: icon glyphs + title pill under the rail.
    bool useIconOnlyTopTabs() const {
        return useTopTabRail()
            && (m_mode == ScreenMode::Settings || m_mode == ScreenMode::ThemeShop);
    }
    // L/R cycle tabs in portrait and landscape. Theme Shop keeps ZL/ZR for pages.
    bool shoulderCyclesTopTabs() const {
        return m_mode == ScreenMode::Settings
            || m_mode == ScreenMode::ThemeShop
            || m_mode == ScreenMode::GameOptions
            || m_mode == ScreenMode::FolderOptions;
    }
    float selectedTabTitleHeight() const {
        // Glass title chip + padding above the settings page.
        return useIconOnlyTopTabs() ? 62.f : 0.f;
    }
    void layoutTabChildren(const nxui::Rect& tr);
    void applyCanvasChrome();
    float m_canvasW = 1280.f;
    float m_canvasH = 720.f;

    void drawBackground(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawTabs(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawContent(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawDropdown(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawTrackChangedToast(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void syncDebugWireframeRects(const nxui::Rect& panel);
    void openDropdown(int rawIdx);
    void closeDropdown(bool animated);

    struct DropdownGeom {
        float ctrlX = 0.f;
        float ctrlW = 0.f;
        float dy = 0.f;
        float listH = 0.f;
        int visible = 0;
        float optH = 46.f;
        bool valid = false;
    };
    DropdownGeom layoutDropdown(const nxui::Rect& cr) const;
    int dropdownVisibleCount(int optionCount) const;

    // Language refresh rebuilds tabs asynchronously; restore focus/scroll after load.
    void stashFocusForLanguageRestore();
    bool applyStashedFocusRestore();
    virtual bool canApplyStashedFocusRestore() const { return true; }

    std::vector<Tab> m_tabs;
    ScreenMode m_mode = ScreenMode::Settings;
    nxui::Font*       m_font      = nullptr;
    nxui::Font*       m_smallFont = nullptr;
    const nxui::Theme* m_theme    = nullptr;

    SelectionCursor m_focusCursor;
    nxui::AnimatedFloat m_tabReveal;
    nxui::AnimatedFloat m_dropdownAnim;
    nxui::AnimatedFloat m_trackToastAnim;
    nxui::AnimatedFloat m_backgroundPeek;
    float m_trackToastHold = 0.f;
    bool  m_trackToastFading = false;
    std::string m_toastText;
    float m_uiTime = 0.f;

    int   m_tabSwitchDir    = 0;
    nxui::AnimatedFloat m_contentSlideAnim;
    nxui::AnimatedFloat m_tabAccentW;
    // Seconds since the last tab switch, driving the staggered row reveal.
    // Starts finished so rows are never hidden outside a switch.
    float m_contentStaggerT = kContentStaggerDone;

    bool m_dropdownOpen = false;
    bool m_dropdownClosing = false;
    int  m_dropdownRawIdx = -1;
    int  m_dropdownHover = 0;
    float m_dropdownVisualStart = 0.f;
    float m_touchStartDropdownVisualStart = 0.f;
    int  m_dropdownVisible = 6;

    bool m_stashFocusRestore = false;
    int  m_stashTabIndex = 0;
    int  m_stashContentIdx = 0;
    float m_stashScrollY = 0.f;
    FocusArea m_stashFocusArea = FocusArea::Content;

    VoidCb  m_navSfxCb;
    VoidCb  m_activateSfxCb;
    VoidCb  m_closeSfxCb;
    VoidCb  m_closedCb;
    DialogRequestCb m_dialogRequestCb;
    DateTimeEditorRequestCb m_dateTimeEditorRequestCb;
    TimezonePickerRequestCb m_timezonePickerRequestCb;
    TextEntryRequestCb m_textEntryRequestCb;
    BoolCb  m_toggleSfxCb;
    BoolCb  m_sliderSfxCb;
    StringCb m_accessibilityCb;
    AccessibilityStructuredCb m_accessibilityStructuredCb;
    bool m_accessibilityVoiceEnabled = true;
    bool m_accessibilitySpeakHints = true;
    bool m_accessibilitySpeakPosition = true;
    int m_i18nListenerId = -1;
    bool m_deferredRefresh = false;

    // Touch tracking
    enum class TouchTarget { None, Tab, Content, Dropdown };
    TouchTarget m_touchTarget = TouchTarget::None;
    int   m_touchHitIndex = -1;
    bool  m_touchTabBranch = false;
    bool  m_touchOnSelected = false;
    bool  m_touchDirectControl = false;
    float m_touchStartX = 0.f;
    float m_touchStartY = 0.f;
    float m_touchStartScroll = 0.f;
    bool  m_touchScrolling = false;
    bool  m_touchDraggingSlider = false;
    bool  m_ignoreInitialTouchRelease = false;
};
