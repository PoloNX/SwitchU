#pragma once

#include "TabbedOverlayScreen.hpp"
#include <nxui/core/Texture.hpp>
#include <cstdint>

class FolderOptionsScreen final : public TabbedOverlayScreen {
public:
    struct FolderInfo {
        std::uint32_t id = 0;
        std::string name;
        int itemCount = 0;
        int colorIndex = 0;
        // Internal capacity: Small / Medium / Large.
        int sizeIndex = 1;
        // HOME grid span: 0=1x1, 1=2x1, 2=4x2, 3=2x2, 4=2x3 (same as games).
        int tileSizeIndex = 0;
        int styleIndex = 0;
        // 0 = Auto, 1 = Grid, 2 = Single-row. Global for all folders.
        int defaultViewIndex = 0;
        bool showCover = false;
        bool hasCustomIcon = false;
        bool hasCustomSquareCover = false;
        bool hasCustomPoster = false;
        bool hasCustomPosterWide = false;
        bool hasCustomLogo = false;
        bool hasCustomHero = false;
        bool hasHeroPresentation = false;
        bool overrideTitleHeroes = false;
        nxui::Texture* cover = nullptr;
    };

    FolderOptionsScreen();

    void setFolder(const FolderInfo& info);
    void onOpen(VoidCb cb) { m_openCb = std::move(cb); }
    void onRename(VoidCb cb) { m_renameCb = std::move(cb); }
    void onDelete(VoidCb cb) { m_deleteCb = std::move(cb); }
    void onColorChange(IntCb cb) { m_colorCb = std::move(cb); }
    void onSizeChange(IntCb cb) { m_sizeCb = std::move(cb); }
    void onTileSizeChange(IntCb cb) { m_tileSizeCb = std::move(cb); }
    void onStyleChange(IntCb cb) { m_styleCb = std::move(cb); }
    void onDefaultViewChange(IntCb cb) { m_defaultViewCb = std::move(cb); }
    void onCoverChange(BoolCb cb) { m_coverCb = std::move(cb); }
    void onCustomIconSelect(VoidCb cb) { m_customIconSelectCb = std::move(cb); }
    void onCustomIconClear(VoidCb cb) { m_customIconClearCb = std::move(cb); }
    void onCustomSquareCoverSelect(VoidCb cb) { m_customSquareCoverSelectCb = std::move(cb); }
    void onCustomSquareCoverClear(VoidCb cb) { m_customSquareCoverClearCb = std::move(cb); }
    void onCustomPosterSelect(VoidCb cb) { m_customPosterSelectCb = std::move(cb); }
    void onCustomPosterClear(VoidCb cb) { m_customPosterClearCb = std::move(cb); }
    void onCustomPosterWideSelect(VoidCb cb) { m_customPosterWideSelectCb = std::move(cb); }
    void onCustomPosterWideClear(VoidCb cb) { m_customPosterWideClearCb = std::move(cb); }
    void onCustomLogoSelect(VoidCb cb) { m_customLogoSelectCb = std::move(cb); }
    void onCustomLogoClear(VoidCb cb) { m_customLogoClearCb = std::move(cb); }
    void onCustomHeroSelect(VoidCb cb) { m_customHeroSelectCb = std::move(cb); }
    void onCustomHeroClear(VoidCb cb) { m_customHeroClearCb = std::move(cb); }
    void onCustomizeHero(VoidCb cb) { m_customizeHeroCb = std::move(cb); }
    void onClearHeroPresentation(VoidCb cb) { m_clearHeroPresentationCb = std::move(cb); }
    void onOverrideTitleHeroesChange(BoolCb cb) { m_overrideTitleHeroesCb = std::move(cb); }

protected:
    void buildTabs() override;
    // Matches top/bottom panel padding (~24–30): preview sits at y+24 (92 tall),
    // content starts at kInnerPad + headerH. 110 leaves ~24px under the preview.
    float overlayHeaderHeight() const override { return 110.f; }
    float overlayTabWidth() const override { return 250.f; }
    void drawOverlayHeader(nxui::Renderer& ren, const nxui::Rect& panel,
                           float opacity) override;

private:
    FolderInfo m_folder;
    VoidCb m_openCb;
    VoidCb m_renameCb;
    VoidCb m_deleteCb;
    IntCb m_colorCb;
    IntCb m_sizeCb;
    IntCb m_tileSizeCb;
    IntCb m_styleCb;
    IntCb m_defaultViewCb;
    BoolCb m_coverCb;
    VoidCb m_customIconSelectCb;
    VoidCb m_customIconClearCb;
    VoidCb m_customSquareCoverSelectCb;
    VoidCb m_customSquareCoverClearCb;
    VoidCb m_customPosterSelectCb;
    VoidCb m_customPosterClearCb;
    VoidCb m_customPosterWideSelectCb;
    VoidCb m_customPosterWideClearCb;
    VoidCb m_customLogoSelectCb;
    VoidCb m_customLogoClearCb;
    VoidCb m_customHeroSelectCb;
    VoidCb m_customHeroClearCb;
    VoidCb m_customizeHeroCb;
    VoidCb m_clearHeroPresentationCb;
    BoolCb m_overrideTitleHeroesCb;
};
