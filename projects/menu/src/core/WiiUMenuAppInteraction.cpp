#include "WiiUMenuApp.hpp"
#include "widgets/GlossyIcon.hpp"
#include "widgets/FolderPalette.hpp"
#include "DebugLog.hpp"
#ifdef SWITCHU_MENU
#include <switchu/control_cache.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <unordered_set>
#include <nxui/core/I18n.hpp>

bool WiiUMenuApp::isEditableIcon(nxui::Widget* w) const {
    if (!w || w->tag() != "glossy_icon")
        return false;
    auto* icon = static_cast<GlossyIcon*>(w);
    const int index = findTitleIndex(icon->titleId());
    if (index < 0)
        return false;
    return m_openFolderId != 0
        ? m_model.at(index).isApplication()
        : (m_model.at(index).isApplication() || m_model.at(index).isFolder() ||
           m_model.at(index).isWidget());
}

std::string WiiUMenuApp::accessibilityContextFor(nxui::Widget* w) const {
    auto& i18n = nxui::I18n::instance();
    if (!w)
        return {};
    if (m_dialog && m_dialog->isActive() && w == m_dialog.get())
        return i18n.tr("accessibility.context.dialog", "Dialog");
    if (m_contextMenu && m_contextMenu->isActive() && w == m_contextMenu.get())
        return i18n.tr("accessibility.context.menu", "Context menu");
    if (m_userSelect && m_userSelect->isActive() && w == m_userSelect.get())
        return i18n.tr("accessibility.context.profile_selection", "Profile selection");
    if (m_settings && m_settings->isActive() && w == m_settings.get())
        return i18n.tr("accessibility.context.settings", "Settings");
    if (m_themeShop && m_themeShop->isActive() && w == m_themeShop.get())
        return i18n.tr("accessibility.context.themes", "Themes");
    if (w->tag() == "glossy_icon" && m_grid) {
        if (m_appLayoutMode == AppLayoutMode::DynamicLine)
            return i18n.tr("accessibility.context.main_menu", "Main menu")
                 + ", " + i18n.tr("accessibility.context.dynamic_line", "Center line");
        return i18n.tr("accessibility.context.main_menu", "Main menu")
             + ", " + i18n.tr("accessibility.context.page", "page") + " "
             + std::to_string(m_grid->currentPage() + 1)
             + " " + i18n.tr("accessibility.context.of", "of") + " "
             + std::to_string(m_grid->totalPages());
    }
    for (const auto& btn : m_sidebar.leftButtons())
        if (btn.get() == w) return i18n.tr("accessibility.context.left_sidebar", "Left sidebar");
    for (const auto& btn : m_sidebar.rightButtons())
        if (btn.get() == w) return i18n.tr("accessibility.context.right_sidebar", "Right sidebar");
    for (const auto& avatar : m_userAvatarButtons)
        if (avatar.get() == w) return i18n.tr("accessibility.context.user_profiles", "User profiles");
    return {};
}

std::string WiiUMenuApp::accessibilityActionsFor(nxui::Widget* w) const {
    auto& i18n = nxui::I18n::instance();
    if (!w)
        return {};
    if (m_editMode && w->tag() == "glossy_icon")
        return m_openFolderId != 0
            ? i18n.tr("accessibility.actions.edit_mode_folder", "Directional pad to choose the position. A to place. B to leave the folder while keeping the software selected.")
            : i18n.tr("accessibility.actions.edit_mode", "Directional pad to choose the new position. A to place or enter a folder. B to cancel.");
    if (w->tag() == "glossy_icon") {
        auto* icon = static_cast<GlossyIcon*>(w);
        if (icon->titleId() == 0)
            return i18n.tr("accessibility.actions.empty_slot", "Directional pad to navigate. Plus for options. Minus to change view.");
        const int index = findTitleIndex(icon->titleId());
        if (index >= 0 && m_model.at(index).isFolder())
            return i18n.tr("folder.open_hint", "A to open. Plus for folder options. Y to move. Minus to change view.");
        if (index >= 0 && m_model.at(index).isWidget() &&
            m_model.at(index).widgetType == switchu::widgets::WidgetType::RecentlyPlayed &&
            m_widgetStore.recentActivity().titleId != 0)
            return i18n.tr("accessibility.hints.recently_played_widget",
                           "A to launch the recently played game. Plus for widget options. Y to move.");
        if (index >= 0 && m_model.at(index).isWidget())
            return i18n.tr("accessibility.hints.widget",
                           "Plus for widget options. Y to move. Minus to change view.");
        return icon->isNotLaunchable()
            ? i18n.tr("accessibility.actions.game_blocked", "A to show the reason. Plus for options. Y to move. Minus to change view.")
            : i18n.tr("accessibility.actions.game_launchable", "A to launch. Plus for options. Y to move. Minus to change view.");
    }
    if (m_settings && w == m_settings.get())
        return i18n.tr("accessibility.actions.settings", "Up and down to choose a category. A or right to enter. B to close.");
    if (m_themeShop && w == m_themeShop.get())
        return i18n.tr("accessibility.actions.themes", "Up and down to navigate. A to choose. B to close.");
    if ((m_dialog && w == m_dialog.get()) || (m_userSelect && w == m_userSelect.get()))
        return i18n.tr("accessibility.actions.dialog", "Left and right to change choice. A to confirm. B to cancel.");
    if (m_contextMenu && w == m_contextMenu.get())
        return i18n.tr("accessibility.actions.context_menu", "Up and down to choose. A to confirm. B to close.");
    return {};
}

std::string WiiUMenuApp::accessibilityPositionFor(nxui::Widget* w) const {
    if (!w || !m_config.accessibilitySpeakPosition)
        return {};

    auto& i18n = nxui::I18n::instance();
    if (w->tag() == "glossy_icon" && m_grid) {
        const int global = m_grid->focusedGlobalIndex();
        if (global >= 0) {
            if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
                const int total = (int)m_grid->allIcons().size();
                return i18n.tr("accessibility.context.dynamic_line", "Center line") + ", "
                     + i18n.tr("accessibility.position.item", "item") + " " + std::to_string(global + 1)
                     + " " + i18n.tr("accessibility.context.of", "of") + " " + std::to_string(total);
            }
            const int cols = std::max(1, m_grid->columns());
            const int rows = std::max(1, m_grid->rowsPerPage());
            const int local = global % std::max(1, m_grid->iconsPerPage());
            const int row = local / cols + 1;
            const int col = local % cols + 1;
            return i18n.tr("accessibility.position.row", "row") + " " + std::to_string(row)
                 + " " + i18n.tr("accessibility.context.of", "of") + " " + std::to_string(rows)
                 + ". " + i18n.tr("accessibility.position.column", "column") + " " + std::to_string(col)
                 + " " + i18n.tr("accessibility.context.of", "of") + " " + std::to_string(cols);
        }
    }

    auto describeLinear = [&](const auto& buttons) -> std::string {
        for (int i = 0; i < (int)buttons.size(); ++i) {
            if (buttons[(size_t)i].get() == w) {
                return std::to_string(i + 1) + " "
                     + i18n.tr("accessibility.context.of", "of") + " "
                     + std::to_string((int)buttons.size());
            }
        }
        return {};
    };

    if (auto text = describeLinear(m_sidebar.leftButtons()); !text.empty())
        return text;
    if (auto text = describeLinear(m_sidebar.rightButtons()); !text.empty())
        return text;

    for (int i = 0; i < (int)m_userAvatarButtons.size(); ++i) {
        if (m_userAvatarButtons[(size_t)i].get() == w) {
            return std::to_string(i + 1) + " "
                 + i18n.tr("accessibility.context.of", "of") + " "
                 + std::to_string((int)m_userAvatarButtons.size());
        }
    }

    return {};
}

void WiiUMenuApp::announceFocusedWidget(nxui::Widget* w) {
    if (!w)
        return;

    std::string hint = accessibilityActionsFor(w);
    std::string position = accessibilityPositionFor(w);

    std::string originalHint = w->accessibilityHint();
    if (m_accessibility.speakHints()) {
        if (!hint.empty())
            w->setAccessibilityHint(hint);
    } else {
        w->setAccessibilityHint({});
    }
    const bool forceRepeat = w->tag() == "glossy_icon";
    std::string summary = w->accessibilitySummary();
    if (summary.empty())
        summary = w->tag();
    m_accessibility.announceStructuredFocus(accessibilityContextFor(w),
                                            position,
                                            summary,
                                            forceRepeat);
    if ((m_accessibility.speakHints() && !hint.empty()) || !m_accessibility.speakHints())
        w->setAccessibilityHint(originalHint);
}

nxui::Texture* WiiUMenuApp::adoptEditGhostTexture(GlossyIcon* sourceIcon) {
    // The streamer pins the source index for the whole edit operation, so the
    // source texture already has the lifetime needed by the ghost. Re-decoding
    // and uploading a second copy here forced a synchronous GPU drain both
    // when movement started and when it stopped.
    m_editGhostTexture.reset();
    return sourceIcon ? sourceIcon->texture() : nullptr;
}

void WiiUMenuApp::startEditGhost(GlossyIcon* sourceIcon) {
    stopEditGhost();
    if (!sourceIcon)
        return;

    if (m_editSourceIndex >= 0)
        m_iconStreamer.setPinnedIndex(m_editSourceIndex);

    m_editSourceIcon = sourceIcon;
    m_editSourceIcon->setOpacity(0.10f);

    auto ghost = std::make_shared<GlossyIcon>();
    ghost->setTag("edit_ghost");
    ghost->setFocusable(false);
    ghost->setTitle(sourceIcon->title());
    ghost->setTitleId(sourceIcon->titleId());
    ghost->setTexture(adoptEditGhostTexture(sourceIcon));
    ghost->copyWidgetPresentationFrom(*sourceIcon);
    ghost->setIsGameCard(sourceIcon->isGameCard());
    ghost->setGameCardTexture(sourceIcon->gameCardTexture());
    ghost->setNotLaunchable(sourceIcon->isNotLaunchable());
    ghost->setCornerRadius(sourceIcon->cornerRadius());
    ghost->setBlurEnabled(false);
    ghost->setPanelOpacity(0.84f);
    ghost->setOpacity(0.84f);
    ghost->setScale(1.06f);
    ghost->forceVisible();

    m_editGhostTargetRect = m_grid
        ? m_grid->gridSpanRect(m_editSourceIndex,
                               ghost->gridSpanColumns(), ghost->gridSpanRows())
        : sourceIcon->focusRect();
    ghost->setRect(m_editGhostTargetRect);
    m_editGhostPulse = 0.f;

    m_editGhostIcon = ghost;
    DebugLog::log("[edit] ghost started index=%d title=0x%016lX ownedTexture=%d",
                  m_editSourceIndex,
                  static_cast<unsigned long>(sourceIcon->titleId()),
                  m_editGhostTexture ? 1 : 0);
}

void WiiUMenuApp::stopEditGhost() {
    if (m_editGhostIcon || m_editGhostTexture) {
        DebugLog::log("[edit] ghost stopping index=%d ownedTexture=%d",
                      m_editSourceIndex, m_editGhostTexture ? 1 : 0);
    }
    detachEditSourceIcon();

    m_editGhostIcon.reset();
    m_editGhostTexture.reset();
    m_editGhostPulse = 0.f;
}

void WiiUMenuApp::detachEditSourceIcon() {
    m_iconStreamer.clearPinnedIndex();
    if (m_editSourceIcon)
        m_editSourceIcon->setOpacity(1.f);
    m_editSourceIcon = nullptr;
    if (m_editGhostIcon && !m_editGhostTexture)
        m_editGhostIcon->setTexture(nullptr);
}

void WiiUMenuApp::reattachEditSourceIcon() {
    if (!m_editMode || !m_grid || m_editHeldTitleId == 0)
        return;
    const int index = findTitleIndex(m_editHeldTitleId);
    if (index < 0 || index >= static_cast<int>(m_grid->allIcons().size())) {
        m_editSourceIndex = -1;
        return;
    }
    auto& icon = m_grid->allIcons()[static_cast<std::size_t>(index)];
    if (!icon)
        return;
    m_editSourceIndex = index;
    m_editSourceIcon = icon.get();
    m_editSourceIcon->setOpacity(0.10f);
    m_iconStreamer.setPinnedIndex(index);
    if (m_editGhostIcon && !m_editGhostTexture)
        m_editGhostIcon->setTexture(m_editSourceIcon->texture());
}

void WiiUMenuApp::updateEditGhost(float dt) {
    if (!m_editMode || !m_editGhostIcon)
        return;

    if (m_grid && m_editTargetIndex >= 0) {
        const int target = m_editTargetIndex;
        m_editGhostTargetRect = m_grid->gridSpanRect(
            target, m_editGhostIcon->gridSpanColumns(),
            m_editGhostIcon->gridSpanRows());
    } else if (m_cursor && m_cursor->isVisible()) {
        m_editGhostTargetRect = m_cursor->currentRect();
    } else if (auto* cur = focusManager().current()) {
        if (cur->tag() == "glossy_icon")
            m_editGhostTargetRect = cur->focusRect().expanded(4.f);
    }

    m_editGhostPulse += dt;
    float pulse = 0.80f + 0.08f * std::sin(m_editGhostPulse * 8.f);
    m_editGhostIcon->setOpacity(pulse);
    m_editGhostIcon->setPanelOpacity(std::min(1.f, pulse + 0.12f));
    m_editGhostIcon->setScale(1.07f + 0.025f * std::sin(m_editGhostPulse * 7.f));

    m_editGhostIcon->setRect(m_editGhostTargetRect);
}

void WiiUMenuApp::unbindEditActions() {
    if (!m_editBoundIcon)
        return;
    m_editBoundIcon->clearActions();
    m_editBoundIcon = nullptr;
}

void WiiUMenuApp::bindEditActions(GlossyIcon* icon) {
    if (!icon)
        return;
    if (m_editBoundIcon == icon)
        return;

    unbindEditActions();
    m_editBoundIcon = icon;

    icon->addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        activateEditModeTarget();
    });
    icon->addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (m_openFolderId != 0) {
            closeFolder(true);
        } else {
            exitEditMode();
            m_audio.playSfx(Sfx::ModalHide);
        }
    });
    icon->addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        moveFocusedIcon(nxui::FocusDirection::LEFT);
    });
    icon->addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        moveFocusedIcon(nxui::FocusDirection::RIGHT);
    });
    icon->addDirectionAction(nxui::FocusDirection::UP, [this]() {
        moveFocusedIcon(nxui::FocusDirection::UP);
    });
    icon->addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        moveFocusedIcon(nxui::FocusDirection::DOWN);
    });
}

void WiiUMenuApp::enterEditMode() {
    auto* cur = focusManager().current();
    if (!isEditableIcon(cur))
        return;

    auto* icon = static_cast<GlossyIcon*>(cur);
    m_editMode = true;
    m_editSourceIndex = m_grid ? m_grid->focusedGlobalIndex() : -1;
    m_editTargetIndex = m_editSourceIndex;
    m_editHeldTitleId = icon->titleId();
    m_editOriginFolderId = m_openFolderId;
    m_editOriginFolderIndex = m_openFolderId != 0 ? m_editSourceIndex : -1;
    m_editOriginRootSlot = m_openFolderId == 0 ? m_editSourceIndex : -1;
    m_editHeldTitle = icon->title();
    startEditGhost(icon);
    bindEditActions(icon);
    m_titlePill->setText(nxui::I18n::instance().tr("game.move_prefix", "Move: ") + m_editHeldTitle);
    m_titlePill->setVisible(true);
    m_accessibility.announce(nxui::I18n::instance().tr(
        "accessibility.move_mode.enter",
        "Moving game: ") + m_editHeldTitle, true, true);
}

void WiiUMenuApp::exitEditMode() {
    if (!m_editMode)
        return;

    m_editMode = false;
    unbindEditActions();
    m_editSourceIndex = -1;
    m_editTargetIndex = -1;
    m_editOriginRootSlot = -1;
    m_editOriginFolderIndex = -1;
    m_editOriginFolderId = 0;
    m_editHeldTitleId = 0;
    m_editHeldTitle.clear();
    stopEditGhost();

    auto* cur = focusManager().current();
    if (isEditableIcon(cur)) {
        auto* icon = static_cast<GlossyIcon*>(cur);
        m_titlePill->setText(icon->title());
        m_titlePill->setVisible(true);
    } else {
        m_titlePill->hideAnimated();
    }

    if (m_layoutDirty)
        saveMenuLayout();
}

bool WiiUMenuApp::commitEditModePlacement() {
    if (!m_editMode || !m_grid)
        return false;

    int from = m_editSourceIndex;
    int target = m_editTargetIndex;
    if (from < 0 || target < 0 || from >= m_model.count() || target >= m_model.count())
        return false;
    if (m_model.at(from).titleId == 0)
        return false;

    int oldPage = m_grid->currentPage();
    bool changed = (from != target);
    DebugLog::log("[edit] commit from=%d target=%d changed=%d", from, target,
                  changed ? 1 : 0);
    if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
        if (!changed)
            return true;

        std::vector<std::uint64_t> visibleOrder;
        visibleOrder.reserve(static_cast<std::size_t>(m_model.count()));
        for (const auto& entry : m_model.entries()) {
            if (entry.titleId != 0 && entry.kind != GridEntryKind::WidgetContinuation)
                visibleOrder.push_back(entry.titleId);
        }
        auto sourceIt = std::find(visibleOrder.begin(), visibleOrder.end(),
                                  m_editHeldTitleId);
        if (sourceIt == visibleOrder.end())
            return false;
        const std::size_t sourceOrder = static_cast<std::size_t>(
            std::distance(visibleOrder.begin(), sourceIt));
        const std::uint64_t targetTitleId = m_model.at(target).titleId;
        std::size_t destinationOrder = visibleOrder.empty() ? 0 : visibleOrder.size() - 1;
        if (targetTitleId != 0) {
            const auto destinationIt = std::find(visibleOrder.begin(), visibleOrder.end(),
                                                 targetTitleId);
            if (destinationIt == visibleOrder.end())
                return false;
            destinationOrder = static_cast<std::size_t>(
                std::distance(visibleOrder.begin(), destinationIt));
            std::swap(visibleOrder[sourceOrder], visibleOrder[destinationOrder]);
        } else {
            const auto held = visibleOrder[sourceOrder];
            visibleOrder.erase(visibleOrder.begin() +
                               static_cast<std::ptrdiff_t>(sourceOrder));
            visibleOrder.push_back(held);
            destinationOrder = visibleOrder.size() - 1;
        }

        std::vector<std::size_t> visibleSlots;
        if (m_openFolderId != 0) {
            auto* folder = m_folderStore.find(m_openFolderId);
            if (!folder)
                return false;
            folder->titleIds = visibleOrder;
            if (!saveFoldersOrReport("move_single_row"))
                return false;
        } else {
            const std::unordered_set<std::uint64_t> visibleIds(
                visibleOrder.begin(), visibleOrder.end());
            visibleSlots.reserve(visibleOrder.size());
            for (std::size_t index = 0; index < m_layoutSlots.size(); ++index) {
                if (visibleIds.count(m_layoutSlots[index]))
                    visibleSlots.push_back(index);
            }
            if (visibleSlots.size() != visibleOrder.size())
                return false;
        }

        detachEditSourceIcon();
        unbindEditActions();
        if (m_openFolderId != 0) {
            m_editSourceIndex = static_cast<int>(destinationOrder);
            m_editTargetIndex = m_editSourceIndex;
            applyDisplayModel(buildOpenFolderModel(m_openFolderId),
                              m_editHeldTitleId, false);
        } else {
            for (std::size_t index = 0; index < visibleOrder.size(); ++index)
                m_layoutSlots[visibleSlots[index]] = visibleOrder[index];
            m_editSourceIndex = static_cast<int>(destinationOrder);
            m_editTargetIndex = m_editSourceIndex;
            m_editOriginRootSlot = m_editSourceIndex;
            m_layoutDirty = true;
            applyDisplayModel(buildRootFolderModel(), m_editHeldTitleId, false);
        }
        reattachEditSourceIcon();
        if (auto* focused = m_grid->focusManager().current()) {
            focusManager().setFocus(focused);
            if (focused->tag() == "glossy_icon")
                bindEditActions(static_cast<GlossyIcon*>(focused));
        }
        return true;
    }

    if (m_openFolderId == 0) {
        const auto sizeForTitle = [this](std::uint64_t titleId) {
            const auto widgetId = switchu::widgets::widgetIdFromTitleId(titleId);
            if (const auto* widget = widgetId ? m_widgetStore.find(widgetId) : nullptr)
                return switchu::widgets::validatedSize(
                    widget->type, widget->size, AppLayoutMode::Grid);
            return gameGridSize(titleId, AppLayoutMode::Grid);
        };
        const std::uint64_t displacedTitleId = m_model.at(target).titleId;
        const auto heldSize = sizeForTitle(m_editHeldTitleId);
        const auto displacedSize = sizeForTitle(displacedTitleId);
        const int columns = std::max(1, m_grid->columns());
        const auto footprintsOverlap = [columns](
                int firstAnchor, switchu::widgets::WidgetSize first,
                int secondAnchor, switchu::widgets::WidgetSize second) {
            for (int firstY = 0; firstY < first.rows; ++firstY) {
                for (int firstX = 0; firstX < first.columns; ++firstX) {
                    const int firstCell = firstAnchor + firstY * columns + firstX;
                    for (int secondY = 0; secondY < second.rows; ++secondY)
                        for (int secondX = 0; secondX < second.columns; ++secondX)
                            if (firstCell == secondAnchor + secondY * columns + secondX)
                                return true;
                }
            }
            return false;
        };
        if (!canPlaceGridItem(target, heldSize, m_editHeldTitleId,
                              displacedTitleId))
            return false;

        int displacedAnchor = from;
        if (displacedTitleId != 0 &&
            (!canPlaceGridItem(displacedAnchor, displacedSize,
                               m_editHeldTitleId, displacedTitleId) ||
             footprintsOverlap(target, heldSize,
                               displacedAnchor, displacedSize))) {
            displacedAnchor = -1;
            int bestDistance = std::numeric_limits<int>::max();
            for (int candidate = 0;
                 candidate < static_cast<int>(m_layoutSlots.size()); ++candidate) {
                if (!canPlaceGridItem(candidate, displacedSize,
                                      m_editHeldTitleId, displacedTitleId) ||
                    footprintsOverlap(target, heldSize, candidate, displacedSize))
                    continue;
                const int distance = std::abs(candidate - from);
                if (distance < bestDistance) {
                    displacedAnchor = candidate;
                    bestDistance = distance;
                }
            }
            if (displacedAnchor < 0)
                return false;
        }

        if (changed) {
            detachEditSourceIcon();
            unbindEditActions();
            std::replace(m_layoutSlots.begin(), m_layoutSlots.end(),
                         m_editHeldTitleId, std::uint64_t{0});
            if (displacedTitleId != 0)
                std::replace(m_layoutSlots.begin(), m_layoutSlots.end(),
                             displacedTitleId, std::uint64_t{0});
            m_layoutSlots[static_cast<std::size_t>(target)] = m_editHeldTitleId;
            if (displacedTitleId != 0)
                m_layoutSlots[static_cast<std::size_t>(displacedAnchor)] = displacedTitleId;
            m_editSourceIndex = target;
            m_editOriginRootSlot = target;
            m_layoutDirty = true;
            applyDisplayModel(buildRootFolderModel(), m_editHeldTitleId, false);
            reattachEditSourceIcon();
            if (auto* focused = m_grid->focusManager().current()) {
                focusManager().setFocus(focused);
                if (focused->tag() == "glossy_icon")
                    bindEditActions(static_cast<GlossyIcon*>(focused));
            }
        }
        return true;
    }
    if (changed) {
        // Keep the three index-based stores atomic. A catalogue refresh can
        // resize the streamer between entering move mode and committing it;
        // partially swapping the model used to leave texture/index pointers
        // inconsistent for the next render.
        if (!m_iconStreamer.swapIndices(from, target)) {
            DebugLog::log("[edit] rejected stale streamer swap from=%d target=%d", from, target);
            return false;
        }
        if (!m_model.swapEntries(from, target)) {
            m_iconStreamer.swapIndices(from, target);
            return false;
        }
        if (!m_grid->swapSlots(from, target)) {
            m_model.swapEntries(from, target);
            m_iconStreamer.swapIndices(from, target);
            return false;
        }
        if (!m_layoutSlots.empty() && from < (int)m_layoutSlots.size() && target < (int)m_layoutSlots.size())
            std::swap(m_layoutSlots[from], m_layoutSlots[target]);

        m_editSourceIndex = target;
        m_iconStreamer.setPinnedIndex(m_editSourceIndex);
        m_layoutDirty = true;
    }

    m_grid->focusGlobalIndex(target);

    int newPage = m_grid->currentPage();
    if (changed || newPage != oldPage) {
        m_iconStreamer.onPageChanged(newPage, m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }
    for (auto* icon : m_grid->pageIcons()) {
        if (icon)
            icon->forceVisible();
    }

    if (auto* cur = m_grid->focusManager().current())
        focusManager().setFocus(cur);

    if (m_editGhostIcon) {
        if (auto* focused = m_grid->focusManager().current())
            m_editGhostTargetRect = focused->focusRect().expanded(4.f);
    }

    updateCursor();
    return true;
}

bool WiiUMenuApp::activateEditModeTarget() {
    if (!m_editMode || !m_grid)
        return false;

    const int target = m_editTargetIndex;
    if (target < 0 || target >= m_model.count() || m_editHeldTitleId == 0)
        return false;

    const AppEntry targetEntry = m_model.at(target);
    if (m_openFolderId == 0 && targetEntry.isFolder() &&
        m_editHeldTitleId < kFolderTitleIdPrefix) {
        detachEditSourceIcon();
        unbindEditActions();
        requestOpenFolder(targetEntry.folderId);
        return true;
    }

    if (m_openFolderId != 0 && m_editHeldTitleId < kFolderTitleIdPrefix) {
        const std::uint32_t targetFolderId = m_openFolderId;
        if (!m_folderStore.placeTitle(targetFolderId, m_editHeldTitleId,
                                      static_cast<std::size_t>(target)) ||
            !saveFoldersOrReport("place_in_folder"))
            return false;

        if (m_editOriginFolderId == 0 && m_editOriginRootSlot >= 0 &&
            m_editOriginRootSlot < static_cast<int>(m_layoutSlots.size())) {
            m_layoutSlots[static_cast<std::size_t>(m_editOriginRootSlot)] = 0;
            m_layoutDirty = true;
        }
        const std::uint64_t titleId = m_editHeldTitleId;
        exitEditMode();
        applyDisplayModel(buildOpenFolderModel(targetFolderId), titleId, false);
        m_audio.playSfx(Sfx::ConfirmPositive);
        return true;
    }

    if (m_openFolderId == 0 && targetEntry.kind == GridEntryKind::Empty &&
        m_editOriginFolderId != 0 &&
        target < static_cast<int>(m_layoutSlots.size())) {
        if (!m_folderStore.removeTitle(m_editOriginFolderId, m_editHeldTitleId) ||
            !saveFoldersOrReport("move_out_of_folder"))
            return false;
        const std::uint64_t titleId = m_editHeldTitleId;
        m_layoutSlots[static_cast<std::size_t>(target)] = titleId;
        m_layoutDirty = true;
        exitEditMode();
        applyDisplayModel(buildRootFolderModel(), titleId, false);
        m_audio.playSfx(Sfx::ConfirmPositive);
        return true;
    }

    if (m_editSourceIndex < 0)
        return false;
    if (!commitEditModePlacement())
        return false;
    exitEditMode();
    m_audio.playSfx(Sfx::ConfirmPositive);
    return true;
}

bool WiiUMenuApp::moveFocusedIcon(nxui::FocusDirection dir) {
    if (!m_editMode || !m_grid)
        return false;

    const int from = m_editTargetIndex;
    if (from < 0 || from >= m_model.count())
        return false;

    if (m_grid->isDynamicLine()) {
        int target = from;
        if (dir == nxui::FocusDirection::LEFT)
            --target;
        else if (dir == nxui::FocusDirection::RIGHT)
            ++target;
        else
            return false;
        if (target < 0 || target >= m_model.count() ||
            !m_grid->focusGlobalIndex(target))
            return false;
        m_editTargetIndex = target;
        if (auto* focused = m_grid->focusManager().current())
            focusManager().setFocus(focused);
        m_editGhostTargetRect = m_grid->gridSpanRect(target, 1, 1);
        updateCursor();
        return true;
    }

    const int cols = std::max(1, m_grid->columns());
    const int rows = std::max(1, m_grid->rowsPerPage());
    const int perPage = std::max(1, m_grid->iconsPerPage());
    const int totalPages = std::max(1, m_grid->totalPages());

    const int page = from / perPage;
    const int local = from % perPage;
    const int col = local % cols;
    const int row = local / cols;
    const int spanColumns = m_editGhostIcon
        ? std::max(1, m_editGhostIcon->gridSpanColumns()) : 1;
    const int spanRows = m_editGhostIcon
        ? std::max(1, m_editGhostIcon->gridSpanRows()) : 1;

    int target = from;
    switch (dir) {
        case nxui::FocusDirection::LEFT:
            if (col > 0)
                target = from - 1;
            else if (page > 0)
                target = (page - 1) * perPage + row * cols + (cols - 1);
            else
                return false;
            break;
        case nxui::FocusDirection::RIGHT:
            if (col + spanColumns < cols)
                target = from + 1;
            else if (page + 1 < totalPages)
                target = (page + 1) * perPage + row * cols;
            else
                return false;
            break;
        case nxui::FocusDirection::UP:
            if (row > 0)
                target = from - cols;
            else
                return false;
            break;
        case nxui::FocusDirection::DOWN:
            if (row + spanRows < rows)
                target = from + cols;
            else
                return false;
            break;
    }

    if (target < 0 || target >= m_model.count() || target == from)
        return false;

    m_editTargetIndex = target;
    const int targetPage = target / perPage;
    if (targetPage != m_grid->currentPage()) {
        m_grid->startPageTransition(targetPage);
        m_iconStreamer.onPageChanged(targetPage, perPage,
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }
    // Focus the underlying selectable item when possible, but the edit anchor
    // remains valid even over an invisible continuation cell.
    if (m_grid->focusGlobalIndex(target)) {
        if (auto* focused = m_grid->focusManager().current())
            focusManager().setFocus(focused);
    }
    m_editGhostTargetRect = m_grid->gridSpanRect(
        m_editTargetIndex, spanColumns, spanRows);
    updateCursor();
    return true;
}

void WiiUMenuApp::wireFocusCallback() {
    focusManager().onFocusChanged([this](nxui::Widget*, nxui::Widget* cur) {
        updateCursor();
        announceFocusedWidget(cur);

        if ((m_contextMenu && m_contextMenu->isActive()) ||
            (m_dialog && m_dialog->isActive()) ||
            (m_themeShop && m_themeShop->isActive()) ||
            (m_settings && m_settings->isActive()) ||
            (m_steamGridDbPicker && m_steamGridDbPicker->isActive()) ||
            (m_userSelect && m_userSelect->isActive()))
            return;

        bool suppressSfx = m_suppressNextNavigateSfx;
        m_suppressNextNavigateSfx = false;
        if (!suppressSfx)
            m_audio.playSfx(Sfx::Navigate);

        if (cur && cur->tag() == "glossy_icon") {
            m_grid->focusManager().setFocus(cur);
            auto* icon = static_cast<GlossyIcon*>(cur);
            if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
                const int focusedIndex = m_grid->focusedGlobalIndex();
                if (focusedIndex >= 0) {
                    m_iconStreamer.onPageChanged(focusedIndex, 1,
                                                 app().gpu(), app().renderer(),
                                                 m_grid->allIcons());
                }
            }
            if (m_steamGridDbBackdrop && icon->titleId() != 0
                && icon->titleId() < kFolderTitleIdPrefix)
                m_steamGridDbBackdrop->showTitle(icon->titleId());
            auto& i18n = nxui::I18n::instance();
            if (m_editMode) {
                bindEditActions(icon);
                if (m_editGhostIcon)
                    m_editGhostTargetRect = m_grid->gridSpanRect(
                        m_editTargetIndex,
                        m_editGhostIcon->gridSpanColumns(),
                        m_editGhostIcon->gridSpanRows());
                else
                    m_editGhostTargetRect = icon->focusRect();
                if (!m_editHeldTitle.empty())
                    m_titlePill->setText(i18n.tr("game.move_prefix", "Move: ") + m_editHeldTitle);
                else if (icon->titleId() != 0)
                    m_titlePill->setText(i18n.tr("game.move_prefix", "Move: ") + icon->title());
                else
                    m_titlePill->setText(i18n.tr("game.move", "Move"));
                m_titlePill->setVisible(true);
                return;
            }
            if (icon->titleId() == 0) {
                m_titlePill->hideAnimated();
                return;
            }
#ifdef SWITCHU_MENU
            if (m_launcher.isAppSuspended(icon->titleId())) {
                m_titlePill->setText(icon->title());
            } else
#endif
            m_titlePill->setText(icon->title());
            m_titlePill->setVisible(true);
        } else if (cur) {
            if (m_editMode)
                exitEditMode();
            for (auto& btn : m_sidebar.leftButtons()) {
                if (btn.get() == cur) { m_titlePill->setText(btn->label()); m_titlePill->setVisible(true); return; }
            }
            for (auto& btn : m_sidebar.rightButtons()) {
                if (btn.get() == cur) { m_titlePill->setText(btn->label()); m_titlePill->setVisible(true); return; }
            }
            for (auto& avatar : m_userAvatarButtons) {
                if (avatar.get() == cur) {
                    m_titlePill->setText(avatar->nickname());
                    m_titlePill->setVisible(!avatar->nickname().empty());
                    return;
                }
            }
            m_titlePill->hideAnimated();
        } else {
            m_titlePill->hideAnimated();
        }
    });
    updateCursor();
    if (auto* cur = focusManager().current()) {
        if (cur->tag() == "glossy_icon") {
            auto* icon = static_cast<GlossyIcon*>(cur);
            if (icon->titleId() != 0)
                m_titlePill->setText(icon->title());
        }
    }
}

bool WiiUMenuApp::isCurrentFocusableWidget(nxui::Widget* w) const {
    if (!w) return false;
    if (m_steamGridDbPicker && m_steamGridDbPicker.get() == w) return w->isFocusable();
    if (m_themeShop && m_themeShop.get() == w) return w->isFocusable();
    if (m_settings && m_settings.get() == w) return w->isFocusable();
    for (const auto& btn : m_sidebar.leftButtons())
        if (btn.get() == w) return w->isFocusable();
    for (const auto& btn : m_sidebar.rightButtons())
        if (btn.get() == w) return w->isFocusable();
    for (const auto& avatar : m_userAvatarButtons)
        if (avatar.get() == w) return w->isFocusable();
    if (m_grid)
        for (const auto& icon : m_grid->allIcons())
            if (icon.get() == w) return w->isFocusable();
    return false;
}

int WiiUMenuApp::findTitleIndex(uint64_t titleId) const {
    if (titleId == 0)
        return -1;
    for (int i = 0; i < m_model.count(); ++i) {
        if (m_model.at(i).titleId == titleId)
            return i;
    }
    return -1;
}

bool WiiUMenuApp::focusTitle(uint64_t titleId) {
    if (!m_grid)
        return false;

    int idx = findTitleIndex(titleId);
    if (idx < 0 && titleId != 0) {
        const std::uint32_t folderId = m_folderStore.folderForTitle(titleId);
        if (folderId == 0 || folderId == m_openFolderId)
            return false;
        requestOpenFolder(folderId, titleId);
        idx = findTitleIndex(folderTitleId(folderId));
        if (idx < 0)
            return true;
    }

    int oldPage = m_grid->currentPage();
    if (!m_grid->focusGlobalIndex(idx))
        return false;

    if (m_grid->currentPage() != oldPage || titleId != 0) {
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }

    if (auto* cur = m_grid->focusManager().current())
        focusManager().setFocus(cur);
    updateCursor();
    return true;
}

void WiiUMenuApp::markSuspendedIcon(uint64_t titleId) {
    if (!m_grid)
        return;
    for (auto& icon : m_grid->allIcons())
        icon->setSuspended(titleId != 0 && icon->titleId() == titleId);
    if (titleId != 0)
        focusTitle(titleId);

    if (auto* cur = m_grid->focusManager().current()) {
        auto* icon = static_cast<GlossyIcon*>(cur);
        if (m_launcher.isAppSuspended(icon->titleId())) {
            m_titlePill->setText(icon->title());
        } else {
            m_titlePill->setText(icon->title());
        }
    }
}

void WiiUMenuApp::closeActiveOverlays() {
    // Transfer input ownership before starting any exit animation. An overlay
    // that is still visually fading out must never win focusRoot().
    m_navigator.resetToHome();
    if (m_editMode)
        exitEditMode();
    if (m_userSelect && m_userSelect->isActive())
        m_userSelect->hide();
    if (m_contextMenu && m_contextMenu->isActive())
        m_contextMenu->hide();
    if (m_dialog && m_dialog->isActive())
        m_dialog->hide();
    if (m_settings && m_settings->isActive())
        m_settings->hide();
    if (m_themeShop && m_themeShop->isActive())
        m_themeShop->hide();
    if (m_gameOptions && m_gameOptions->isActive())
        m_gameOptions->hide();
    if (m_folderOptions && m_folderOptions->isActive())
        m_folderOptions->hide();
    if (m_controllerTest && m_controllerTest->isActive())
        m_controllerTest->hide();
    if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
        m_steamGridDbPicker->hide();
    if (m_openFolderId != 0)
        closeFolder();
}

nxui::Widget* WiiUMenuApp::focusRoot() {
    if (m_launchAnim && m_launchAnim->isPlaying()) return nullptr;
    if (m_folderCaptureRequested) return nullptr;
    if (m_progressDialog && m_progressDialog->isActive()) return m_progressDialog.get();
    if (m_dialog && m_dialog->isActive()) return m_dialog.get();
    if (m_contextMenu && m_contextMenu->isActive()) return m_contextMenu.get();
    if (m_userSelect && m_userSelect->isActive()) return m_userSelect.get();
    if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
        return m_steamGridDbPicker.get();
    switch (m_navigator.route()) {
        case switchu::navigation::Route::Settings:
            return m_settings ? m_settings.get() : &rootBox();
        case switchu::navigation::Route::ThemeShop:
            return m_themeShop ? m_themeShop.get() : &rootBox();
        case switchu::navigation::Route::GameOptions:
            return m_gameOptions ? m_gameOptions.get() : &rootBox();
        case switchu::navigation::Route::FolderOptions:
            return m_folderOptions ? m_folderOptions.get() : &rootBox();
        case switchu::navigation::Route::ControllerTest:
            return m_controllerTest ? m_controllerTest.get() : &rootBox();
        case switchu::navigation::Route::Home:
            break;
    }
    return &rootBox();
}

void WiiUMenuApp::toggleAccessibilitySpeech() {
    auto& i18n = nxui::I18n::instance();
    const bool enabled = !m_config.accessibilityEnabled;
    m_config.accessibilityEnabled = enabled;
    if (m_settings)
        m_settings->setAccessibilityEnabledState(enabled);
    if (m_themeShop)
        m_themeShop->setAccessibilityVoiceEnabled(enabled);
    if (m_gameOptions)
        m_gameOptions->setAccessibilityVoiceEnabled(enabled);
    if (m_folderOptions)
        m_folderOptions->setAccessibilityVoiceEnabled(enabled);

    if (enabled) {
        m_audio.playSfx(Sfx::ThemeToggle);
        m_accessibility.setEnabled(true);
        m_accessibility.announce(i18n.tr("accessibility.speech.enabled",
                                         "Voice guidance enabled."), true, true);
    } else {
        m_accessibility.announceAndDisable(i18n.tr("accessibility.speech.disabled",
                                                   "Voice guidance disabled."));
    }
    m_config.save();
}

bool WiiUMenuApp::handleAccessibilityToggleCombo() {
    if (m_navigator.route() == switchu::navigation::Route::ControllerTest)
        return false;
    auto& input = app().input();
    if (!input.isDown(nxui::Button::Plus) || !input.isDown(nxui::Button::Minus))
        return false;
    if (m_accessibilityToggleComboHeld)
        return true;
    m_accessibilityToggleComboHeld = true;
    m_plusExitPending = false;
    m_plusExitPendingTimer = 0.f;
    toggleAccessibilitySpeech();
    return true;
}

void WiiUMenuApp::wireGlobalActions() {
    auto& root = rootBox();

    root.addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if ((m_contextMenu && m_contextMenu->isActive()) ||
            (m_dialog && m_dialog->isActive()) ||
            (m_settings && m_settings->isActive()) ||
            (m_themeShop && m_themeShop->isActive()) ||
            (m_gameOptions && m_gameOptions->isActive()) ||
            (m_folderOptions && m_folderOptions->isActive()) ||
            (m_controllerTest && m_controllerTest->isActive()) ||
            (m_userSelect && m_userSelect->isActive()))
            return;
        if (m_openFolderId != 0 && !(m_dialog && m_dialog->isActive()))
            closeFolder();
    });

    root.addAction(static_cast<uint64_t>(nxui::Button::L), [this]() {
        if (m_navigator.route() == switchu::navigation::Route::ControllerTest)
            return;
        m_accessibility.repeatLastAnnouncement();
    });

    // The grid holds the open folder's model, so paging works inside a folder too.
    root.addAction(static_cast<uint64_t>(nxui::Button::ZL), [this]() {
        if (m_navigator.route() != switchu::navigation::Route::Home || focusRoot() != &rootBox())
            return;
        flipPage(-1);
    });
    root.addAction(static_cast<uint64_t>(nxui::Button::ZR), [this]() {
        if (m_navigator.route() != switchu::navigation::Route::Home || focusRoot() != &rootBox())
            return;
        flipPage(+1);
    });
    root.addAction(static_cast<uint64_t>(nxui::Button::Y), [this]() {
        if ((m_contextMenu && m_contextMenu->isActive()) ||
            (m_dialog && m_dialog->isActive()) ||
            (m_themeShop && m_themeShop->isActive()) ||
            (m_settings && m_settings->isActive()) ||
            (m_gameOptions && m_gameOptions->isActive()) ||
            (m_folderOptions && m_folderOptions->isActive()) ||
            (m_controllerTest && m_controllerTest->isActive()) ||
            (m_userSelect && m_userSelect->isActive())) {
            return;
        }

        auto* cur = focusManager().current();
        if (m_editMode)
            return;
        if (!isEditableIcon(cur))
            return;

        enterEditMode();
        m_audio.playSfx(Sfx::Activate);
    });
#ifdef SWITCHU_DEBUG_UI
    root.addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        if (m_navigator.route() == switchu::navigation::Route::ControllerTest)
            return;
        if (handleAccessibilityToggleCombo())
            return;
        m_showDebugOverlay = !m_showDebugOverlay;
        DebugLog::log("[debug] ImGui overlay toggled: %d", m_showDebugOverlay ? 1 : 0);
    });
#else
    root.addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        if (m_navigator.route() == switchu::navigation::Route::ControllerTest)
            return;
        handleAccessibilityToggleCombo();
    });
#endif
#ifdef SWITCHU_HOMEBREW
    root.addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        if (m_navigator.route() == switchu::navigation::Route::ControllerTest)
            return;
        if (handleAccessibilityToggleCombo())
            return;
        m_plusExitPending = true;
        m_plusExitPendingTimer = 0.80f;
    });
#endif

#ifdef SWITCHU_MENU
    root.addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (m_editMode) return;
        if (m_launcher.suspendedTitleId() == 0) return;
        auto* cur = focusManager().current();
        if (!cur || cur->tag() != "glossy_icon") return;
        auto* icon = static_cast<GlossyIcon*>(cur);
        if (!m_launcher.isAppSuspended(icon->titleId())) return;

        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = cur;
        auto& i18n = nxui::I18n::instance();
        m_dialog->show(
            i18n.tr("game.close_title", "Close game"),
            i18n.tr("game.close_prefix", "Close") + std::string(" ") + icon->title()
                + i18n.tr("game.close_suffix", "?\nUnsaved progress will be lost."),
            {
                {i18n.tr("button.cancel", "Cancel"), [this]() {}, true},
                {i18n.tr("button.close", "Close"),  [this]() {
                    m_launcher.terminateApplication();
                    // Keep the visible suspended state until the daemon emits
                    // ApplicationExited. Clearing it optimistically made a
                    // failed termination indistinguishable from success.
                }, true}
            },
            1,
            {}
        );
        focusManager().setFocus(m_dialog.get());
    });
#endif
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::showGameContextMenu(GlossyIcon* icon) {
    if (!icon || !m_gameOptions || icon->titleId() == 0)
        return;

    const uint64_t titleId = icon->titleId();
    const std::string title = icon->title();
    auto& i18n = nxui::I18n::instance();
    switchu::control_cache::Meta meta{};
    const bool hasMeta = switchu::control_cache::readMeta(titleId, meta);
    const std::string version = hasMeta && meta.display_version[0] != '\0'
        ? std::string(meta.display_version)
        : i18n.tr("game.version_unknown", "Unknown");

    GameOptionsScreen::GameInfo game;
    game.titleId = titleId;
    game.name = title;
    game.version = version;
    game.publisher = hasMeta && meta.publisher[0] != '\0'
        ? std::string(meta.publisher)
        : i18n.tr("game.publisher_unknown", "Unknown publisher");
    game.icon = icon->texture();
    game.gameCard = icon->isGameCard();
    game.suspended = m_launcher.isAppSuspended(titleId);
    game.canMove = m_openFolderId == 0;
    game.canResize = m_openFolderId == 0;
    const auto currentSize = gameGridSize(titleId, AppLayoutMode::Grid);
    game.sizeIndex = currentSize == switchu::widgets::WidgetSize{2, 2}
        ? 2 : (currentSize == switchu::widgets::WidgetSize{2, 1} ? 1 : 0);
    m_gameOptions->setGame(game);
    m_gameOptions->onMove([this]() {
        if (m_gameOptions) m_gameOptions->hide();
        if (focusTitle(m_gameOptionsTitleId)) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
        enterEditMode();
        m_audio.playSfx(Sfx::Activate);
    });
    m_gameOptions->onResize([this, titleId](int sizeIndex) {
        const switchu::widgets::WidgetSize requested = sizeIndex == 2
            ? switchu::widgets::WidgetSize{2, 2}
            : (sizeIndex == 1 ? switchu::widgets::WidgetSize{2, 1}
                              : switchu::widgets::WidgetSize{1, 1});
        m_gameSizes[titleId] = requested;
        if (requested == switchu::widgets::WidgetSize{1, 1})
            m_gameArtwork.erase(titleId);
        normalizeWidgetPlacements();
        m_layoutDirty = true;
        saveMenuLayout();
        if (m_gameOptions) m_gameOptions->hide();
        m_navigator.resetToHome();
        if (m_openFolderId == 0)
            applyDisplayModel(buildRootFolderModel(), titleId, false);
    });
    m_gameOptions->onCloseSoftware([this]() { m_launcher.terminateApplication(); });
    m_gameOptions->onDeleteSoftware([this, titleId, title]() {
        auto& localI18n = nxui::I18n::instance();
        m_dialogReturnFocus = m_gameOptions.get();
        m_dialog->show(
            localI18n.tr("game.uninstall_title", "Delete software"),
            localI18n.tr("game.delete_confirmation", "The software will be deleted. Save data will remain.")
                + std::string("\n") + title,
            {
                {localI18n.tr("button.cancel", "Cancel"), []() {}, true},
                {localI18n.tr("button.delete", "Delete"), [this, titleId, title]() {
                    startSoftwareDeletion(titleId, title, true);
                }, true},
            }, 0, {});
        focusManager().setFocus(m_dialog.get());
    });
    m_gameOptions->onSelectArtwork([this](GameOptionsScreen::ArtworkKind kind) {
        openSteamGridDbPicker(kind);
    });
    m_audio.playSfx(Sfx::ModalShow);
    m_gameOptionsTitleId = titleId;
    m_navigator.navigate(switchu::navigation::Route::GameOptions);
    m_gameOptions->show();
    focusManager().setFocus(m_gameOptions.get());
}

void WiiUMenuApp::showFolderContextMenu(std::uint32_t folderId) {
    const auto* folder = m_folderStore.find(folderId);
    if (!folder || !m_folderOptions || !m_dialog) return;

    const std::string name = folder->name;
    FolderOptionsScreen::FolderInfo info;
    info.id = folder->id;
    info.name = folder->name;
    info.itemCount = static_cast<int>(folder->titleCount());
    info.colorIndex = folder->colorIndex;
    info.sizeIndex = folder->sizeIndex;
    m_folderOptions->setFolder(info);
    m_folderOptions->onOpen([this, folderId]() {
        if (m_folderOptions) m_folderOptions->hide();
        m_navigator.resetToHome();
        requestOpenFolder(folderId);
    });
    m_folderOptions->onRename([this, folderId]() {
        if (m_folderOptions) m_folderOptions->hide();
        m_navigator.resetToHome();
        renameFolder(folderId);
    });
    m_folderOptions->onColorChange([this, folderId](int colorIndex) {
        if (!m_folderStore.setColorIndex(folderId, colorIndex) ||
            !saveFoldersOrReport("folder_color"))
            return;
        const std::uint64_t id = folderTitleId(folderId);
        const int index = findTitleIndex(id);
        if (index >= 0 && index < m_model.count()) {
            m_model.at(index).folderColorIndex = colorIndex;
            const auto& icons = m_grid->allIcons();
            if (index < static_cast<int>(icons.size()) && icons[static_cast<std::size_t>(index)])
                icons[static_cast<std::size_t>(index)]->setFolderColorIndex(colorIndex);
        }
        if (m_openFolderId == folderId && m_pageIndicator)
            m_pageIndicator->setActiveColor(switchu::folders::colorForIndex(colorIndex));
    });
    m_folderOptions->onSizeChange([this, folderId](int sizeIndex) {
        if (!m_folderStore.setSizeIndex(folderId, sizeIndex))
            return;
        saveFoldersOrReport("folder_size");
    });
    m_folderOptions->onDelete([this, folderId, name]() {
        auto& local = nxui::I18n::instance();
        m_dialogReturnFocus = m_folderOptions.get();
        m_dialog->show(local.tr("folder.delete", "Delete folder"),
                       local.tr("folder.delete_desc", "Games inside will return to the HOME menu."),
                       {
                           {local.tr("button.cancel", "Cancel"), {}, true},
                           {local.tr("button.delete", "Delete"), [this, folderId]() {
                               m_folderStore.remove(folderId);
                               if (!saveFoldersOrReport("delete")) return;
                               const auto pseudo = folderTitleId(folderId);
                               std::replace(m_layoutSlots.begin(), m_layoutSlots.end(), pseudo, std::uint64_t{0});
                               m_layoutDirty = true;
                               if (m_folderOptions) m_folderOptions->hide();
                               m_navigator.resetToHome();
                               applyDisplayModel(buildRootFolderModel(), 0, false);
                               m_audio.playSfx(Sfx::ConfirmPositive);
                           }, true}
                       }, 0, {});
        focusManager().setFocus(m_dialog.get());
    });

    m_audio.playSfx(Sfx::ModalShow);
    m_folderOptionsId = folderId;
    m_navigator.navigate(switchu::navigation::Route::FolderOptions);
    m_folderOptions->show();
    focusManager().setFocus(m_folderOptions.get());
}
#endif

void WiiUMenuApp::handleTouch() {
    constexpr float kSwipeThreshold = 80.f;
    constexpr float kLongPressThreshold = 0.55f;
    constexpr float kLongPressMoveThreshold = 18.f;

    auto& input = app().input();

    auto hitAvatar = [this](float x, float y) -> UserAvatarButton* {
        for (auto& avatar : m_userAvatarButtons) {
            if (avatar && avatar->isVisible() && avatar->hitTest(x, y))
                return avatar.get();
        }
        return nullptr;
    };

    auto focusTouchedIcon = [this](int localHit) -> GlossyIcon* {
        if (!m_grid || localHit < 0)
            return nullptr;

        int global = m_grid->currentPage() * m_grid->iconsPerPage() + localHit;
        if (m_editMode)
            m_editTargetIndex = global;
        if (!m_grid->focusGlobalIndex(global))
            return nullptr;

        auto* cur = m_grid->focusManager().current();
        if (!cur)
            return nullptr;

        focusManager().setFocus(cur);
        if (m_cursor) {
            nxui::Rect cursorRect = cur->focusRect();
            if (m_editMode && m_editGhostIcon)
                cursorRect = m_grid->gridSpanRect(
                    global, m_editGhostIcon->gridSpanColumns(),
                    m_editGhostIcon->gridSpanRows());
            m_cursor->moveTo(cursorRect.expanded(4.f), 0.f);
            m_cursor->setVisible(true);
        } else {
            updateCursor();
        }

        if (!isEditableIcon(cur))
            return nullptr;
        return static_cast<GlossyIcon*>(cur);
    };

    if (input.touchDown()) {
        float tx = input.touchX();
        float ty = input.touchY();

        m_touchArrowLeft = m_touchArrowRight = false;
        if (m_arrowAnimLeft.show > 0.5f && pageArrowRect(true).expanded(12.f).contains(tx, ty)) {
            m_touchArrowLeft = true;
            m_touchHitIndex = -1;
            return;
        }
        if (m_arrowAnimRight.show > 0.5f && pageArrowRect(false).expanded(12.f).contains(tx, ty)) {
            m_touchArrowRight = true;
            m_addPageTouchHold = m_addPageMode;
            m_touchHitIndex = -1;
            return;
        }

        m_touchAvatarTarget = hitAvatar(tx, ty);
        m_touchAvatarWasFocused = m_touchAvatarTarget && (focusManager().current() == m_touchAvatarTarget);
        if (m_touchAvatarTarget) {
            m_touchHitIndex = -1;
            m_touchOnFocused = false;
            m_touchEditDragActive = false;
            return;
        }

        int hit = m_grid->hitTest(tx, ty);
        m_touchHitIndex = hit;
        m_touchOnFocused = false;
        m_touchEditDragActive = false;
        if (hit >= 0) {
            auto icons = m_grid->pageIcons();
            if (hit < (int)icons.size())
                m_touchOnFocused = (icons[hit] == focusManager().current());
        }
    }

    if (input.isTouching() && m_addPageTouchHold &&
        !pageArrowRect(false).expanded(20.f).contains(input.touchX(), input.touchY())) {
        m_addPageTouchHold = false;
    }

    if (input.isTouching() && m_touchHitIndex >= 0) {
        float dx = input.touchDeltaX();
        float dy = input.touchDeltaY();

        if (!m_editMode
            && std::abs(dx) <= kLongPressMoveThreshold
            && std::abs(dy) <= kLongPressMoveThreshold
            && input.touchDuration() >= kLongPressThreshold)
        {
            if (auto* icon = focusTouchedIcon(m_touchHitIndex)) {
                enterEditMode();
                if (m_editMode) {
                    m_touchEditDragActive = true;
                    m_audio.playSfx(Sfx::Activate);
                    m_editGhostTargetRect = m_grid->gridSpanRect(
                        m_editTargetIndex,
                        m_editGhostIcon->gridSpanColumns(),
                        m_editGhostIcon->gridSpanRows());
                }
            }
        }

        if (m_editMode && m_touchEditDragActive) {
            int dragHit = m_grid->hitTest(input.touchX(), input.touchY());
            if (dragHit >= 0)
                focusTouchedIcon(dragHit);
        }
    }

    if (input.touchUp()) {
        if (m_touchArrowLeft || m_touchArrowRight) {
            const bool left = m_touchArrowLeft;
            const bool wasAddHold = m_addPageTouchHold;
            m_touchArrowLeft = m_touchArrowRight = false;
            m_addPageTouchHold = false;
            if (!wasAddHold &&
                pageArrowRect(left).expanded(12.f).contains(input.touchX(), input.touchY()))
                flipPage(left ? -1 : +1);
            return;
        }

        if (m_touchAvatarTarget) {
            float dx = input.touchDeltaX();
            float dy = input.touchDeltaY();
            UserAvatarButton* avatar = m_touchAvatarTarget;
            m_touchAvatarTarget = nullptr;
            if (std::abs(dx) < 20.f && std::abs(dy) < 20.f &&
                hitAvatar(input.touchX(), input.touchY()) == avatar)
            {
                focusManager().setFocus(avatar);
                if (!m_touchAvatarWasFocused)
                    avatar->activate();
            }
            m_touchAvatarWasFocused = false;
            return;
        }

        if (m_editMode && m_touchEditDragActive) {
            bool changed = activateEditModeTarget();
            m_audio.playSfx(changed ? Sfx::ConfirmPositive : Sfx::ModalHide);
            m_touchHitIndex = -1;
            m_touchEditDragActive = false;
            return;
        }

        float dx = input.touchDeltaX();
        float dy = input.touchDeltaY();
        if (std::abs(dx) > kSwipeThreshold && std::abs(dx) > std::abs(dy) * 1.5f)
            flipPage(dx < 0 ? 1 : -1);
        m_touchHitIndex = -1;
        m_touchEditDragActive = false;
    }
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::handleSystemAction(SysAction a) {
    switch (a) {
        case SysAction::HomeButton: {
            DebugLog::log("[pump] HomeButton -> UI update");
            m_launcher.setAppHasForeground(false);

            std::uint64_t returnFocusId = m_launcher.suspendedTitleId();
            if (returnFocusId == 0 && m_grid) {
                if (auto* focused = m_grid->focusManager().current();
                    focused && focused->tag() == "glossy_icon")
                    returnFocusId = static_cast<GlossyIcon*>(focused)->titleId();
            }
            refreshRecentActivityDuration();
            if (!m_widgetStore.save())
                DebugLog::log("[widgets] recent activity duration could not be saved");
            closeActiveOverlays();
            const bool hasActivityWidget = std::any_of(
                m_widgetStore.all().begin(), m_widgetStore.all().end(),
                [](const switchu::widgets::Widget& widget) {
                    return widget.type == switchu::widgets::WidgetType::RecentlyPlayed
                        || widget.type == switchu::widgets::WidgetType::RecentPlaytime;
                });
            if (hasActivityWidget && m_openFolderId == 0) {
                applyDisplayModel(buildRootFolderModel(), returnFocusId, false);
            } else {
                markSuspendedIcon(m_launcher.suspendedTitleId());
                focusTitle(returnFocusId);
            }
            break;
        }
        default:
            break;
    }
}
#endif

void WiiUMenuApp::updateCursor() {
    if (m_contextMenu && m_contextMenu->isActive()) {
        if (m_cursor) m_cursor->setVisible(false);
        return;
    }
    if (m_grid && m_grid->isTransitioning()) {
        if (m_cursor) m_cursor->setVisible(false); // it would sit at the landing spot
        return;
    }
    if (m_navigator.route() != switchu::navigation::Route::Home ||
        (m_dialog && m_dialog->isActive()) ||
        (m_progressDialog && m_progressDialog->isActive()) ||
        (m_userSelect && m_userSelect->isActive()))
        return;

    auto* cur = focusManager().current();
    if (cur) {
        const bool movingLineFocus = m_grid && m_grid->isDynamicLine()
                                  && cur->tag() == "glossy_icon";
        nxui::Rect fr = movingLineFocus
            ? m_grid->focusedDisplayRect()
            : cur->focusRect();
        if (m_editMode && m_editGhostIcon && m_grid) {
            fr = m_grid->gridSpanRect(m_editTargetIndex,
                                      m_editGhostIcon->gridSpanColumns(),
                                      m_editGhostIcon->gridSpanRows());
        }
        // The app carousel already owns the motion curve; attaching the ring
        // directly avoids a second easing curve that would visibly lag behind.
        const bool carouselScrolling = movingLineFocus && m_grid->isDynamicLineScrolling();
        m_cursor->moveTo(fr.expanded(4.f), carouselScrolling ? 0.f : 0.2f);
        m_cursor->setVisible(true);
    } else {
        m_cursor->setVisible(false);
    }
}
