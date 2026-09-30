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
        bool showCover = false;
        bool hasCustomIcon = false;
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
    void onCoverChange(BoolCb cb) { m_coverCb = std::move(cb); }
    void onCustomIconSelect(VoidCb cb) { m_customIconSelectCb = std::move(cb); }
    void onCustomIconClear(VoidCb cb) { m_customIconClearCb = std::move(cb); }

protected:
    void buildTabs() override;
    float overlayHeaderHeight() const override { return 132.f; }
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
    BoolCb m_coverCb;
    VoidCb m_customIconSelectCb;
    VoidCb m_customIconClearCb;
};
