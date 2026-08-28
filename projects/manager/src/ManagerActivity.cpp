#include "ManagerActivity.hpp"

#include <nxui/Application.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/widgets/Box.hpp>
#include <switchu/file_log.hpp>
#include <switchu/sd_commit.hpp>
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace switchu::manager {
namespace {

class ManagerBackdrop final : public nxui::Widget {
protected:
    void onRender(nxui::Renderer& renderer) override {
        renderer.drawGradientRect(rect(),
            nxui::Color(0.045f, 0.055f, 0.095f, 1.f),
            nxui::Color(0.085f, 0.12f, 0.20f, 1.f));
        renderer.drawCircle({1110.f, 92.f}, 210.f,
                            nxui::Color(0.18f, 0.48f, 1.f, 0.10f), 64);
        renderer.drawCircle({118.f, 650.f}, 250.f,
                            nxui::Color(0.18f, 0.82f, 0.68f, 0.065f), 64);
    }
};

std::string tr(const char* key, const char* fallback) {
    return nxui::I18n::instance().tr(key, fallback);
}

} // namespace

ManagerActivity::ButtonView ManagerActivity::createButton(
        const nxui::Rect& rect, const std::string& text,
        std::function<void()> action) {
    ButtonView view;
    view.button = std::make_shared<ActionButton>();
    view.button->setRect(rect);
    view.button->setTheme(&m_theme);
    view.button->setCornerRadius(18.f);
    view.button->setFocusable(true);
    view.button->setAccessibilityRole("button");
    view.button->setAccessibilityLabel(text);
    view.button->setOnActivate(std::move(action));

    view.label = std::make_shared<nxui::Label>(text);
    view.label->setFont(&m_bodyFont);
    view.label->setScale(0.86f);
    view.label->setTextColor(m_theme.textPrimary);
    view.label->setHAlign(nxui::Label::HAlign::Center);
    view.label->setVAlign(nxui::Label::VAlign::Center);
    view.label->setSize(std::max(0.f, rect.width - 28.f),
                        std::max(0.f, rect.height - 18.f));
    view.label->setShrink(0.f);
    view.button->addChild(view.label);
    view.button->layout();
    rootBox().addChild(view.button);
    return view;
}

bool ManagerActivity::onCreate() {
    auto& renderer = app().renderer();
    renderer.setBoxWireframeEnabled(false);

    if (!m_titleFont.load(app().gpu(), renderer, "romfs:/fonts/DejaVuSans.ttf", 34)
        || !m_bodyFont.load(app().gpu(), renderer, "romfs:/fonts/DejaVuSans.ttf", 24)
        || !m_smallFont.load(app().gpu(), renderer, "romfs:/fonts/DejaVuSans.ttf", 18)) {
        switchu::FileLog::log("[ui] font loading failed");
        return false;
    }

    m_backdrop = std::make_shared<ManagerBackdrop>();
    m_backdrop->setRect({0.f, 0.f, 1280.f, 720.f});
    rootBox().addChild(m_backdrop);

    m_panel = std::make_shared<nxui::GlassPanel>();
    m_panel->setRect({220.f, 52.f, 840.f, 616.f});
    m_panel->setCornerRadius(32.f);
    m_panel->setBaseColor(m_theme.panelBase.withAlpha(0.96f));
    m_panel->setBorderColor(m_theme.panelBorder.withAlpha(0.52f));
    m_panel->setHighlightColor(m_theme.panelHighlight.withAlpha(0.13f));
    m_panel->setBorderWidth(1.5f);
    m_panel->setLiquidGlassEnabled(true);
    m_panel->setPanelOpacity(0.98f);
    rootBox().addChild(m_panel);

    m_titleLabel = std::make_shared<nxui::Label>(tr("manager.title", "SwitchU Manager"));
    m_titleLabel->setFont(&m_titleFont);
    m_titleLabel->setTextColor(m_theme.textPrimary);
    m_titleLabel->setHAlign(nxui::Label::HAlign::Center);
    m_titleLabel->setVAlign(nxui::Label::VAlign::Center);
    m_titleLabel->setRect({270.f, 84.f, 740.f, 52.f});
    rootBox().addChild(m_titleLabel);

    m_subtitleLabel = std::make_shared<nxui::Label>(
        tr("manager.subtitle", "Manage the HOME menu used on the next boot.")
        + std::string("  •  v") + ReleaseUpdater::kCurrentVersion);
    m_subtitleLabel->setFont(&m_smallFont);
    m_subtitleLabel->setScale(0.86f);
    m_subtitleLabel->setTextColor(m_theme.textSecondary);
    m_subtitleLabel->setHAlign(nxui::Label::HAlign::Center);
    m_subtitleLabel->setVAlign(nxui::Label::VAlign::Center);
    m_subtitleLabel->setRect({300.f, 140.f, 680.f, 34.f});
    rootBox().addChild(m_subtitleLabel);

    m_statusBadge = std::make_shared<nxui::GlassPanel>();
    m_statusBadge->setRect({390.f, 202.f, 500.f, 74.f});
    m_statusBadge->setCornerRadius(24.f);
    m_statusBadge->setBorderWidth(1.4f);
    m_statusBadge->setLiquidGlassEnabled(true);
    rootBox().addChild(m_statusBadge);

    m_statusLabel = std::make_shared<nxui::Label>();
    m_statusLabel->setFont(&m_bodyFont);
    m_statusLabel->setScale(0.96f);
    m_statusLabel->setHAlign(nxui::Label::HAlign::Center);
    m_statusLabel->setVAlign(nxui::Label::VAlign::Center);
    m_statusLabel->setRect({414.f, 214.f, 452.f, 50.f});
    rootBox().addChild(m_statusLabel);

    m_detailLabel = std::make_shared<nxui::Label>();
    m_detailLabel->setFont(&m_smallFont);
    m_detailLabel->setScale(0.82f);
    m_detailLabel->setMultiline(true);
    m_detailLabel->setLineSpacing(1.18f);
    m_detailLabel->setTextColor(m_theme.textSecondary);
    m_detailLabel->setHAlign(nxui::Label::HAlign::Center);
    m_detailLabel->setVAlign(nxui::Label::VAlign::Center);
    m_detailLabel->setRect({310.f, 298.f, 660.f, 90.f});
    rootBox().addChild(m_detailLabel);

    m_noticeLabel = std::make_shared<nxui::Label>();
    m_noticeLabel->setFont(&m_smallFont);
    m_noticeLabel->setScale(0.86f);
    m_noticeLabel->setMultiline(true);
    m_noticeLabel->setTextColor(nxui::Color(1.f, 0.78f, 0.26f, 1.f));
    m_noticeLabel->setHAlign(nxui::Label::HAlign::Center);
    m_noticeLabel->setVAlign(nxui::Label::VAlign::Center);
    m_noticeLabel->setRect({310.f, 392.f, 660.f, 52.f});
    rootBox().addChild(m_noticeLabel);

    m_toggleButton = createButton({440.f, 454.f, 400.f, 58.f}, "", [this]() {
        requestToggle();
    });
    m_updateButton = createButton({440.f, 526.f, 400.f, 58.f}, "", [this]() {
        requestUpdate();
    });
    m_rebootButton = createButton({310.f, 486.f, 318.f, 64.f},
        tr("manager.reboot_now", "Restart now"), [this]() { requestReboot(); });
    m_laterButton = createButton({652.f, 486.f, 318.f, 64.f},
        tr("manager.later", "Later"), [this]() { app().requestExit(); });

    m_helpLabel = std::make_shared<nxui::Label>();
    m_helpLabel->setFont(&m_smallFont);
    m_helpLabel->setScale(0.72f);
    m_helpLabel->setTextColor(m_theme.textSecondary.withAlpha(0.82f));
    m_helpLabel->setHAlign(nxui::Label::HAlign::Center);
    m_helpLabel->setVAlign(nxui::Label::VAlign::Center);
    m_helpLabel->setRect({300.f, 608.f, 680.f, 36.f});
    rootBox().addChild(m_helpLabel);

    m_cursor = std::make_shared<SelectionCursor>();
    m_cursor->setColor(m_theme.cursorNormal);
    m_cursor->setBorderWidth(2.8f);
    rootBox().addChild(m_cursor);

    m_dialog = std::make_shared<OverlayDialog>();
    m_dialog->setFont(&m_bodyFont);
    m_dialog->setSmallFont(&m_smallFont);
    m_dialog->setTheme(&m_theme);
    rootBox().addChild(m_dialog);

    m_updateProgressDialog = std::make_shared<UpdateProgressDialog>();
    m_updateProgressDialog->setFonts(&m_titleFont, &m_smallFont);
    m_updateProgressDialog->setTheme(&m_theme);
    m_updateProgressDialog->setLabels(
        tr("manager.update_download", "Download"),
        tr("manager.update_installation", "Installation"));
    rootBox().addChild(m_updateProgressDialog);

    rootBox().addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (!m_loading && !m_rebooting
            && m_updateState != UpdateUiState::Checking
            && m_updateState != UpdateUiState::Installing)
            app().requestExit();
    });

    focusManager().onFocusChanged([this](nxui::Widget*, nxui::Widget* current) {
        if (current && m_cursor && !(m_dialog && m_dialog->isActive()))
            m_cursor->moveTo(current->focusRect().expanded(4.f), 20.f, 0.13f);
    });

    refreshState();
    refreshPresentation();
    focusFirstAvailable();
    return true;
}

void ManagerActivity::onDestroy() {
    if (m_updateCheckFuture.valid()) m_updateCheckFuture.wait();
    if (m_updateInstallFuture.valid()) m_updateInstallFuture.wait();
    endUpdateInputBlock();
    ReleaseUpdater::shutdownNetwork();
    switchu::FileLog::log("[ui] manager closing restart_required=%d",
                          m_restartRequired ? 1 : 0);
}

void ManagerActivity::refreshState() {
    m_snapshot = m_installation.inspect();
    // There is no SwitchU runtime service to query from a regular homebrew.
    // At launch, the active override is therefore the best persistent source
    // of truth. After a local toggle we retain that launch state until reboot
    // and present the renamed file separately as the next-boot configuration.
    if (!m_restartRequired)
        m_runtimeState = m_snapshot.state;
    switchu::FileLog::log("[state] override=%u menu_payload=%d",
                          static_cast<unsigned>(m_snapshot.state),
                          m_snapshot.menuPayloadPresent ? 1 : 0);
}

void ManagerActivity::setButtonVisible(ButtonView& view, bool visible, bool focusable) {
    view.button->setVisible(visible);
    view.button->setFocusable(visible && focusable);
    view.button->setOpacity(focusable ? 1.f : 0.48f);
}

void ManagerActivity::refreshPresentation() {
    const bool validState = m_snapshot.state == InstallationState::Enabled
        || m_snapshot.state == InstallationState::Disabled;
    const bool runtimeEnabled = m_runtimeState == InstallationState::Enabled;
    const bool configuredEnabled = m_snapshot.state == InstallationState::Enabled;

    if (m_runtimeState == InstallationState::Enabled) {
        m_statusLabel->setText(tr("manager.status_enabled", "SwitchU enabled"));
        m_statusLabel->setTextColor(nxui::Color(0.42f, 1.f, 0.72f, 1.f));
        m_statusBadge->setBaseColor(nxui::Color(0.08f, 0.34f, 0.25f, 0.88f));
        m_statusBadge->setBorderColor(nxui::Color(0.35f, 1.f, 0.68f, 0.46f));
    } else if (m_runtimeState == InstallationState::Disabled) {
        m_statusLabel->setText(tr("manager.status_disabled", "SwitchU disabled"));
        m_statusLabel->setTextColor(m_theme.textPrimary);
        m_statusBadge->setBaseColor(nxui::Color(0.19f, 0.21f, 0.27f, 0.92f));
        m_statusBadge->setBorderColor(m_theme.panelBorder.withAlpha(0.48f));
    } else {
        m_statusLabel->setText(tr("manager.status_unknown", "SwitchU state unavailable"));
        m_statusLabel->setTextColor(nxui::Color(1.f, 0.68f, 0.34f, 1.f));
        m_statusBadge->setBaseColor(nxui::Color(0.34f, 0.19f, 0.08f, 0.88f));
        m_statusBadge->setBorderColor(nxui::Color(1.f, 0.62f, 0.22f, 0.48f));
    }
    m_statusBadge->setHighlightColor(m_theme.panelHighlight.withAlpha(0.10f));

    const bool updaterBusy = m_updateState == UpdateUiState::Checking
        || m_updateState == UpdateUiState::Installing;
    if (m_updateState == UpdateUiState::Checking) {
        m_detailLabel->setText(tr("manager.update_checking",
            "Checking the latest GitHub release..."));
        m_noticeLabel->setText("");
    } else if (m_updateState == UpdateUiState::Installing) {
        m_detailLabel->setText(tr("manager.update_installing",
            "Downloading and installing the SwitchU update..."));
        m_noticeLabel->setText(tr("manager.do_not_close", "Do not close the application."));
    } else if (m_loading) {
        m_detailLabel->setText(tr("manager.applying", "Applying the change safely..."));
        m_noticeLabel->setText(tr("manager.do_not_close", "Do not close the application."));
    } else if (!m_errorMessage.empty()) {
        m_detailLabel->setText(m_errorMessage);
        m_noticeLabel->setText(tr("manager.no_change", "No unrelated data was modified."));
    } else if (m_restartRequired) {
        m_detailLabel->setText(configuredEnabled
            ? tr("manager.next_boot_enabled", "SwitchU will be enabled after the next restart.")
            : tr("manager.next_boot_disabled", "SwitchU will be disabled after the next restart."));
        m_noticeLabel->setText(tr("manager.restart_required", "Restart required"));
    } else if (m_snapshot.state == InstallationState::Conflict) {
        m_detailLabel->setText(tr("manager.conflict",
            "Both enabled and disabled override files exist. Resolve the installation manually."));
        m_noticeLabel->setText(tr("manager.operation_blocked", "Operation blocked for safety"));
    } else if (m_snapshot.state == InstallationState::Missing) {
        m_detailLabel->setText(tr("manager.missing",
            "SwitchU's Atmosphere override was not found on the SD card."));
        m_noticeLabel->setText(tr("manager.operation_blocked", "Operation blocked for safety"));
    } else {
        m_detailLabel->setText(runtimeEnabled
            ? tr("manager.enabled_detail", "The SwitchU HOME menu is configured for this boot.")
            : tr("manager.disabled_detail", "The original Nintendo HOME menu is configured for this boot."));
        m_noticeLabel->setText("");
    }

    if (!m_restartRequired && !m_loading && !updaterBusy) {
        if (m_updateState == UpdateUiState::Available) {
            m_noticeLabel->setText(
                tr("manager.update_available", "A new SwitchU version is available: ")
                + m_latestRelease.version);
        } else if (m_updateState == UpdateUiState::Error && !m_updateError.empty()) {
            m_noticeLabel->setText(tr("manager.update_check_failed",
                "Update check failed. You can retry."));
        }
    }

    const std::string toggleText = configuredEnabled
        ? tr("manager.disable", "Disable SwitchU")
        : tr("manager.enable", "Enable SwitchU");
    m_toggleButton.label->setText(toggleText);
    m_toggleButton.button->setAccessibilityLabel(toggleText);

    std::string updateText;
    switch (m_updateState) {
        case UpdateUiState::Checking:
            updateText = tr("manager.update_checking_button", "Checking for updates...");
            break;
        case UpdateUiState::Available:
            updateText = tr("manager.update_to", "Update to ") + m_latestRelease.version;
            break;
        case UpdateUiState::UpToDate:
            updateText = tr("manager.up_to_date", "Up to date")
                + std::string(" (v") + ReleaseUpdater::kCurrentVersion + ")";
            break;
        case UpdateUiState::Installing: {
            const int percent = std::clamp(
                static_cast<int>(m_updateInstallProgress.load() * 100.f), 0, 100);
            updateText = tr("manager.updating", "Updating...") + std::string(" ")
                + std::to_string(percent) + "%";
            break;
        }
        case UpdateUiState::Error:
            updateText = tr("manager.update_retry", "Retry update check");
            break;
        default:
            updateText = tr("manager.check_updates", "Check for updates");
            break;
    }
    m_updateButton.label->setText(updateText);
    m_updateButton.button->setAccessibilityLabel(updateText);

    setButtonVisible(m_toggleButton, !m_restartRequired,
                     validState && !m_loading && !updaterBusy);
    setButtonVisible(m_updateButton, !m_restartRequired,
                     !m_loading && !updaterBusy);
    setButtonVisible(m_rebootButton, m_restartRequired,
                     !m_loading && !m_rebooting && !updaterBusy);
    setButtonVisible(m_laterButton, m_restartRequired,
                     !m_loading && !m_rebooting && !updaterBusy);

    m_helpLabel->setText(m_restartRequired
        ? tr("manager.help_restart", "A: confirm  •  B: later")
        : tr("manager.help", "D-pad / stick: navigate  •  A: confirm  •  B: quit"));
    if (m_cursor)
        m_cursor->setVisible(!m_loading && !m_rebooting && !updaterBusy);
}

void ManagerActivity::focusFirstAvailable() {
    if (m_restartRequired && m_rebootButton.button->isFocusable())
        focusManager().setFocus(m_rebootButton.button.get());
    else if (m_toggleButton.button->isFocusable())
        focusManager().setFocus(m_toggleButton.button.get());
    else if (m_updateButton.button->isFocusable())
        focusManager().setFocus(m_updateButton.button.get());
}

void ManagerActivity::startUpdateCheck() {
    if (m_loading || m_rebooting || m_restartRequired
        || m_updateState == UpdateUiState::Checking
        || m_updateState == UpdateUiState::Installing)
        return;
    m_updateError.clear();
    m_updateState = UpdateUiState::Checking;
    m_updateCheckFuture = std::async(std::launch::async, []() {
        return ReleaseUpdater::checkLatest();
    });
    refreshPresentation();
}

void ManagerActivity::requestUpdate() {
    if (m_updateState != UpdateUiState::Available) {
        startUpdateCheck();
        return;
    }
    const std::string message =
        tr("manager.update_confirm_message", "Download and install SwitchU ")
        + m_latestRelease.version
        + tr("manager.update_confirm_suffix",
             " from GitHub? Your configuration and themes will be preserved.");
    m_dialog->show(
        tr("manager.update_confirm_title", "Install SwitchU update?"),
        message,
        {
            {tr("manager.cancel", "Cancel"), {}, true},
            {tr("manager.update_install", "Install update"),
             [this]() { startUpdateInstall(); }, true},
        }, 0);
    focusManager().setFocus(m_dialog.get());
}

void ManagerActivity::startUpdateInstall() {
    if (m_updateState != UpdateUiState::Available)
        return;
    const bool preserveDisabled = m_snapshot.state == InstallationState::Disabled;
    const ReleaseInfo release = m_latestRelease;
    m_updateDownloadProgress.store(0.f);
    m_updateInstallProgress.store(0.f);
    m_updateWorkerStage.store(static_cast<int>(UpdateWorkerStage::Idle));
    m_lastDownloadPercent = -1;
    m_lastInstallPercent = -1;
    m_lastUpdateStage = static_cast<int>(UpdateWorkerStage::Idle);
    m_updateState = UpdateUiState::Installing;
    beginUpdateInputBlock();
    m_updateProgressDialog->show(
        tr("manager.update_progress_title", "Installing SwitchU update"));
    m_updateProgressDialog->setProgress(
        0.f, 0.f, tr("manager.update_preparing", "Preparing update..."));
    m_updateInstallFuture = std::async(std::launch::async,
        [this, release, preserveDisabled]() {
            return ReleaseUpdater::install(release, preserveDisabled,
                                           m_updateDownloadProgress,
                                           m_updateInstallProgress,
                                           m_updateWorkerStage);
        });
    refreshPresentation();
}

void ManagerActivity::syncUpdater() {
    using namespace std::chrono_literals;
    if (m_updateState == UpdateUiState::Checking && m_updateCheckFuture.valid()
        && m_updateCheckFuture.wait_for(0s) == std::future_status::ready) {
        try {
            m_latestRelease = m_updateCheckFuture.get();
            m_updateState = m_latestRelease.updateAvailable
                ? UpdateUiState::Available : UpdateUiState::UpToDate;
        } catch (const std::exception& ex) {
            m_updateError = ex.what();
            m_updateState = UpdateUiState::Error;
            switchu::FileLog::log("[updater] check failed: %s", ex.what());
        } catch (...) {
            m_updateError = "Unknown update check error";
            m_updateState = UpdateUiState::Error;
        }
        refreshPresentation();
        focusFirstAvailable();
    }

    if (m_updateState == UpdateUiState::Installing && m_updateInstallFuture.valid()) {
        const int downloadPercent = std::clamp(
            static_cast<int>(m_updateDownloadProgress.load() * 100.f), 0, 100);
        const int installPercent = std::clamp(
            static_cast<int>(m_updateInstallProgress.load() * 100.f), 0, 100);
        const int workerStage = m_updateWorkerStage.load();
        if (downloadPercent != m_lastDownloadPercent
            || installPercent != m_lastInstallPercent
            || workerStage != m_lastUpdateStage) {
            m_lastDownloadPercent = downloadPercent;
            m_lastInstallPercent = installPercent;
            m_lastUpdateStage = workerStage;
            std::string status;
            switch (static_cast<UpdateWorkerStage>(workerStage)) {
                case UpdateWorkerStage::Downloading:
                    status = tr("manager.update_stage_downloading", "Downloading update...");
                    break;
                case UpdateWorkerStage::Verifying:
                    status = tr("manager.update_stage_verifying", "Verifying download...");
                    break;
                case UpdateWorkerStage::Extracting:
                    status = tr("manager.update_stage_extracting", "Preparing installation...");
                    break;
                case UpdateWorkerStage::Installing:
                    status = tr("manager.update_stage_installing", "Installing files...");
                    break;
                default:
                    status = tr("manager.update_preparing", "Preparing update...");
                    break;
            }
            m_updateProgressDialog->setProgress(
                m_updateDownloadProgress.load(), m_updateInstallProgress.load(), status);
            refreshPresentation();
        }
        if (m_updateInstallFuture.wait_for(0s) == std::future_status::ready) {
            const UpdateInstallResult result = m_updateInstallFuture.get();
            m_updateProgressDialog->hide();
            endUpdateInputBlock();
            if (result.success) {
                m_updateState = UpdateUiState::UpToDate;
                m_restartRequired = true;
                m_errorMessage.clear();
                refreshState();
            } else {
                m_updateState = UpdateUiState::Error;
                m_updateError = result.error;
                m_dialog->show(
                    tr("manager.update_error_title", "Update failed"),
                    result.error,
                    {{tr("manager.ok", "OK"), {}, true}});
                focusManager().setFocus(m_dialog.get());
            }
            refreshPresentation();
            focusFirstAvailable();
        }
    }
}

void ManagerActivity::beginUpdateInputBlock() {
    if (!m_exitLockedForUpdate) {
        const Result rc = appletLockExit();
        m_exitLockedForUpdate = R_SUCCEEDED(rc);
        switchu::FileLog::log("[updater] exit lock result=0x%X", rc);
    }
    if (!m_homeBlockedForUpdate) {
        const Result rc = appletBeginBlockingHomeButtonShortAndLongPressed(0);
        m_homeBlockedForUpdate = R_SUCCEEDED(rc);
        switchu::FileLog::log("[updater] HOME block result=0x%X", rc);
    }
}

void ManagerActivity::endUpdateInputBlock() {
    if (m_homeBlockedForUpdate) {
        const Result rc = appletEndBlockingHomeButtonShortAndLongPressed();
        switchu::FileLog::log("[updater] HOME unblock result=0x%X", rc);
        m_homeBlockedForUpdate = false;
    }
    if (m_exitLockedForUpdate) {
        const Result rc = appletUnlockExit();
        switchu::FileLog::log("[updater] exit unlock result=0x%X", rc);
        m_exitLockedForUpdate = false;
    }
}

void ManagerActivity::requestToggle() {
    if (m_loading || m_restartRequired)
        return;
    const bool enable = m_snapshot.state == InstallationState::Disabled;
    const std::string title = enable
        ? tr("manager.confirm_enable_title", "Enable SwitchU?")
        : tr("manager.confirm_disable_title", "Disable SwitchU?");
    const std::string message = tr("manager.confirm_toggle_message",
        "The change will take effect after a restart.");
    m_dialog->show(title, message,
        {
            {tr("manager.cancel", "Cancel"), {}, true},
            {enable ? tr("manager.enable", "Enable SwitchU")
                    : tr("manager.disable", "Disable SwitchU"),
             [this, enable]() {
                 m_pendingTargetEnabled = enable;
                 m_loading = true;
                 m_errorMessage.clear();
                 m_operationDelayFrames = 2;
                 refreshPresentation();
             }, true},
        }, 0);
    focusManager().setFocus(m_dialog.get());
}

void ManagerActivity::performPendingToggle() {
    const ToggleResult result = m_installation.setEnabled(m_pendingTargetEnabled);
    m_loading = false;
    m_snapshot = result.snapshot;
    if (result.success) {
        m_restartRequired = true;
        m_errorMessage.clear();
    } else {
        showError(result.error, result.detail);
    }
    refreshPresentation();
    focusFirstAvailable();
}

std::string ManagerActivity::errorText(ToggleError error, int detail) const {
    std::string message;
    switch (error) {
        case ToggleError::InvalidState:
            message = tr("manager.error_invalid", "The installation state changed. No action was taken.");
            break;
        case ToggleError::MissingMenuPayload:
            message = tr("manager.error_payload", "The SwitchU menu payload is incomplete. Activation was cancelled.");
            break;
        case ToggleError::RenameFailed:
            message = tr("manager.error_rename", "The Atmosphere override could not be renamed.");
            break;
        case ToggleError::VerificationFailed:
            message = tr("manager.error_verify", "The change could not be verified and was rolled back.");
            break;
        case ToggleError::CommitFailed:
            message = tr("manager.error_commit", "The SD card could not be synchronized. The change was rolled back.");
            break;
        case ToggleError::RollbackFailed:
            message = tr("manager.error_rollback", "Rollback failed. Check the SwitchU override files before restarting.");
            break;
        case ToggleError::RebootFailed:
            message = tr("manager.error_reboot", "The console could not be restarted. The configuration change is still saved.");
            break;
        default:
            message = tr("manager.error_unknown", "An unexpected error occurred.");
            break;
    }
    if (detail != 0) {
        char suffix[32]{};
        std::snprintf(suffix, sizeof(suffix), " (0x%X)", static_cast<unsigned>(detail));
        message += suffix;
    }
    return message;
}

void ManagerActivity::showError(ToggleError error, int detail) {
    m_errorMessage = errorText(error, detail);
    switchu::FileLog::log("[ui] operation error=%u detail=0x%X",
                          static_cast<unsigned>(error),
                          static_cast<unsigned>(detail));
    m_dialog->show(tr("manager.error_title", "Operation failed"), m_errorMessage,
                   {{tr("manager.ok", "OK"), {}, true}});
    focusManager().setFocus(m_dialog.get());
}

void ManagerActivity::requestReboot() {
    if (!m_restartRequired || m_loading || m_rebooting)
        return;
    m_dialog->show(
        tr("manager.reboot_confirm_title", "Restart the console?"),
        tr("manager.reboot_confirm_message",
           "Close any other software and save your work before restarting."),
        {
            {tr("manager.cancel", "Cancel"), {}, true},
            {tr("manager.reboot", "Restart"), [this]() {
                 // Let the confirmation dialog close before invoking power
                 // services. This also keeps any fallback error dialog visible.
                 m_rebooting = true;
                 m_rebootDelayFrames = 2;
                 refreshPresentation();
             }, true},
        }, 0);
    focusManager().setFocus(m_dialog.get());
}

void ManagerActivity::rebootNow() {
    switchu::FileLog::log("[power] reboot requested");
    switchu::FileLog::close();
    if (!switchu::commitSdCard("SwitchU Manager reboot")) {
        switchu::FileLog::open("manager");
        m_rebooting = false;
        showError(ToggleError::CommitFailed, 0);
        refreshPresentation();
        return;
    }

    Result result = spsmInitialize();
    if (R_SUCCEEDED(result)) {
        result = spsmShutdown(true);
        spsmExit();
        if (R_SUCCEEDED(result))
            return;
    }
    const Result fallback = appletStartRebootSequence();
    if (R_SUCCEEDED(fallback))
        return;

    switchu::FileLog::open("manager");
    switchu::FileLog::log("[power] reboot failed spsm=0x%X applet=0x%X",
                          result, fallback);
    m_rebooting = false;
    showError(ToggleError::RebootFailed, static_cast<int>(fallback));
    refreshPresentation();
}

void ManagerActivity::onUpdate(float dt) {
    nxui::AnimationManager::instance().update(dt);
    syncUpdater();
    if (m_dialog && m_dialog->isActive())
        m_dialog->handleTouch(app().input());

    if (m_operationDelayFrames > 0) {
        --m_operationDelayFrames;
        if (m_operationDelayFrames == 0)
            performPendingToggle();
    }

    if (m_rebootDelayFrames > 0) {
        --m_rebootDelayFrames;
        if (m_rebootDelayFrames == 0)
            rebootNow();
    }

    nxui::Widget* focused = focusManager().current();
    auto syncButton = [focused](ButtonView& view) {
        if (!view.button)
            return;
        const float emphasis = focused == view.button.get() && view.button->isFocusable()
            ? 1.f : 0.f;
        view.button->setVisualState(view.button->isFocusable() ? 1.f : 0.55f,
                                    emphasis);
    };
    syncButton(m_toggleButton);
    syncButton(m_updateButton);
    syncButton(m_rebootButton);
    syncButton(m_laterButton);

    if (m_cursor && focused && !(m_dialog && m_dialog->isActive()) && focused->isFocusable())
        m_cursor->moveTo(focused->focusRect().expanded(4.f), 20.f, 0.13f);
}

void ManagerActivity::onRender(nxui::Renderer&) {
}

nxui::Widget* ManagerActivity::focusRoot() {
    if (m_loading || m_rebooting || m_updateState == UpdateUiState::Checking
        || m_updateState == UpdateUiState::Installing)
        return nullptr;
    if (m_dialog && m_dialog->isActive())
        return m_dialog.get();
    return &rootBox();
}

} // namespace switchu::manager
