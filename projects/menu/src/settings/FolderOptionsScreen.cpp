#include "FolderOptionsScreen.hpp"
#include "widgets/FolderPalette.hpp"
#include "core/FolderStore.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>

namespace {

std::string ellipsize(nxui::Font* font, std::string text, float maxWidth) {
    if (!font || font->measure(text).x <= maxWidth)
        return text;
    while (!text.empty()) {
        text.pop_back();
        if (font->measure(text + "...").x <= maxWidth)
            return text + "...";
    }
    return "...";
}

} // namespace

FolderOptionsScreen::FolderOptionsScreen()
    : TabbedOverlayScreen(ScreenMode::FolderOptions) {
    constexpr float width = 920.f;
    setRect({(1280.f - width) * 0.5f, 32.f, width, 656.f});
}

void FolderOptionsScreen::setFolder(const FolderInfo& info) {
    m_folder = info;
    m_folder.colorIndex = std::clamp(m_folder.colorIndex, 0, 7);
    m_folder.sizeIndex = std::clamp(m_folder.sizeIndex, 0, 2);
    m_folder.styleIndex = std::clamp(m_folder.styleIndex, 0,
                                     switchu::folders::kFolderStyleCount - 1);
    m_tabs.clear();
    m_cachedTabContentWidgets.clear();
    warmup();
}

void FolderOptionsScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    m_tabs.clear();

    Tab appearance;
    appearance.name = i18n.tr("folder.appearance", "Appearance");

    SettingItem color;
    color.label = i18n.tr("folder.color", "Folder color");
    color.description = i18n.tr("folder.color_desc", "Choose the color used by this folder.");
    color.type = ItemType::Selector;
    color.options = {
        i18n.tr("folder.color_blue", "Blue"),
        i18n.tr("folder.color_green", "Green"),
        i18n.tr("folder.color_yellow", "Yellow"),
        i18n.tr("folder.color_orange", "Orange"),
        i18n.tr("folder.color_red", "Red"),
        i18n.tr("folder.color_pink", "Pink"),
        i18n.tr("folder.color_purple", "Purple"),
        i18n.tr("folder.color_gray", "Gray"),
    };
    color.intVal = m_folder.colorIndex;
    color.onChange = [this](SettingItem& self) {
        m_folder.colorIndex = std::clamp(self.intVal, 0, 7);
        if (m_colorCb) m_colorCb(m_folder.colorIndex);
    };
    appearance.items.push_back(std::move(color));

    SettingItem size;
    size.label = i18n.tr("folder.size", "Folder size");
    size.description = i18n.tr(
        "folder.size_desc", "Changes the size and number of icons shown inside this folder.");
    size.type = ItemType::Selector;
    size.options = {
        i18n.tr("folder.size_small", "Small"),
        i18n.tr("folder.size_medium", "Medium"),
        i18n.tr("folder.size_large", "Large"),
    };
    size.intVal = m_folder.sizeIndex;
    size.onChange = [this](SettingItem& self) {
        m_folder.sizeIndex = std::clamp(self.intVal, 0, 2);
        if (m_sizeCb) m_sizeCb(m_folder.sizeIndex);
    };
    appearance.items.push_back(std::move(size));

    SettingItem style;
    style.label = i18n.tr("folder.style", "Folder style");
    style.description = i18n.tr(
        "folder.style_desc",
        "Applies Classic or Glass to every folder, including new ones.");
    style.type = ItemType::Selector;
    style.options = {
        i18n.tr("folder.style_classic", "Classic"),
        i18n.tr("folder.style_glass", "Glass"),
    };
    style.intVal = m_folder.styleIndex;
    style.onChange = [this](SettingItem& self) {
        m_folder.styleIndex = std::clamp(self.intVal, 0,
                                         switchu::folders::kFolderStyleCount - 1);
        if (m_styleCb) m_styleCb(m_folder.styleIndex);
    };
    appearance.items.push_back(std::move(style));
    m_tabs.push_back(std::move(appearance));

    Tab management;
    management.name = i18n.tr("folder.management", "Management");

    SettingItem open;
    open.label = i18n.tr("folder.open_title", "Open folder");
    open.buttonLabel = i18n.tr("button.open", "Open");
    open.type = ItemType::Action;
    open.onChange = [this](SettingItem&) { if (m_openCb) m_openCb(); };
    management.items.push_back(std::move(open));

    SettingItem rename;
    rename.label = i18n.tr("folder.rename_title", "Folder name");
    rename.buttonLabel = i18n.tr("button.rename", "Rename");
    rename.type = ItemType::Action;
    rename.onChange = [this](SettingItem&) { if (m_renameCb) m_renameCb(); };
    management.items.push_back(std::move(rename));

    SettingItem erase;
    erase.label = i18n.tr("folder.delete", "Delete folder");
    erase.buttonLabel = i18n.tr("button.delete", "Delete");
    erase.description = i18n.tr("folder.delete_desc", "Games inside will return to the HOME menu.");
    erase.type = ItemType::Action;
    erase.onChange = [this](SettingItem&) { if (m_deleteCb) m_deleteCb(); };
    management.items.push_back(std::move(erase));
    m_tabs.push_back(std::move(management));

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());
    rebuildTabBar();
    rebuildContentItems();
}

void FolderOptionsScreen::drawOverlayHeader(nxui::Renderer& ren,
                                             const nxui::Rect& panel,
                                             float opacity) {
    if (!m_theme || !m_font || !m_smallFont)
        return;

    const nxui::Rect shell{panel.x + 30.f, panel.y + 24.f, 92.f, 92.f};
    const nxui::Color accent = switchu::folders::colorForIndex(m_folder.colorIndex);
    const bool classic = m_folder.styleIndex == switchu::folders::kFolderStyleClassic;

    if (classic) {
        ren.drawRoundedRect({shell.x, shell.y + 5.f, shell.width, shell.height},
                            nxui::Color::black().withAlpha(0.22f * opacity), 18.f);
        ren.drawRoundedRect(shell, nxui::Color(0.92f, 0.95f, 0.94f, 0.96f * opacity), 18.f);
        ren.drawRoundedRectOutline(shell.shrunk(1.f),
                                   nxui::Color::white().withAlpha(0.90f * opacity),
                                   17.f, 2.f);

        constexpr float cell = 19.f;
        constexpr float gap = 4.f;
        const float gridSize = cell * 3.f + gap * 2.f;
        const float gx = shell.x + (shell.width - gridSize) * 0.5f;
        const float gy = shell.y + (shell.height - gridSize) * 0.5f;
        for (int i = 0; i < 9; ++i) {
            nxui::Rect tile{gx + (i % 3) * (cell + gap),
                            gy + (i / 3) * (cell + gap), cell, cell};
            ren.drawRoundedRect(tile, accent.withAlpha(0.96f * opacity), 4.f);
            ren.drawRoundedRect({tile.x + 2.f, tile.y + 2.f, tile.width - 4.f, 3.f},
                                nxui::Color::white().withAlpha(0.20f * opacity), 1.5f);
        }
    } else {
        ren.drawRoundedRect(shell,
                            nxui::Color(0.04f, 0.07f, 0.11f, 0.50f * opacity), 18.f);
        ren.drawRoundedRect(shell, accent.withAlpha(0.18f * opacity), 18.f);
        ren.drawRoundedRectOutline(shell.shrunk(1.f),
                                   nxui::Color::white().withAlpha(0.40f * opacity),
                                   17.f, 1.5f);
        ren.drawRoundedRect({shell.x + 8.f, shell.y + 8.f, shell.width - 16.f, 10.f},
                            accent.withAlpha(0.82f * opacity), 4.f);
        ren.drawRoundedRect({shell.x + 14.f, shell.y + 42.f, shell.width - 28.f, 22.f},
                            nxui::Color(0.02f, 0.04f, 0.08f, 0.55f * opacity), 8.f);
    }

    const float textX = shell.right() + 24.f;
    const float textW = std::max(0.f, panel.right() - 30.f - textX);
    ren.drawText(ellipsize(m_font, m_folder.name, textW),
                 {textX, panel.y + 38.f}, m_font,
                 m_theme->textPrimary.withAlpha(opacity), 1.f);
    const std::string count = std::to_string(m_folder.itemCount) + " " +
        nxui::I18n::instance().tr("folder.items", "items");
    ren.drawText(count, {textX, panel.y + 82.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(opacity), 0.82f);
}
