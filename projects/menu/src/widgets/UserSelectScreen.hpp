#pragma once
#include <nxui/widgets/GlassPanel.hpp>
#include "SelectionCursor.hpp"
#include <nxui/core/Texture.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Types.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Input.hpp>
#include <switch.h>
#include <vector>
#include <string>
#include <functional>


class AudioManager;

class UserSelectScreen : public nxui::Widget {
public:
    using SelectCallback = std::function<void(AccountUid uid)>;
    using CancelCallback = std::function<void()>;

    UserSelectScreen();

    bool loadUsers(nxui::GpuDevice& gpu, nxui::Renderer& ren);

    void setFont(nxui::Font* f)       { m_font = f; }
    void setSmallFont(nxui::Font* sf) { m_smallFont = sf; }
    void setAudio(AudioManager* a) { m_audio = a; }

    void show(SelectCallback onSelect, CancelCallback onCancel = {});
    void hide();

    bool isActive() const { return m_active; }

    void handleTouch(nxui::Input& input);

    nxui::GlassPanel& panel() { return m_panel; }
    nxui::GlassPanel& titlePanel() { return m_titlePanel; }
    SelectionCursor& cursor() { return m_cursor; }

    void setTextColor(const nxui::Color& c)          { m_textPrimary = c; }
    void setSecondaryTextColor(const nxui::Color& c) { m_textSecondary = c; }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    struct UserEntry {
        AccountUid    uid;
        std::string   nickname;
        nxui::Texture       icon;
    };

    std::vector<UserEntry> m_users;
    int  m_selected = 0;
    bool m_active   = false;

    nxui::Font* m_font      = nullptr;
    nxui::Font* m_smallFont = nullptr;
    AudioManager* m_audio = nullptr;

    SelectCallback m_onSelect;
    CancelCallback m_onCancel;

    nxui::GlassPanel m_panel;
    nxui::GlassPanel m_titlePanel;
    SelectionCursor m_cursor;

    nxui::AnimatedFloat m_overlayAlpha;
    nxui::AnimatedFloat m_panelScale;
    bool m_animatingOut = false;

    nxui::Color m_textPrimary    {1.f, 1.f, 1.f, 1.f};
    nxui::Color m_textSecondary  {0.7f, 0.7f, 0.8f, 0.8f};

    std::vector<nxui::Rect> m_avatarRects;

    int m_touchHitAvatar = -1;
    bool m_touchOnSelected = false;
    bool m_ignoreInitialTouchRelease = false;
};

