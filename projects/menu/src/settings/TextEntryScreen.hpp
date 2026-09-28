#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>

#include <functional>
#include <string>
#include <vector>

// On-screen keyboard.
//
// The production menu is a library applet, and swkbdShow() never returns from
// one: it froze the console when Create folder called it, proven by a
// "[keyboard] folder-name show" line in menu.log with no matching "returned".
// Every applet this menu offers is launched by the daemon over SMI instead, and
// that protocol has no keyboard command, so text entry is drawn here rather
// than delegated. This has no applet to wait on and works in both build shapes.
class TextEntryScreen final : public nxui::GlassWidget {
public:
    using AcceptCallback = std::function<void(const std::string&)>;
    using VoidCallback = std::function<void()>;
    using StringCallback = std::function<void(const std::string&)>;
    using LayoutCallback = std::function<void(bool fullLayout)>;

    struct Request {
        std::string title;
        std::string guide;
        std::string initial;
        int maxLength = 64;
        bool password = false;
    };

    TextEntryScreen();

    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme);

    void setFullLayout(bool full);
    bool fullLayout() const { return m_fullLayout; }
    void setGlassStyle(bool glass);
    bool glassStyle() const { return m_glassStyle; }

    void onAccept(AcceptCallback cb) { m_acceptCb = std::move(cb); }
    void onCancel(VoidCallback cb) { m_cancelCb = std::move(cb); }
    void onKeySfx(VoidCallback cb) { m_keySfxCb = std::move(cb); }
    void onNavigateSfx(VoidCallback cb) { m_navigateSfxCb = std::move(cb); }
    void onCloseSfx(VoidCallback cb) { m_closeSfxCb = std::move(cb); }
    void onAccessibilityAnnouncement(StringCallback cb) { m_accessibilityCb = std::move(cb); }
    void onLayoutModeChanged(LayoutCallback cb) { m_layoutCb = std::move(cb); }
    void onGlassStyleChanged(LayoutCallback cb) { m_styleCb = std::move(cb); }

    void show(const Request& request);
    void hide(bool accepted);
    bool isActive() const { return m_active || m_animatingOut; }
    const std::string& text() const { return m_text; }

    void handleTouch(nxui::Input& input);

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;
    void onContentRender(nxui::Renderer& ren) override;

private:
    enum class Action { None, Shift, Page, Space, Backspace, Accept, Cancel };
    struct Key {
        std::string lower;
        std::string upper;
        Action action = Action::None;
        int span = 1;
    };

    struct Metrics {
        float panelX = 96.f;
        float panelY = 84.f;
        float panelW = 1088.f;
        float panelH = 552.f;
        float inset = 16.f;
        float keyGap = 2.f;
        float rowHeight = 72.f;
        float fieldTop = 86.f;
        float fieldHeight = 58.f;
        float keyboardTop = 156.f;
        float keyRadius = 6.f;
        float boardPad = 6.f;
    };

    void buildLayout();
    void setupActions();
    void applyPanelRect();
    Metrics metrics() const;
    const std::vector<std::vector<Key>>& rows() const;
    nxui::Rect keyRect(int row, int column) const;
    nxui::Rect styleToggleRect() const;
    nxui::Rect sizeToggleRect() const;
    nxui::Rect cancelChipRect() const;
    nxui::Rect keyboardBoardRect() const;
    void moveSelection(int dx, int dy);
    void togglePage();
    void toggleSizeMode();
    void toggleGlassStyle();
    void pressSelected();
    void pressKey(const Key& key);
    void appendText(const std::string& utf8);
    void backspace();
    void beginBackspaceHold();
    void updateBackspaceHold(float dt, bool held);
    void announceSelection();
    std::string displayText() const;
    std::string keyLabel(const Key& key) const;
    int textLength() const;

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;

    bool m_active = false;
    bool m_animatingOut = false;
    bool m_backdropReady = false;
    bool m_accepted = false;
    bool m_fullLayout = true;
    bool m_glassStyle = true;
    nxui::AnimatedFloat m_alpha;

    Request m_request;
    std::string m_text;
    bool m_shift = false;
    bool m_shiftLock = false;
    int m_page = 0;          // 0 letters, 1 symbols and accented vowels
    int m_row = 0;
    int m_column = 0;
    float m_caretTime = 0.f;
    int m_touchRow = -1;
    int m_touchColumn = -1;
    bool m_waitingForTouchRelease = false;
    bool m_touchOnStyleToggle = false;
    bool m_touchOnSizeToggle = false;
    bool m_touchOnCancelChip = false;
    bool m_backspaceHeld = false;
    float m_backspaceHoldTime = 0.f;
    float m_backspaceRepeatLeft = 0.f;

    std::vector<std::vector<Key>> m_letters;
    std::vector<std::vector<Key>> m_symbols;

    AcceptCallback m_acceptCb;
    VoidCallback m_cancelCb;
    VoidCallback m_keySfxCb;
    VoidCallback m_navigateSfxCb;
    VoidCallback m_closeSfxCb;
    StringCallback m_accessibilityCb;
    LayoutCallback m_layoutCb;
    LayoutCallback m_styleCb;

    static constexpr int kColumns = 10;
};
