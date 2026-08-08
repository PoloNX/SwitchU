#pragma once

#include "TabbedOverlayScreen.hpp"
#include <nxui/core/Texture.hpp>
#include <cstdint>

class GameOptionsScreen final : public TabbedOverlayScreen {
public:
    struct GameInfo {
        std::uint64_t titleId = 0;
        std::string name;
        std::string version;
        std::string publisher;
        nxui::Texture* icon = nullptr;
        bool gameCard = false;
        bool suspended = false;
    };

    GameOptionsScreen();

    void setGame(const GameInfo& info);
    void onMove(VoidCb cb) { m_moveCb = std::move(cb); }
    void onCloseSoftware(VoidCb cb) { m_closeSoftwareCb = std::move(cb); }
    void onDeleteSoftware(VoidCb cb) { m_deleteSoftwareCb = std::move(cb); }

protected:
    void buildTabs() override;
    float overlayHeaderHeight() const override { return 132.f; }
    float overlayTabWidth() const override { return 250.f; }
    void drawOverlayHeader(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) override;

private:
    GameInfo m_game;
    VoidCb m_moveCb;
    VoidCb m_closeSoftwareCb;
    VoidCb m_deleteSoftwareCb;
};
