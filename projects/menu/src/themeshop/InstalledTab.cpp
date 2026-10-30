#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>

ThemeShopScreen::Tab themeshop::tabs::InstalledTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.themes", "Themes");
    // Themes hosts Installed | Shop; Shop needs the same catalog warm as the
    // former Community tab so covers/names are ready when you flip the shelf.
    t.onUpdate = [&screen](Tab&, TabbedOverlayScreen&) {
        if (screen.m_threadPool
            && screen.m_communityRevision == 0
            && screen.m_communityTransferState.phase() == ThemeTransferState::Phase::Idle) {
            screen.refreshCommunityCatalog();
        }
        if (screen.pollCommunityCatalog())
            screen.rebuildCurrentTab();
    };
    return t;
}
