#pragma once

#include "TabbedOverlayScreen.hpp"
#include <nxui/core/Texture.hpp>
#include <cstdint>

class GameOptionsScreen final : public TabbedOverlayScreen {
public:
    enum class ArtworkKind { Hero, Logo, Icon };
    struct GameInfo {
        std::uint64_t titleId = 0;
        std::string name;
        std::string version;
        std::string publisher;
        nxui::Texture* icon = nullptr;
        bool gameCard = false;
        bool suspended = false;
        bool canMove = true;
        bool canResize = true;
        int sizeIndex = 0;
    };

    GameOptionsScreen();

    void setGame(const GameInfo& info);
    void setGameIcon(nxui::Texture* icon) { m_game.icon = icon; }
    void onMove(VoidCb cb) { m_moveCb = std::move(cb); }
    void onResize(IntCb cb) { m_resizeCb = std::move(cb); }
    void onCloseSoftware(VoidCb cb) { m_closeSoftwareCb = std::move(cb); }
    void onDeleteSoftware(VoidCb cb) { m_deleteSoftwareCb = std::move(cb); }
    void onSelectArtwork(std::function<void(ArtworkKind)> cb) {
        m_selectArtworkCb = std::move(cb);
    }

protected:
    void buildTabs() override;
    float overlayHeaderHeight() const override { return 132.f; }
    float overlayTabWidth() const override { return 250.f; }
    void drawOverlayHeader(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) override;

private:
    GameInfo m_game;
    VoidCb m_moveCb;
    IntCb m_resizeCb;
    VoidCb m_closeSoftwareCb;
    VoidCb m_deleteSoftwareCb;
    std::function<void(ArtworkKind)> m_selectArtworkCb;
};
