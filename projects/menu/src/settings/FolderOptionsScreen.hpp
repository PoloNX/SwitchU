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
        int sizeIndex = 1;
        int styleIndex = 0;
        // 0 = Auto, 1 = Grid, 2 = Single-row. Global for all folders.
        int defaultViewIndex = 0;
        bool showCover = false;
        bool hasCustomIcon = false;
        bool hasCustomPoster = false;
        nxui::Texture* cover = nullptr;
    };

    FolderOptionsScreen();

    void setFolder(const FolderInfo& info);
    void onOpen(VoidCb cb) { m_openCb = std::move(cb); }
    void onRename(VoidCb cb) { m_renameCb = std::move(cb); }
    void onDelete(VoidCb cb) { m_deleteCb = std::move(cb); }
    void onColorChange(IntCb cb) { m_colorCb = std::move(cb); }
    void onSizeChange(IntCb cb) { m_sizeCb = std::move(cb); }
    void onStyleChange(IntCb cb) { m_styleCb = std::move(cb); }
    void onDefaultViewChange(IntCb cb) { m_defaultViewCb = std::move(cb); }
    void onCoverChange(BoolCb cb) { m_coverCb = std::move(cb); }
    void onCustomIconSelect(VoidCb cb) { m_customIconSelectCb = std::move(cb); }
    void onCustomIconClear(VoidCb cb) { m_customIconClearCb = std::move(cb); }
    void onCustomPosterSelect(VoidCb cb) { m_customPosterSelectCb = std::move(cb); }
    void onCustomPosterClear(VoidCb cb) { m_customPosterClearCb = std::move(cb); }

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
    IntCb m_styleCb;
    IntCb m_defaultViewCb;
    BoolCb m_coverCb;
    VoidCb m_customIconSelectCb;
    VoidCb m_customIconClearCb;
    VoidCb m_customPosterSelectCb;
    VoidCb m_customPosterClearCb;
};
