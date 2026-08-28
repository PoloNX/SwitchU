#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>

#include <functional>
#include <string>
#include <vector>

class ContextMenu final : public nxui::GlassWidget {
public:
    struct Item {
        std::string label;
        std::function<void()> onPress;
        bool enabled = true;
    };

    using VoidCallback = std::function<void()>;

    ContextMenu();

    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void onNavigate(VoidCallback callback) { m_onNavigate = std::move(callback); }
    void onActivate(VoidCallback callback) { m_onActivate = std::move(callback); }
    void onClose(VoidCallback callback) { m_onClose = std::move(callback); }

    void show(const nxui::Rect& anchor, std::string title,
              std::vector<Item> items, int selected = 0,
              VoidCallback onCancel = {});
    void hide(bool notify = true);
    bool isActive() const { return m_active; }
    int selectedIndex() const { return m_selected; }

    void handleTouch(nxui::Input& input);
    void update(float dt) override;
    void render(nxui::Renderer& renderer) override;

private:
    nxui::Rect itemRect(int visibleIndex) const;
    void moveSelection(int direction);
    void activateSelected();
    void cancel();
    void keepSelectedVisible();
    int firstEnabled(int preferred) const;

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    std::string m_title;
    std::vector<Item> m_items;
    int m_selected = 0;
    int m_scroll = 0;
    int m_visibleItems = 0;
    bool m_active = false;
    int m_touchItem = -1;
    bool m_touchWasSelected = false;
    VoidCallback m_onCancel;
    VoidCallback m_onNavigate;
    VoidCallback m_onActivate;
    VoidCallback m_onClose;
    nxui::AnimatedFloat m_reveal{0.f};

    static constexpr float kPanelWidth = 312.f;
    static constexpr float kPanelPadding = 14.f;
    static constexpr float kTitleHeight = 34.f;
    static constexpr float kItemHeight = 48.f;
    static constexpr float kItemGap = 6.f;
    static constexpr int kMaximumVisibleItems = 7;
};
