#include "FolderOptionsScreen.hpp"
#include "widgets/FolderPalette.hpp"
#include "widgets/FolderStyleDraw.hpp"
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
    m_folder.tileSizeIndex = std::clamp(m_folder.tileSizeIndex, 0, 4);
    m_folder.styleIndex = std::clamp(m_folder.styleIndex, 0,
                                     switchu::folders::kFolderStyleCount - 1);
    m_folder.defaultViewIndex = std::clamp(m_folder.defaultViewIndex, 0, 2);
    m_tabs.clear();
    m_cachedTabContentWidgets.clear();
    warmup();
}

void FolderOptionsScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    m_tabs.clear();

    Tab appearance;
    appearance.name = i18n.tr("folder.appearance", "Appearance");

    SettingItem defaultView;
    defaultView.label = i18n.tr("folder.default_view", "Default folder view");
    defaultView.description = i18n.tr(
        "folder.default_view_desc",
        "Layout used when any folder is opened.");
    defaultView.type = ItemType::Selector;
    defaultView.options = {
        i18n.tr("folder.default_view_auto", "Auto"),
        i18n.tr("folder.default_view_grid", "Grid"),
        i18n.tr("folder.default_view_line", "Single-row"),
    };
    defaultView.intVal = m_folder.defaultViewIndex;
    defaultView.onChange = [this](SettingItem& self) {
        m_folder.defaultViewIndex = std::clamp(self.intVal, 0, 2);
        if (m_defaultViewCb) m_defaultViewCb(m_folder.defaultViewIndex);
    };
    appearance.items.push_back(std::move(defaultView));

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
        "folder.size_desc", "Changes the size and number of icons.");
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
        "Change look and feel (of all folders)");
    style.type = ItemType::Selector;
    style.options = {
        i18n.tr("folder.style_classic", "Classic"),
        i18n.tr("folder.style_simple", "Simple"),
        i18n.tr("folder.style_minimal", "Minimal"),
        i18n.tr("folder.style_tab", "Tab"),
        i18n.tr("folder.style_ring", "Ring"),
        i18n.tr("folder.style_manila", "Manila"),
        i18n.tr("folder.style_label", "Label"),
    };
    style.intVal = m_folder.styleIndex;
    style.onChange = [this](SettingItem& self) {
        m_folder.styleIndex = std::clamp(self.intVal, 0,
                                         switchu::folders::kFolderStyleCount - 1);
        if (m_styleCb) m_styleCb(m_folder.styleIndex);
    };
    appearance.items.push_back(std::move(style));

    SettingItem tileSize;
    tileSize.label = i18n.tr("folder.tile_size", "Tile size");
    tileSize.description = i18n.tr(
        "folder.tile_size_desc",
        "Change folder tile size on HOME.");
    tileSize.type = ItemType::Selector;
    tileSize.options = {"1x1", "2x1", "4x2", "2x2", "2x3"};
    tileSize.intVal = m_folder.tileSizeIndex;
    tileSize.onChange = [this](SettingItem& self) {
        m_folder.tileSizeIndex = std::clamp(self.intVal, 0, 4);
        if (m_tileSizeCb) m_tileSizeCb(m_folder.tileSizeIndex);
    };
    appearance.items.push_back(std::move(tileSize));

    SettingItem cover;
    cover.label = i18n.tr("folder.show_cover", "Show cover");
    cover.description = i18n.tr(
        "folder.show_cover_desc",
        "Show first apps icon on folders.");
    cover.type = ItemType::Toggle;
    cover.boolVal = m_folder.showCover;
    cover.anim01 = cover.boolVal ? 1.f : 0.f;
    cover.onChange = [this](SettingItem& self) {
        m_folder.showCover = self.boolVal;
        if (m_coverCb) m_coverCb(m_folder.showCover);
    };
    appearance.items.push_back(std::move(cover));
    m_tabs.push_back(std::move(appearance));

    Tab artwork;
    artwork.name = i18n.tr("folder.steamgriddb", "SteamGridDB");

    // Invoke member callbacks at click time — setFolder/buildTabs often runs
    // before showFolderContextMenu rebinds onCustom*Select, and capturing the
    // VoidCb by value left rows dead or wired to the previous folder (search leak).
    enum class ArtAction { Hero, Logo, Poster, PosterWide, Icon, SquareCover };
    auto addArtRow = [this, &artwork, &i18n](const char* labelKey,
                                             const char* label,
                                             const char* descKey,
                                             const char* desc,
                                             bool hasArt,
                                             ArtAction action) {
        SettingItem row;
        row.label = i18n.tr(labelKey, label);
        row.buttonLabel = i18n.tr("button.select", "Select");
        if (hasArt) {
            row.secondaryButtonLabel = i18n.tr("button.clear_x", "×");
            row.onSecondary = [this, action](SettingItem&) {
                switch (action) {
                    case ArtAction::Hero:
                        if (m_customHeroClearCb) m_customHeroClearCb();
                        break;
                    case ArtAction::Logo:
                        if (m_customLogoClearCb) m_customLogoClearCb();
                        break;
                    case ArtAction::Poster:
                        if (m_customPosterClearCb) m_customPosterClearCb();
                        break;
                    case ArtAction::PosterWide:
                        if (m_customPosterWideClearCb) m_customPosterWideClearCb();
                        break;
                    case ArtAction::Icon:
                        if (m_customIconClearCb) m_customIconClearCb();
                        break;
                    case ArtAction::SquareCover:
                        if (m_customSquareCoverClearCb) m_customSquareCoverClearCb();
                        break;
                }
            };
        }
        row.description = i18n.tr(descKey, desc);
        row.type = ItemType::Action;
        row.onChange = [this, action](SettingItem&) {
            switch (action) {
                case ArtAction::Hero:
                    if (m_customHeroSelectCb) m_customHeroSelectCb();
                    break;
                case ArtAction::Logo:
                    if (m_customLogoSelectCb) m_customLogoSelectCb();
                    break;
                case ArtAction::Poster:
                    if (m_customPosterSelectCb) m_customPosterSelectCb();
                    break;
                case ArtAction::PosterWide:
                    if (m_customPosterWideSelectCb) m_customPosterWideSelectCb();
                    break;
                case ArtAction::Icon:
                    if (m_customIconSelectCb) m_customIconSelectCb();
                    break;
                case ArtAction::SquareCover:
                    if (m_customSquareCoverSelectCb) m_customSquareCoverSelectCb();
                    break;
            }
        };
        artwork.items.push_back(std::move(row));
    };

    addArtRow("game.steamgriddb.hero", "Hero",
              "game.steamgriddb.hero_desc", "Choose a hero image.",
              m_folder.hasCustomHero, ArtAction::Hero);

    SettingItem customizeHero;
    customizeHero.type = ItemType::Action;
    customizeHero.label = i18n.tr("game.steamgriddb.customize_hero", "Customize Hero");
    customizeHero.buttonLabel = i18n.tr("button.open", "Open");
    if (m_folder.hasHeroPresentation) {
        customizeHero.secondaryButtonLabel = i18n.tr("button.clear_x", "×");
        customizeHero.onSecondary = [this](SettingItem&) {
            if (m_clearHeroPresentationCb) m_clearHeroPresentationCb();
        };
    }
    customizeHero.description = i18n.tr(
        "folder.customize_hero_desc",
        "Crop and pan this folder's hero.");
    customizeHero.indentLevel = 1;
    customizeHero.enabled = m_folder.hasCustomHero;
    customizeHero.onChange = [this](SettingItem&) {
        if (m_customizeHeroCb) m_customizeHeroCb();
    };
    artwork.items.push_back(std::move(customizeHero));

    SettingItem overrideHeroes;
    overrideHeroes.label = i18n.tr("folder.override_title_heroes", "Override Title Heroes");
    overrideHeroes.description = i18n.tr(
        "folder.override_title_heroes_desc",
        "Use folders hero for all folder items.");
    overrideHeroes.type = ItemType::Toggle;
    overrideHeroes.boolVal = m_folder.overrideTitleHeroes;
    overrideHeroes.anim01 = overrideHeroes.boolVal ? 1.f : 0.f;
    overrideHeroes.enabled = m_folder.hasCustomHero;
    overrideHeroes.onChange = [this](SettingItem& self) {
        m_folder.overrideTitleHeroes = self.boolVal;
        if (m_overrideTitleHeroesCb) m_overrideTitleHeroesCb(m_folder.overrideTitleHeroes);
    };
    artwork.items.push_back(std::move(overrideHeroes));

    addArtRow("game.steamgriddb.logo", "Logo",
              "game.steamgriddb.logo_desc", "Choose a logo.",
              m_folder.hasCustomLogo, ArtAction::Logo);
    addArtRow("game.steamgriddb.poster", "Poster",
              "game.steamgriddb.poster_desc", "Tall art in single-row and 2x3.",
              m_folder.hasCustomPoster, ArtAction::Poster);
    if (m_folder.tileSizeIndex == 1 || m_folder.tileSizeIndex == 2) {
        addArtRow("game.steamgriddb.poster_wide", "Poster wide",
                  "game.steamgriddb.poster_wide_desc",
                  "Landscape grid art for this 2x1 / 4x2 tile.",
                  m_folder.hasCustomPosterWide, ArtAction::PosterWide);
    }
    addArtRow("game.steamgriddb.icon", "Replacement icon",
              "game.steamgriddb.icon_desc", "Applies to 1x1 and 2x2.",
              m_folder.hasCustomIcon, ArtAction::Icon);
    // Always list Square cover (same as titles) so it can be set before/after
    // resizing; art still applies on 1x1 / 2x2 faces.
    addArtRow("game.steamgriddb.square_cover", "Square cover",
              "game.steamgriddb.square_cover_desc",
              "Applies to 1x1 and 2x2.",
              m_folder.hasCustomSquareCover, ArtAction::SquareCover);
    m_tabs.push_back(std::move(artwork));

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
    erase.description = i18n.tr("folder.delete_desc", "Games inside will return to HOME.");
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
    switchu::folders::FolderStyleDrawArgs preview;
    preview.renderer = &ren;
    preview.bounds = shell;
    preview.radius = 18.f;
    preview.scale = 1.f;
    preview.opacity = opacity;
    preview.styleIndex = m_folder.styleIndex;
    preview.accent = switchu::folders::colorForIndex(m_folder.colorIndex);
    preview.themeMode = m_theme->mode;
    preview.drawName = false;
    preview.showCover = m_folder.showCover;
    preview.cover = (m_folder.cover && m_folder.cover->valid()) ? m_folder.cover : nullptr;
    preview.schematicPlaceholder = preview.cover == nullptr;
    switchu::folders::drawFolderStyle(preview);

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
