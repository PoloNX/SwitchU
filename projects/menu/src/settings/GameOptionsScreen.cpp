#include "GameOptionsScreen.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cstdio>

namespace {
std::string ellipsize(nxui::Font* font, std::string text, float maxWidth, float scale) {
    if (!font || font->measure(text).x * scale <= maxWidth)
        return text;
    while (!text.empty()) {
        text.pop_back();
        if (font->measure(text + "...").x * scale <= maxWidth)
            return text + "...";
    }
    return "...";
}
}

GameOptionsScreen::GameOptionsScreen()
    : TabbedOverlayScreen(ScreenMode::GameOptions) {
    constexpr float width = 920.f;
    setRect({(1280.f - width) * 0.5f, 32.f, width, 656.f});
}

void GameOptionsScreen::setGame(const GameInfo& info) {
    m_game = info;
    m_tabs.clear();
    m_cachedTabContentWidgets.clear();
    warmup();
}

void GameOptionsScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    // buildTabs() is also called when translations are refreshed. Rebuild
    // from a clean snapshot instead of appending to the previous language's
    // tabs, otherwise every language change duplicates the rail.
    m_tabs.clear();

    Tab data;
    data.name = i18n.tr("game.data_management", "Data Management");

    if (m_game.canMove) {
        SettingItem move;
        move.label = i18n.tr("game.move", "Move");
        move.description = i18n.tr("game.move_desc", "Move this software to another position on the HOME menu.");
        move.type = ItemType::Action;
        move.onChange = [this](SettingItem&) { if (m_moveCb) m_moveCb(); };
        data.items.push_back(std::move(move));
    }

    if (m_game.suspended) {
        SettingItem close;
        close.label = i18n.tr("game.close_title", "Close software");
        close.description = i18n.tr("game.close_desc", "Close the running software. Unsaved progress may be lost.");
        close.type = ItemType::Action;
        close.onChange = [this](SettingItem&) { if (m_closeSoftwareCb) m_closeSoftwareCb(); };
        data.items.push_back(std::move(close));
    }

    SettingItem erase;
    erase.label = i18n.tr("game.delete_software", "Delete Software");
    erase.description = m_game.gameCard
        ? i18n.tr("game.delete_gamecard_unavailable", "Game card software cannot be deleted from the console.")
        : i18n.tr("game.delete_software_desc", "Deletes the software from the console. Save data is not deleted.");
    erase.type = m_game.gameCard ? ItemType::Info : ItemType::Action;
    erase.onChange = [this](SettingItem&) { if (m_deleteSoftwareCb) m_deleteSoftwareCb(); };
    data.items.push_back(std::move(erase));
    m_tabs.push_back(std::move(data));

    Tab info;
    info.name = i18n.tr("game.information", "Information");
    SettingItem version;
    version.label = i18n.tr("game.version", "Version");
    version.type = ItemType::Info;
    version.infoText = m_game.version;
    info.items.push_back(std::move(version));

    char id[17]{};
    std::snprintf(id, sizeof(id), "%016lX", static_cast<unsigned long>(m_game.titleId));
    SettingItem titleId;
    titleId.label = i18n.tr("game.title_id", "Title ID");
    titleId.type = ItemType::Info;
    titleId.infoText = id;
    info.items.push_back(std::move(titleId));
    m_tabs.push_back(std::move(info));

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());
    rebuildTabBar();
    rebuildContentItems();
}

void GameOptionsScreen::drawOverlayHeader(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || !m_font || !m_smallFont)
        return;

    const float pad = 30.f;
    const float iconSize = 92.f;
    nxui::Rect iconRect{panel.x + pad, panel.y + pad, iconSize, iconSize};
    if (m_game.icon && m_game.icon->valid()) {
        ren.drawTextureRounded(m_game.icon, iconRect, 18.f, nxui::Color::white().withAlpha(opacity));
    } else {
        ren.drawRoundedRect(iconRect, m_theme->panelBorder.withAlpha(0.38f * opacity), 18.f);
    }

    float textX = iconRect.right() + 24.f;
    float textW = std::max(0.f, panel.right() - pad - textX);
    ren.drawText(ellipsize(m_font, m_game.name, textW, 1.f),
                 {textX, panel.y + 34.f}, m_font, m_theme->textPrimary.withAlpha(opacity), 1.f);

    std::string version = nxui::I18n::instance().tr("game.version", "Version") + " " + m_game.version;
    ren.drawText(ellipsize(m_smallFont, version, textW, 0.78f),
                 {textX, panel.y + 78.f}, m_smallFont, m_theme->textSecondary.withAlpha(opacity), 0.78f);
    ren.drawText(ellipsize(m_smallFont, m_game.publisher, textW, 0.78f),
                 {textX, panel.y + 103.f}, m_smallFont, m_theme->textSecondary.withAlpha(0.92f * opacity), 0.78f);

    float dividerY = panel.y + pad + overlayHeaderHeight() - 14.f;
    ren.drawRoundedRect({panel.x + pad, dividerY, panel.width - pad * 2.f, 1.5f},
                        m_theme->panelBorder.withAlpha(0.25f * opacity), 0.75f);
}
