#include "WiiUMenuApp.hpp"
#include "widgets/GlossyIcon.hpp"
#include "DebugLog.hpp"

#include <cmath>

bool WiiUMenuApp::isEditableIcon(nxui::Widget* w) const {
    if (!w || w->tag() != "glossy_icon")
        return false;
    auto* icon = static_cast<GlossyIcon*>(w);
    return icon->titleId() != 0;
}

void WiiUMenuApp::startEditGhost(GlossyIcon* sourceIcon) {
    stopEditGhost();
    if (!sourceIcon)
        return;

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
    m_titlePill->setText(std::string("Move: ") + m_editHeldTitle);
    m_titlePill->setVisible(true);
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
        m_titlePill->setVisible(false);
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
        m_layoutDirty = true;
    }

    m_grid->focusGlobalIndex(target);

    int newPage = m_grid->currentPage();
    if (changed || newPage != oldPage) {
        m_iconStreamer.onPageChanged(newPage, m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
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

    int oldPage = m_grid->currentPage();

    if (!m_layoutSlots.empty() && from < (int)m_layoutSlots.size() && target < (int)m_layoutSlots.size())
        std::swap(m_layoutSlots[from], m_layoutSlots[target]);

    m_model.swapEntries(from, target);
    m_iconStreamer.swapIndices(from, target);
    m_grid->swapSlots(from, target);
    m_grid->focusGlobalIndex(target);

    int newPage = m_grid->currentPage();
    if (newPage != oldPage) {
        m_iconStreamer.onPageChanged(newPage, m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }

    if (auto* cur = m_grid->focusManager().current())
        focusManager().setFocus(cur);

    auto* cur = focusManager().current();
    if (isEditableIcon(cur)) {
        auto* icon = static_cast<GlossyIcon*>(cur);
        bindEditActions(icon);
        m_titlePill->setText(std::string("Move: ") + icon->title());
    }

    m_layoutDirty = true;
    updateCursor();
    return true;
}

void WiiUMenuApp::wireFocusCallback() {
    focusManager().onFocusChanged([this](nxui::Widget*, nxui::Widget* cur) {
        updateCursor();

        if ((m_dialog && m_dialog->isActive()) ||
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
            if (m_editMode) {
                bindEditActions(icon);
                m_editGhostTargetRect = icon->focusRect();
                if (!m_editHeldTitle.empty())
                    m_titlePill->setText(std::string("Move: ") + m_editHeldTitle);
                else if (icon->titleId() != 0)
                    m_titlePill->setText(std::string("Move: ") + icon->title());
                else
                    m_titlePill->setText("Move");
                m_titlePill->setVisible(true);
                return;
            }
            if (icon->titleId() == 0) {
                m_titlePill->setVisible(false);
                return;
            }
#ifndef SWITCHU_HOMEBREW
            if (m_launcher.isAppSuspended(icon->titleId()))
                m_titlePill->setText(std::string("\xe2\x96\xb6  ") + icon->title());
            else
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
            m_titlePill->setVisible(false);
        } else {
            m_titlePill->setVisible(false);
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
    if (m_settings && m_settings.get() == w) return w->isFocusable();
    for (const auto& btn : m_sidebar.leftButtons())
        if (btn.get() == w) return w->isFocusable();
    for (const auto& btn : m_sidebar.rightButtons())
        if (btn.get() == w) return w->isFocusable();
    if (m_grid)
        for (const auto& icon : m_grid->allIcons())
            if (icon.get() == w) return w->isFocusable();
    return false;
}

nxui::Widget* WiiUMenuApp::focusRoot() {
    if (m_suspended) return nullptr;
    if (m_launchAnim && m_launchAnim->isPlaying()) return nullptr;
    if (m_dialog && m_dialog->isActive()) return m_dialog.get();
    if (m_settings && m_settings->isActive()) return m_settings.get();
    if (m_userSelect && m_userSelect->isActive()) return m_userSelect.get();
    return &rootBox();
}

void WiiUMenuApp::wireGlobalActions() {
    auto& root = rootBox();

    root.addAction(static_cast<uint64_t>(nxui::Button::L), [this]() {
        int p = m_grid->currentPage() - 1;
        if (p >= 0 && !m_grid->isTransitioning()) {
            m_grid->startWaveTransition(p);
            m_audio.playSfx(Sfx::PageChange);
        }
    });
    root.addAction(static_cast<uint64_t>(nxui::Button::R), [this]() {
        int p = m_grid->currentPage() + 1;
        if (p < m_grid->totalPages() && !m_grid->isTransitioning()) {
            m_grid->startWaveTransition(p);
            m_audio.playSfx(Sfx::PageChange);
        }
    });
    root.addAction(static_cast<uint64_t>(nxui::Button::Y), [this]() {
        if ((m_dialog && m_dialog->isActive()) ||
            (m_settings && m_settings->isActive()) ||
            (m_userSelect && m_userSelect->isActive())) {
            return;
        }

        if (m_editMode) {
            bool changed = commitEditModePlacement();
            exitEditMode();
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
        m_showDebugOverlay = !m_showDebugOverlay;
        DebugLog::log("[debug] ImGui overlay toggled: %d", m_showDebugOverlay ? 1 : 0);
    });
#endif
#ifdef SWITCHU_HOMEBREW
    root.addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        m_audio.playSfx(Sfx::ModalHide);
        app().requestExit();
    });
#endif

#ifndef SWITCHU_HOMEBREW
    root.addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (m_editMode) return;
        if (m_launcher.suspendedTitleId() == 0) return;
        auto* cur = focusManager().current();
        if (!cur || cur->tag() != "glossy_icon") return;
        auto* icon = static_cast<GlossyIcon*>(cur);
        if (!m_launcher.isAppSuspended(icon->titleId())) return;

        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = cur;
        m_dialog->show(
            "Close game",
            "Close " + icon->title() + "?\nUnsaved progress will be lost.",
            {
                {"Cancel", [this]() {}, true},
                {"Close",  [this]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
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
            0,
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

#ifndef SWITCHU_HOMEBREW
void WiiUMenuApp::handleSystemAction(SysAction a) {
    switch (a) {
        case SysAction::HomeButton:
            DebugLog::log("[pump] HomeButton -> UI update");
            m_launcher.setAppHasForeground(false);
            m_showLoadingScreen = false;

            for (auto& ic : m_grid->allIcons())
                ic->setSuspended(m_launcher.suspendedTitleId() != 0 &&
                                 ic->titleId() == m_launcher.suspendedTitleId());
            if (auto* cur = m_grid->focusManager().current()) {
                auto* icon = static_cast<GlossyIcon*>(cur);
                if (m_launcher.isAppSuspended(icon->titleId()))
                    m_titlePill->setText(std::string("\xe2\x96\xb6  ") + icon->title());
                else
                    m_titlePill->setText(icon->title());
            }
            break;
        default:
            break;
    }
}
#endif

void WiiUMenuApp::updateCursor() {
    if ((m_settings && m_settings->isActive()) ||
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