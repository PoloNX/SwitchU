#include "WiiUMenuApp.hpp"
#include "widgets/GlossyIcon.hpp"
#include "DebugLog.hpp"

#include <algorithm>
#include <cmath>
#include <nxui/core/I18n.hpp>

bool WiiUMenuApp::isEditableIcon(nxui::Widget* w) const {
    if (!w || w->tag() != "glossy_icon")
        return false;
    auto* icon = static_cast<GlossyIcon*>(w);
    return icon->titleId() != 0;
}

std::string WiiUMenuApp::accessibilityContextFor(nxui::Widget* w) const {
    auto& i18n = nxui::I18n::instance();
    if (!w)
        return {};
    if (m_dialog && m_dialog->isActive() && w == m_dialog.get())
        return i18n.tr("accessibility.context.dialog", "Dialog");
    if (m_userSelect && m_userSelect->isActive() && w == m_userSelect.get())
        return i18n.tr("accessibility.context.profile_selection", "Profile selection");
    if (m_settings && m_settings->isActive() && w == m_settings.get())
        return i18n.tr("accessibility.context.settings", "Settings");
    if (m_themeShop && m_themeShop->isActive() && w == m_themeShop.get())
        return i18n.tr("accessibility.context.themes", "Themes");
    if (w->tag() == "glossy_icon" && m_grid)
        return i18n.tr("accessibility.context.main_menu", "Main menu")
             + ", " + i18n.tr("accessibility.context.page", "page") + " "
             + std::to_string(m_grid->currentPage() + 1)
             + " " + i18n.tr("accessibility.context.of", "of") + " "
             + std::to_string(m_grid->totalPages());
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
        return i18n.tr("accessibility.actions.edit_mode", "Directional pad to choose the new position. Y to place. B to cancel.");
    if (w->tag() == "glossy_icon") {
        auto* icon = static_cast<GlossyIcon*>(w);
        if (icon->titleId() == 0)
            return i18n.tr("accessibility.actions.empty_slot", "Directional pad to navigate. ZL or ZR to change page.");
        return icon->isNotLaunchable()
            ? i18n.tr("accessibility.actions.game_blocked", "A to show the reason. Y to move. ZL or ZR to change page.")
            : i18n.tr("accessibility.actions.game_launchable", "A to launch. X for options. Y to move. ZL or ZR to change page.");
    }
    if (m_settings && w == m_settings.get())
        return i18n.tr("accessibility.actions.settings", "Up and down to choose a category. A or right to enter. B to close.");
    if (m_themeShop && w == m_themeShop.get())
        return i18n.tr("accessibility.actions.themes", "Up and down to navigate. A to choose. B to close.");
    if ((m_dialog && w == m_dialog.get()) || (m_userSelect && w == m_userSelect.get()))
        return i18n.tr("accessibility.actions.dialog", "Left and right to change choice. A to confirm. B to cancel.");
    return {};
}

std::string WiiUMenuApp::accessibilityPositionFor(nxui::Widget* w) const {
    if (!w || !m_config.accessibilitySpeakPosition)
        return {};

    auto& i18n = nxui::I18n::instance();
    if (w->tag() == "glossy_icon" && m_grid) {
        const int global = m_grid->focusedGlobalIndex();
        if (global >= 0) {
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
    ghost->setTexture(sourceIcon->texture());
    ghost->setIsGameCard(sourceIcon->isGameCard());
    ghost->setGameCardTexture(sourceIcon->gameCardTexture());
    ghost->setNotLaunchable(sourceIcon->isNotLaunchable());
    ghost->setCornerRadius(sourceIcon->cornerRadius());
    ghost->setBlurEnabled(false);
    ghost->setPanelOpacity(0.84f);
    ghost->setOpacity(0.84f);
    ghost->setScale(1.06f);
    ghost->forceVisible();

    m_editGhostTargetRect = sourceIcon->focusRect().expanded(4.f);
    ghost->setRect(m_editGhostTargetRect);
    m_editGhostPulse = 0.f;

    m_editGhostIcon = ghost;
}

void WiiUMenuApp::stopEditGhost() {
    m_iconStreamer.clearPinnedIndex();

    if (m_editSourceIcon)
        m_editSourceIcon->setOpacity(1.f);
    m_editSourceIcon = nullptr;

    m_editGhostIcon.reset();
    m_editGhostPulse = 0.f;
}

void WiiUMenuApp::updateEditGhost(float dt) {
    if (!m_editMode || !m_editGhostIcon)
        return;

    if (m_cursor && m_cursor->isVisible()) {
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

    icon->addAction(static_cast<uint64_t>(nxui::Button::A), []() {});
    icon->addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        exitEditMode();
        m_audio.playSfx(Sfx::ModalHide);
    });
}

void WiiUMenuApp::enterEditMode() {
    auto* cur = focusManager().current();
    if (!isEditableIcon(cur))
        return;

    auto* icon = static_cast<GlossyIcon*>(cur);
    m_editMode = true;
    m_editSourceIndex = m_grid ? m_grid->focusedGlobalIndex() : -1;
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
    int target = m_grid->focusedGlobalIndex();
    if (from < 0 || target < 0 || from >= m_model.count() || target >= m_model.count())
        return false;
    if (m_model.at(from).titleId == 0)
        return false;

    int oldPage = m_grid->currentPage();
    bool changed = (from != target);
    if (changed) {
        if (!m_layoutSlots.empty() && from < (int)m_layoutSlots.size() && target < (int)m_layoutSlots.size())
            std::swap(m_layoutSlots[from], m_layoutSlots[target]);

        m_model.swapEntries(from, target);
        m_iconStreamer.swapIndices(from, target);
        m_grid->swapSlots(from, target);
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

bool WiiUMenuApp::moveFocusedIcon(nxui::FocusDirection dir) {
    if (!m_editMode || !m_grid)
        return false;

    int from = m_grid->focusedGlobalIndex();
    if (from < 0 || from >= m_model.count())
        return false;
    if (m_model.at(from).titleId == 0)
        return false;

    int cols = std::max(1, m_grid->columns());
    int rows = std::max(1, m_grid->rowsPerPage());
    int perPage = std::max(1, m_grid->iconsPerPage());
    int totalPages = std::max(1, m_grid->totalPages());

    int page = from / perPage;
    int local = from % perPage;
    int col = local % cols;
    int row = local / cols;

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
            if (col < cols - 1)
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
            if (row + 1 < rows)
                target = from + cols;
            else
                return false;
            break;
    }

    if (target < 0 || target >= m_model.count())
        return false;
    if (target == from)
        return true;

    if (!m_layoutSlots.empty() && from < (int)m_layoutSlots.size() && target < (int)m_layoutSlots.size())
        std::swap(m_layoutSlots[from], m_layoutSlots[target]);

    m_model.swapEntries(from, target);
    m_iconStreamer.swapIndices(from, target);
    m_grid->swapSlots(from, target);
    m_editSourceIndex = target;
    m_iconStreamer.setPinnedIndex(m_editSourceIndex);
    m_grid->focusGlobalIndex(target);

    int newPage = m_grid->currentPage();
    m_iconStreamer.onPageChanged(newPage, m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());
    for (auto* icon : m_grid->pageIcons()) {
        if (icon)
            icon->forceVisible();
    }

    if (auto* cur = m_grid->focusManager().current())
        focusManager().setFocus(cur);

    auto* cur = focusManager().current();
    if (isEditableIcon(cur)) {
        auto* icon = static_cast<GlossyIcon*>(cur);
        bindEditActions(icon);
        m_titlePill->setText(nxui::I18n::instance().tr("game.move_prefix", "Move: ") + icon->title());
    }

    m_layoutDirty = true;
    updateCursor();
    return true;
}

void WiiUMenuApp::wireFocusCallback() {
    focusManager().onFocusChanged([this](nxui::Widget*, nxui::Widget* cur) {
        updateCursor();
        announceFocusedWidget(cur);

        if ((m_dialog && m_dialog->isActive()) ||
            (m_themeShop && m_themeShop->isActive()) ||
            (m_settings && m_settings->isActive()) ||
            (m_userSelect && m_userSelect->isActive()))
            return;

        bool suppressSfx = m_suppressNextNavigateSfx;
        m_suppressNextNavigateSfx = false;
        if (!suppressSfx)
            m_audio.playSfx(Sfx::Navigate);

        if (cur && cur->tag() == "glossy_icon") {
            m_grid->focusManager().setFocus(cur);
            auto* icon = static_cast<GlossyIcon*>(cur);
            auto& i18n = nxui::I18n::instance();
            if (m_editMode) {
                bindEditActions(icon);
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
    if (idx < 0)
        return false;

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
    if (m_editMode)
        exitEditMode();
    if (m_userSelect && m_userSelect->isActive())
        m_userSelect->hide();
    if (m_dialog && m_dialog->isActive())
        m_dialog->hide();
    if (m_settings && m_settings->isActive())
        m_settings->hide();
    if (m_themeShop && m_themeShop->isActive())
        m_themeShop->hide();
}

nxui::Widget* WiiUMenuApp::focusRoot() {
    if (m_launchAnim && m_launchAnim->isPlaying()) return nullptr;
    if (m_dialog && m_dialog->isActive()) return m_dialog.get();
    if (m_themeShop && m_themeShop->isActive()) return m_themeShop.get();
    if (m_settings && m_settings->isActive()) return m_settings.get();
    if (m_userSelect && m_userSelect->isActive()) return m_userSelect.get();
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

    root.addAction(static_cast<uint64_t>(nxui::Button::L), [this]() {
        m_accessibility.repeatLastAnnouncement();
    });

    root.addAction(static_cast<uint64_t>(nxui::Button::ZL), [this]() {
        int p = m_grid->currentPage() - 1;
        if (p >= 0 && !m_grid->isTransitioning()) {
            m_grid->startWaveTransition(p);
            m_audio.playSfx(Sfx::PageChange);
        }
    });
    root.addAction(static_cast<uint64_t>(nxui::Button::ZR), [this]() {
        int p = m_grid->currentPage() + 1;
        if (p < m_grid->totalPages() && !m_grid->isTransitioning()) {
            m_grid->startWaveTransition(p);
            m_audio.playSfx(Sfx::PageChange);
        }
    });
    root.addAction(static_cast<uint64_t>(nxui::Button::Y), [this]() {
        if ((m_dialog && m_dialog->isActive()) ||
            (m_themeShop && m_themeShop->isActive()) ||
            (m_settings && m_settings->isActive()) ||
            (m_userSelect && m_userSelect->isActive())) {
            return;
        }

        if (m_editMode) {
            const std::string movedTitle = m_editHeldTitle;
            bool changed = commitEditModePlacement();
            exitEditMode();
            if (!movedTitle.empty()) {
                auto* focused = focusManager().current();
                std::string summary = nxui::I18n::instance().tr(
                    changed ? "accessibility.move_mode.placed" : "accessibility.move_mode.cancelled",
                    changed ? "Game moved: " : "Move cancelled: ") + movedTitle;
                if (focused) {
                    std::string context = accessibilityContextFor(focused);
                    std::string position = accessibilityPositionFor(focused);
                    if (!position.empty())
                        context = context.empty() ? position : context + ". " + position;
                    if (!context.empty())
                        summary += ". " + context;
                    if (!focused->accessibilitySummary().empty())
                        summary += ". " + focused->accessibilitySummary();
                }
                m_accessibility.announce(summary, true, true);
            }
            m_audio.playSfx(changed ? Sfx::ConfirmPositive : Sfx::ModalHide);
            return;
        }

        auto* cur = focusManager().current();
        if (!isEditableIcon(cur))
            return;

        enterEditMode();
        m_audio.playSfx(Sfx::Activate);
    });
#ifdef SWITCHU_DEBUG_UI
    root.addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        if (handleAccessibilityToggleCombo())
            return;
        m_showDebugOverlay = !m_showDebugOverlay;
        DebugLog::log("[debug] ImGui overlay toggled: %d", m_showDebugOverlay ? 1 : 0);
    });
#else
    root.addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        handleAccessibilityToggleCombo();
    });
#endif
#ifdef SWITCHU_HOMEBREW
    root.addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        if (handleAccessibilityToggleCombo())
            return;
        m_plusExitPending = true;
        m_plusExitPendingTimer = 0.80f;
    });
#else
    root.addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        handleAccessibilityToggleCombo();
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
                    m_launcher.setAppRunning(false);
                    m_launcher.setAppHasForeground(false);
                    m_launcher.setSuspendedTitleId(0);
                    for (auto& ic : m_grid->allIcons())
                        ic->setSuspended(false);
                    if (auto* cur = m_grid->focusManager().current()) {
                        auto* icon = static_cast<GlossyIcon*>(cur);
                        m_titlePill->setText(icon->title());
                    }
                }, true}
            },
            1,
            {}
        );
        focusManager().setFocus(m_dialog.get());
    });
#endif
}

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
        if (!m_grid->focusGlobalIndex(global))
            return nullptr;

        auto* cur = m_grid->focusManager().current();
        if (!cur)
            return nullptr;

        focusManager().setFocus(cur);
        if (m_cursor) {
            m_cursor->moveTo(cur->focusRect().expanded(4.f), 0.f);
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
                    m_editGhostTargetRect = icon->focusRect().expanded(4.f);
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
            bool changed = commitEditModePlacement();
            exitEditMode();
            m_audio.playSfx(changed ? Sfx::ConfirmPositive : Sfx::ModalHide);
            m_touchHitIndex = -1;
            m_touchEditDragActive = false;
            return;
        }

        float dx = input.touchDeltaX();
        float dy = input.touchDeltaY();
        if (std::abs(dx) > kSwipeThreshold && std::abs(dx) > std::abs(dy) * 1.5f) {
            int p = m_grid->currentPage() + (dx < 0 ? 1 : -1);
            if (p >= 0 && p < m_grid->totalPages() && !m_grid->isTransitioning()) {
                m_grid->startWaveTransition(p);
                m_audio.playSfx(Sfx::PageChange);
            }
        }
        m_touchHitIndex = -1;
        m_touchEditDragActive = false;
    }
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::handleSystemAction(SysAction a) {
    switch (a) {
        case SysAction::HomeButton:
            DebugLog::log("[pump] HomeButton -> UI update");
            m_launcher.setAppHasForeground(false);

            markSuspendedIcon(m_launcher.suspendedTitleId());
            closeActiveOverlays();
            focusTitle(m_launcher.suspendedTitleId());
            break;
        default:
            break;
    }
}
#endif

void WiiUMenuApp::updateCursor() {
    if ((m_themeShop && m_themeShop->isActive()) ||
        (m_settings && m_settings->isActive()) ||
        (m_dialog && m_dialog->isActive()) ||
        (m_userSelect && m_userSelect->isActive()))
        return;

    auto* cur = focusManager().current();
    if (cur) {
        nxui::Rect fr = cur->focusRect();
        m_cursor->moveTo(fr.expanded(4.f));
        m_cursor->setVisible(true);
    } else {
        m_cursor->setVisible(false);
    }
}
