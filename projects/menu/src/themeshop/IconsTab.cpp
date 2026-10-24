#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>

ThemeShopScreen::Tab themeshop::tabs::IconsTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.icons", "Icons");
    // The tab lists installed packs plus the online catalog, so it drives the
    // same catalog fetch as Community and lets the app rebuild the list when
    // new icon entries arrive.
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
