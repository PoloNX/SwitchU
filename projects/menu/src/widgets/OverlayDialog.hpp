#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/widgets/Label.hpp>
#include <nxui/widgets/Box.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/Theme.hpp>
#include "ActionButton.hpp"
#include "SelectionCursor.hpp"
#include <switch.h>
#include <vector>
#include <string>
#include <functional>

class OverlayDialog : public nxui::GlassWidget {
public:
    struct ButtonDef {
        std::string label;
        std::function<void()> onPress;
        bool closeOnPress = true;
    };

    using CancelCallback = std::function<void()>;
    using VoidCb = std::function<void()>;
    using UserSelectCallback = std::function<void(AccountUid uid)>;
    struct DateTimeValue {
        int year = 2000;
        int month = 1;
        int day = 1;
        int hour = 0;
        int minute = 0;
    };
    using DateTimeSaveCallback = std::function<bool(const DateTimeValue&)>;

    OverlayDialog();

    void setFont(nxui::Font* f)         { m_font = f; }
    void setSmallFont(nxui::Font* f)    { m_smallFont = f; }
    void setTheme(const nxui::Theme* t) { m_theme = t; }
    /// Logical canvas (1280×720 landscape, 720×1280 portrait). Centers the panel.
    void setCanvasSize(float width, float height) {
        m_canvasW = width > 1.f ? width : 1280.f;
        m_canvasH = height > 1.f ? height : 720.f;
    }
    void setAccessibilitySpeechPreferences(bool speakHints, bool speakPosition) {
        m_accessibilitySpeakHints = speakHints;
        m_accessibilitySpeakPosition = speakPosition;
    }

    void show(const std::string& title,
              const std::string& message,
              std::vector<ButtonDef> buttons,
              int initialSelected = 0,
              CancelCallback onCancel = {});
    bool loadUsers(nxui::GpuDevice& gpu, nxui::Renderer& ren);
    void showUserSelect(UserSelectCallback onSelect, CancelCallback onCancel = {});
    void showDateTimeEditor(const DateTimeValue& initial,
                            DateTimeSaveCallback onSave,
                            CancelCallback onCancel = {});
    using TimezoneSelectCallback = std::function<void(const std::string& zone)>;
    using TimezoneSearchCallback = std::function<void()>;
    void showTimezonePicker(const std::vector<std::string>& zones,
                            int selectedIndex,
                            TimezoneSelectCallback onSelect,
                            TimezoneSearchCallback onSearch,
                            CancelCallback onCancel = {});

    void hide();

    bool isActive() const { return m_active || m_animatingOut; }

    void handleTouch(nxui::Input& input);

    void onNavigateSfx(VoidCb cb)  { m_navSfxCb = std::move(cb); }
    void onActivateSfx(VoidCb cb)  { m_activateSfxCb = std::move(cb); }
    void onCloseSfx(VoidCb cb)     { m_closeSfxCb = std::move(cb); }
    using StringCb = std::function<void(const std::string&)>;
    void onAccessibilityAnnouncement(StringCb cb) { m_accessibilityCb = std::move(cb); }
    using AccessibilityStructuredCb = std::function<void(const std::string& context,
                                                         const std::string& position,
                                                         const std::string& summary,
                                                         bool forceRepeat,
                                                         bool forceContext)>;
    void onAccessibilityStructuredAnnouncement(AccessibilityStructuredCb cb) {
        m_accessibilityStructuredCb = std::move(cb);
    }

    SelectionCursor& cursor() { return m_cursor; }

    static void renderGlassPanel(nxui::Renderer& ren,
                                 const nxui::Theme* theme,
                                 const nxui::Rect& panel,
                                 float radius,
                                 const nxui::Color& base,
                                 const nxui::Color& border,
                                 const nxui::Color& highlight,
                                 float alpha);

    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    void buildWidgetTree();
    void buildUserSelect();
    void buildDateTimeEditor();
    void buildTimezonePicker();
    void animateButtonFocus(float duration, nxui::EasingFunc easing);
    void setupActions();
    void setupUserActions();
    void setupDateTimeActions();
    void setupTimezoneActions();
    void activateSelected();
    void activateSelectedUser();
    void saveDateTime();
    void moveDateTimeField(int direction);
    void adjustDateTimeField(int direction);
    void moveTimezoneHover(int direction);
    void activateTimezoneSelection();
    void cancel();
    void syncCursor();
    void syncUserCursor();
    void syncDateTimeCursor();
    void syncTimezoneCursor();
    void announceCurrentSelection(bool forceRepeat = false, bool forceContext = false);
    void currentAccessibilityParts(std::string& context,
                                   std::string& position,
                                   std::string& summary,
                                   bool& forceRepeat) const;
    std::string currentAccessibilitySummary() const;
    void syncChildOpacities();
    void syncUserOpacities();
    nxui::Rect userAvatarRect(int index) const;
    void renderUserContent(nxui::Renderer& ren, float alpha);
    void renderDateTimeContent(nxui::Renderer& ren, float alpha);
    void renderTimezoneContent(nxui::Renderer& ren, float alpha);
    nxui::Rect dateTimeFieldRect(int index) const;
    nxui::Rect timezoneSearchRect() const;
    nxui::Rect timezoneListRect() const;
    nxui::Rect timezoneRowRect(int visualRow) const;
    void ensureTimezoneHoverVisible();

    nxui::Rect panelRect() const;

    enum class DialogMode {
        Buttons,
        UserSelect,
        DateTime,
        Timezone,
    };

    struct UserEntry {
        AccountUid uid;
        std::string nickname;
        nxui::Texture icon;
    };

    nxui::Font*        m_font      = nullptr;
    nxui::Font*        m_smallFont = nullptr;
    const nxui::Theme* m_theme     = nullptr;

    std::string              m_title;
    std::string              m_message;
    std::vector<ButtonDef>   m_buttons;
    std::vector<UserEntry>   m_users;
    int  m_selected     = 0;
    bool m_active       = false;
    bool m_animatingOut = false;
    DialogMode m_mode = DialogMode::Buttons;

    std::shared_ptr<nxui::Label>                     m_titleLabel;
    std::shared_ptr<nxui::Label>                     m_messageLabel;
    std::shared_ptr<nxui::Box>                       m_buttonRow;
    std::vector<std::shared_ptr<ActionButton>>       m_btnWidgets;
    SelectionCursor m_cursor;
    std::vector<nxui::Rect> m_userAvatarRects;

    nxui::AnimatedFloat m_overlayAlpha;
    nxui::AnimatedFloat m_panelScale;
    nxui::AnimatedFloat m_contentReveal;
    std::vector<nxui::AnimatedFloat> m_buttonFocus;

    float m_panelH = 0.f;
    bool  m_backdropCacheValid = false;
    float m_cachedPreBlurRadius = -1.f;
    int   m_cachedBlurIterations = -1;

    CancelCallback m_onCancel;
    UserSelectCallback m_onUserSelect;
    DateTimeSaveCallback m_onDateTimeSave;
    DateTimeValue m_dateTime{};
    int m_dateTimeField = 0;
    TimezoneSelectCallback m_onTimezoneSelect;
    TimezoneSearchCallback m_onTimezoneSearch;
    std::vector<std::string> m_timezoneOptions;
    int m_timezoneHover = 0;
    int m_timezoneScroll = 0;
    bool m_timezoneSearchFocused = false;
    static constexpr int kTimezoneVisibleRows = 7;
    VoidCb         m_navSfxCb;
    VoidCb         m_activateSfxCb;
    VoidCb         m_closeSfxCb;
    StringCb       m_accessibilityCb;
    AccessibilityStructuredCb m_accessibilityStructuredCb;
    bool m_accessibilitySpeakHints = true;
    bool m_accessibilitySpeakPosition = true;
    int  m_pendingInitialAccessibilityFrames = 0;

    int  m_touchHitButton  = -1;
    int  m_touchHitUser    = -1;
    int  m_touchHitTimezone = -1;
    bool m_touchOnTimezoneSearch = false;
    bool m_touchOnSelected = false;
    bool m_ignoreInitialTouchRelease = false;

    float m_panelW = 560.f;
    float m_canvasW = 1280.f;
    float m_canvasH = 720.f;

    static constexpr float kPanelW       = 560.f;
    static constexpr float kPanelPadX    = 40.f;
    static constexpr float kPanelPadY    = 34.f;
    static constexpr float kPanelRadius  = 26.f;
    static constexpr float kButtonH      = 50.f;
    static constexpr float kButtonRadius = 16.f;
    static constexpr float kButtonGap    = 14.f;
    static constexpr float kTitleMsgGap  = 14.f;
    static constexpr float kMsgBtnGap    = 24.f;
    static constexpr float kUserAvatarSize = 96.f;
    static constexpr float kUserAvatarGap = 32.f;
    static constexpr float kUserTitleGap = 30.f;
    static constexpr float kTimezoneSearchW = 108.f;
    static constexpr float kTimezoneRowH = 44.f;
};
