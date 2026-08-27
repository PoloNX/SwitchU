#pragma once

#include "SwitchUInstallation.hpp"
#include "ReleaseUpdater.hpp"
#include "widgets/ActionButton.hpp"
#include "widgets/OverlayDialog.hpp"
#include "widgets/SelectionCursor.hpp"

#include <nxui/Activity.hpp>
#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/widgets/Label.hpp>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace switchu::manager {

class ManagerActivity final : public nxui::Activity {
public:
    bool onCreate() override;
    void onDestroy() override;
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;
    nxui::Widget* focusRoot() override;

private:
    struct ButtonView {
        std::shared_ptr<ActionButton> button;
        std::shared_ptr<nxui::Label> label;
    };

    ButtonView createButton(const nxui::Rect& rect, const std::string& text,
                            std::function<void()> action);
    void refreshState();
    void refreshPresentation();
    void requestToggle();
    void performPendingToggle();
    void requestReboot();
    void rebootNow();
    void requestUpdate();
    void startUpdateCheck();
    void startUpdateInstall();
    void syncUpdater();
    void showError(ToggleError error, int detail);
    std::string errorText(ToggleError error, int detail) const;
    void setButtonVisible(ButtonView& view, bool visible, bool focusable);
    void focusFirstAvailable();

    nxui::Theme m_theme = nxui::Theme::dark();
    nxui::Font m_titleFont;
    nxui::Font m_bodyFont;
    nxui::Font m_smallFont;
    SwitchUInstallation m_installation;
    InstallationSnapshot m_snapshot;
    InstallationState m_runtimeState = InstallationState::Missing;

    std::shared_ptr<nxui::Widget> m_backdrop;
    std::shared_ptr<nxui::GlassPanel> m_panel;
    std::shared_ptr<nxui::GlassPanel> m_statusBadge;
    std::shared_ptr<nxui::Label> m_titleLabel;
    std::shared_ptr<nxui::Label> m_subtitleLabel;
    std::shared_ptr<nxui::Label> m_statusLabel;
    std::shared_ptr<nxui::Label> m_detailLabel;
    std::shared_ptr<nxui::Label> m_noticeLabel;
    std::shared_ptr<nxui::Label> m_helpLabel;
    ButtonView m_toggleButton;
    ButtonView m_updateButton;
    ButtonView m_rebootButton;
    ButtonView m_laterButton;
    std::shared_ptr<OverlayDialog> m_dialog;
    std::shared_ptr<SelectionCursor> m_cursor;

    bool m_restartRequired = false;
    bool m_loading = false;
    bool m_rebooting = false;
    bool m_pendingTargetEnabled = false;
    int m_operationDelayFrames = 0;
    int m_rebootDelayFrames = 0;
    std::string m_errorMessage;

    enum class UpdateUiState {
        Idle,
        Checking,
        Available,
        UpToDate,
        Installing,
        Error,
    };
    UpdateUiState m_updateState = UpdateUiState::Idle;
    ReleaseInfo m_latestRelease;
    std::future<ReleaseInfo> m_updateCheckFuture;
    std::future<UpdateInstallResult> m_updateInstallFuture;
    std::atomic<float> m_updateProgress{0.f};
    std::atomic<int> m_updateWorkerStage{static_cast<int>(UpdateWorkerStage::Idle)};
    int m_lastUpdatePercent = -1;
    std::string m_updateError;
};

} // namespace switchu::manager
