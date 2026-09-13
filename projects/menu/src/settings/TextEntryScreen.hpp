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

    void onAccept(AcceptCallback cb) { m_acceptCb = std::move(cb); }
    void onCancel(VoidCallback cb) { m_cancelCb = std::move(cb); }
    void onKeySfx(VoidCallback cb) { m_keySfxCb = std::move(cb); }
    void onNavigateSfx(VoidCallback cb) { m_navigateSfxCb = std::move(cb); }
    void onCloseSfx(VoidCallback cb) { m_closeSfxCb = std::move(cb); }
    void onAccessibilityAnnouncement(StringCallback cb) { m_accessibilityCb = std::move(cb); }

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
    // One key. `lower` and `upper` are the UTF-8 sequences the key produces; an
    // action key leaves both empty and carries an Action instead.
    enum class Action { None, Shift, Page, Space, Backspace, Accept };
    struct Key {
        std::string lower;
        std::string upper;
        Action action = Action::None;
        int span = 1;
    };

    void buildLayout();
    void setupActions();
    const std::vector<std::vector<Key>>& rows() const;
    nxui::Rect keyRect(int row, int column) const;
    void moveSelection(int dx, int dy);
    void togglePage();
    void pressSelected();
    void pressKey(const Key& key);
    void appendText(const std::string& utf8);
    void backspace();
    void announceSelection();
    std::string displayText() const;
    std::string keyLabel(const Key& key) const;
    int textLength() const;

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;

    bool m_active = false;
    bool m_animatingOut = false;
    // The blurred copy of whatever is behind the panel is taken once per open;
    // nothing under a modal keyboard moves while it owns input.
    bool m_backdropReady = false;
    bool m_accepted = false;
    nxui::AnimatedFloat m_alpha;

    Request m_request;
    std::string m_text;
    bool m_shift = false;
    int m_page = 0;          // 0 letters, 1 symbols and accented vowels
    int m_row = 0;
    int m_column = 0;
    float m_caretTime = 0.f;
    int m_touchRow = -1;
    int m_touchColumn = -1;
    bool m_waitingForTouchRelease = false;

    std::vector<std::vector<Key>> m_letters;
    std::vector<std::vector<Key>> m_symbols;

    AcceptCallback m_acceptCb;
    VoidCallback m_cancelCb;
    VoidCallback m_keySfxCb;
    VoidCallback m_navigateSfxCb;
    VoidCallback m_closeSfxCb;
    StringCallback m_accessibilityCb;

    static constexpr int kColumns = 10;
    static constexpr float kPanelX = 96.f;
    static constexpr float kPanelY = 84.f;
    static constexpr float kPanelW = 1088.f;
    static constexpr float kPanelH = 552.f;
};
