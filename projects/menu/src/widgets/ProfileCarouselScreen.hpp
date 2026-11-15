#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/Theme.hpp>
#include <switch.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// One console account as shown on HOME.
struct HomeProfile {
    AccountUid uid{};
    std::string nickname;
    std::shared_ptr<nxui::Texture> avatar;
};

// Full-screen Wii U style user picker: a scrolling row of profile tiles
// ("ask each time" first, then a circular "add user" tile) and a details panel
// for the highlighted profile. A opens the profile page, X makes it the
// default profile games launch with.
class ProfileCarouselScreen final : public nxui::GlassWidget {
public:
    enum class CardKind { Nobody, Profile, AddUser };

    using VoidCb = std::function<void()>;
    using StringCb = std::function<void(const std::string&)>;
    using UidCb = std::function<void(AccountUid)>;
    using LockCb = std::function<void(std::optional<AccountUid>)>;

    ProfileCarouselScreen();

    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setProfiles(std::vector<HomeProfile> profiles, bool addUserAvailable);
    void setLockedUid(std::optional<AccountUid> uid) { m_lockedUid = uid; }

    void onOpenProfile(UidCb cb) { m_openProfileCb = std::move(cb); }
    void onLockProfile(LockCb cb) { m_lockProfileCb = std::move(cb); }
    void onAddUser(VoidCb cb) { m_addUserCb = std::move(cb); }
    void onClosed(VoidCb cb) { m_closedCb = std::move(cb); }
    void onNavigateSfx(VoidCb cb) { m_navSfxCb = std::move(cb); }
    void onLockSfx(VoidCb cb) { m_lockSfxCb = std::move(cb); }
    void onCloseSfx(VoidCb cb) { m_closeSfxCb = std::move(cb); }
    void onAccessibilityAnnouncement(StringCb cb) { m_accessibilityCb = std::move(cb); }

    void show();
    void hide();
    bool isActive() const { return m_active || m_animatingOut; }
    bool isOpen() const { return m_active && !m_animatingOut; }
    // Logical canvas (1280×720 landscape, 720×1280 portrait).
    void setCanvasSize(float width, float height);
    CardKind selectedKind() const { return tileKind(m_selected); }
    bool selectedIsLocked() const {
        return tileKind(m_selected) == CardKind::Profile && isLockedTile(m_selected);
    }
    void handleTouch(nxui::Input& input);

    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    // Tiles are "nobody" (index 0), every profile, then the "add user" entry.
    int tileCount() const {
        return 1 + (int)m_profiles.size() + (m_addUserAvailable ? 1 : 0);
    }
    CardKind tileKind(int index) const {
        if (index <= 0) return CardKind::Nobody;
        if (index <= (int)m_profiles.size()) return CardKind::Profile;
        return CardKind::AddUser;
    }
    bool onAddUserTile() const { return tileKind(m_selected) == CardKind::AddUser; }
    const HomeProfile* tileProfile(int index) const;
    bool isLockedTile(int index) const;
    int lockedTileIndex() const;
    std::string tileName(int index) const;

    bool isPortraitCanvas() const { return m_canvasH > m_canvasW + 0.5f; }
    float screenW() const { return m_canvasW; }
    float screenH() const { return m_canvasH; }
    // Portrait: vertically center the tile row + details stack as one block.
    float rowY() const {
        if (!isPortraitCanvas())
            return 250.f;
        constexpr float kTileH = 136.f;
        constexpr float kGap = 100.f;
        constexpr float kDetailStack = 160.f + 100.f; // avatar + name/badge
        const float blockH = kTileH + kGap + kDetailStack;
        const float centered = (m_canvasH - blockH) * 0.5f;
        return centered > 80.f ? centered : 80.f;
    }
    float detailY() const {
        return isPortraitCanvas() ? (rowY() + 136.f + 100.f) : 430.f;
    }
    float rowContentWidth() const;
    float rowScrollTarget(int index) const;
    nxui::Rect tileRect(int index) const;

    void selectTile(int index, bool announce = true);
    void moveHorizontal(int direction);
    void activateSelected();
    void lockSelected();
    void announceSelection();
    void setupActions();

    void drawTile(nxui::Renderer& ren, int index, float alpha);
    void drawAvatar(nxui::Renderer& ren, int index, const nxui::Rect& r, float radius, float alpha);
    void drawCheckBadge(nxui::Renderer& ren, const nxui::Rect& tile, float alpha);
    void drawBubble(nxui::Renderer& ren, float alpha);
    void drawDetails(nxui::Renderer& ren, float alpha);

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    std::vector<HomeProfile> m_profiles;
    bool m_addUserAvailable = false;
    std::optional<AccountUid> m_lockedUid;

    bool m_active = false;
    bool m_animatingOut = false;
    int m_selected = 0;
    nxui::AnimatedFloat m_alpha;
    nxui::AnimatedFloat m_rowScroll;
    nxui::AnimatedFloat m_focusPop;
    int m_touchHitTile = -1;

    UidCb m_openProfileCb;
    LockCb m_lockProfileCb;
    VoidCb m_addUserCb;
    VoidCb m_closedCb;
    VoidCb m_navSfxCb;
    VoidCb m_lockSfxCb;
    VoidCb m_closeSfxCb;
    StringCb m_accessibilityCb;
    float m_canvasW = 1280.f;
    float m_canvasH = 720.f;
};
