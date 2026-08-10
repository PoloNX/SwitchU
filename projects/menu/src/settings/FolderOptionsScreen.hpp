#pragma once

#include "TabbedOverlayScreen.hpp"
#include <cstdint>

class FolderOptionsScreen final : public TabbedOverlayScreen {
public:
    struct FolderInfo {
        std::uint32_t id = 0;
        std::string name;
        int itemCount = 0;
        int colorIndex = 0;
        int sizeIndex = 1;
    };

    FolderOptionsScreen();

    void setFolder(const FolderInfo& info);
    void onOpen(VoidCb cb) { m_openCb = std::move(cb); }
    void onRename(VoidCb cb) { m_renameCb = std::move(cb); }
    void onDelete(VoidCb cb) { m_deleteCb = std::move(cb); }
    void onColorChange(IntCb cb) { m_colorCb = std::move(cb); }
    void onSizeChange(IntCb cb) { m_sizeCb = std::move(cb); }

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
};
