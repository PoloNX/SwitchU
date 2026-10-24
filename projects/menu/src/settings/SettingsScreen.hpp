#pragma once

#include "TabbedOverlayScreen.hpp"
#include <cstdint>
#include <future>

namespace settings::tabs {
class SystemTab;
class AccessibilityTab;
class AudioTab;
class DisplayTab;
class InternetTab;
class SteamGridDbTab;
class ControllersTab;
class BluetoothTab;
class SleepTab;
class StorageTab;
class AboutTab;
}

class SettingsScreen : public TabbedOverlayScreen {
public:
    SettingsScreen();
    ~SettingsScreen() override = default;

    void onWireframeChange(BoolCb cb)   { m_wireframeCb = std::move(cb); }
    void onGridColumnsChange(IntCb cb)  { m_gridColumnsCb = std::move(cb); }
    void onGridRowsChange(IntCb cb)     { m_gridRowsCb = std::move(cb); }
    void onUiLanguageChange(StringCb cb) { m_uiLanguageCb = std::move(cb); }
    void onClockUse12HourChange(BoolCb cb) { m_clockUse12HourCb = std::move(cb); }
    void onWebClockSyncChange(BoolCb cb) { m_webClockSyncCb = std::move(cb); }
    using ExternalToastCb = std::function<void(const std::string& msg, float holdSeconds)>;
    void onExternalToast(ExternalToastCb cb) { m_externalToastCb = std::move(cb); }
    using ClockSyncBootRetryCb = std::function<void(const std::string& error)>;
    void onClockSyncBootRetry(ClockSyncBootRetryCb cb) {
        m_clockSyncBootRetryCb = std::move(cb);
    }

    void startWebClockSync(bool toastFailureOnly = false);
    void pollClockSync();
    void openSettingDropdown(int rawIdx) { openDropdown(rawIdx); }
    void onAccessibilityEnabledChange(BoolCb cb) { m_accessibilityEnabledCb = std::move(cb); }
    void onAccessibilitySpeakHintsChange(BoolCb cb) { m_accessibilitySpeakHintsCb = std::move(cb); }
    void onAccessibilitySpeakContextEveryFocusChange(BoolCb cb) { m_accessibilitySpeakContextEveryFocusCb = std::move(cb); }
    void onAccessibilitySpeakPositionChange(BoolCb cb) { m_accessibilitySpeakPositionCb = std::move(cb); }
    void onAccessibilitySpeechRateChange(IntCb cb) { m_accessibilitySpeechRateCb = std::move(cb); }
    void onNetConnect(VoidCb cb)        { m_netConnectCb = std::move(cb); }
    void onSteamGridDbEnabledChange(BoolCb cb) { m_steamGridDbEnabledCb = std::move(cb); }
    void onSteamGridDbViewFlagsChange(std::function<void(bool, bool, bool)> cb) {
        m_steamGridDbViewFlagsCb = std::move(cb);
    }
    void onSteamGridDbLogoInLineChange(BoolCb cb) {
        m_steamGridDbLogoInLineCb = std::move(cb);
    }
    void onSteamGridDbApiKeyRequest(VoidCb cb) { m_steamGridDbApiKeyCb = std::move(cb); }
    void onSteamGridDbScrapeRequest(VoidCb cb) { m_steamGridDbScrapeCb = std::move(cb); }
    void onControllerPairing(VoidCb cb) { m_controllerPairingCb = std::move(cb); }
    void onControllerRemapping(VoidCb cb) { m_controllerRemappingCb = std::move(cb); }
    void onControllerTest(VoidCb cb) { m_controllerTestCb = std::move(cb); }
    using SoftwareDeleteCb = std::function<void(uint64_t, const std::string&)>;
    void onSoftwareDelete(SoftwareDeleteCb cb) { m_softwareDeleteCb = std::move(cb); }
    void onSleepRequest(VoidCb cb)      { m_sleepCb = std::move(cb); }
    void onShutdownRequest(VoidCb cb)   { m_shutdownCb = std::move(cb); }
    void onRebootRequest(VoidCb cb)     { m_rebootCb = std::move(cb); }

    void setWireframeState(bool enabled) { m_wireframeEnabled = enabled; }
    void setGridLayoutState(int columns, int rows) {
        m_gridColumns = std::clamp(columns, 1, 8);
        m_gridRows = std::clamp(rows, 1, 5);
    }
    void setUiLanguageOverride(const std::string& tag) {
        m_uiLanguageOverride = tag.empty() ? "auto" : tag;
    }
    void setClockUse12HourState(bool enabled) {
        m_clockUse12Hour = enabled;
    }
    void setWebClockSyncState(bool enabled) {
        m_webClockSyncEnabled = enabled;
    }
    bool webClockSyncEnabled() const { return m_webClockSyncEnabled; }
    void setAccessibilityEnabledState(bool enabled) {
        m_accessibilityEnabled = enabled;
        setAccessibilityVoiceEnabled(enabled);
    }
    void setAccessibilitySpeechState(bool speakHints, bool speakContextEveryFocus,
                                     bool speakPosition, int speechRate) {
        m_accessibilitySpeakHints = speakHints;
        m_accessibilitySpeakContextEveryFocus = speakContextEveryFocus;
        m_accessibilitySpeakPosition = speakPosition;
        m_accessibilitySpeechRate = std::clamp(speechRate, 120, 320);
        setAccessibilitySpeechPreferences(speakHints, speakPosition);
    }
    void setSteamGridDbState(bool enabled, bool hasApiKey,
                             bool showInGrid = true,
                             bool showInDynamicLine = true,
                             bool showInFolders = true,
                             bool showLogoInDynamicLine = true) {
        m_steamGridDbEnabled = enabled;
        m_steamGridDbHasApiKey = hasApiKey;
        m_steamGridDbShowInGrid = showInGrid;
        m_steamGridDbShowInDynamicLine = showInDynamicLine;
        m_steamGridDbShowInFolders = showInFolders;
        m_steamGridDbShowLogoInDynamicLine = showLogoInDynamicLine;
    }
    void setSteamGridDbProgress(bool running, bool finished, int completed, int total,
                                int matched, int failed, const std::string& current,
                                const std::string& message) {
        m_steamGridDbRunning = running;
        m_steamGridDbFinished = finished;
        m_steamGridDbCompleted = completed;
        m_steamGridDbTotal = total;
        m_steamGridDbMatched = matched;
        m_steamGridDbFailed = failed;
        m_steamGridDbCurrent = current;
        m_steamGridDbMessage = message;
    }

protected:
    void buildTabs() override;
    void ensureTabLoaded(int tabIndex) override;
    void onContentUpdate(float dt) override;

private:
    friend class settings::tabs::SystemTab;
    friend class settings::tabs::AccessibilityTab;
    friend class settings::tabs::AudioTab;
    friend class settings::tabs::DisplayTab;
    friend class settings::tabs::InternetTab;
    friend class settings::tabs::SteamGridDbTab;
    friend class settings::tabs::ControllersTab;
    friend class settings::tabs::BluetoothTab;
    friend class settings::tabs::SleepTab;
    friend class settings::tabs::StorageTab;
    friend class settings::tabs::AboutTab;

    BoolCb m_wireframeCb;
    IntCb m_gridColumnsCb;
    IntCb m_gridRowsCb;
    StringCb m_uiLanguageCb;
    BoolCb m_clockUse12HourCb;
    BoolCb m_webClockSyncCb;
    ExternalToastCb m_externalToastCb;
    ClockSyncBootRetryCb m_clockSyncBootRetryCb;
    BoolCb m_accessibilityEnabledCb;
    BoolCb m_accessibilitySpeakHintsCb;
    BoolCb m_accessibilitySpeakContextEveryFocusCb;
    BoolCb m_accessibilitySpeakPositionCb;
    IntCb m_accessibilitySpeechRateCb;
    VoidCb m_netConnectCb;
    BoolCb m_steamGridDbEnabledCb;
    std::function<void(bool, bool, bool)> m_steamGridDbViewFlagsCb;
    BoolCb m_steamGridDbLogoInLineCb;
    VoidCb m_steamGridDbApiKeyCb;
    VoidCb m_steamGridDbScrapeCb;
    VoidCb m_controllerPairingCb;
    VoidCb m_controllerRemappingCb;
    VoidCb m_controllerTestCb;
    SoftwareDeleteCb m_softwareDeleteCb;
    VoidCb m_sleepCb;
    VoidCb m_shutdownCb;
    VoidCb m_rebootCb;

    bool m_wireframeEnabled = false;
    int m_gridColumns = 5;
    int m_gridRows = 3;
    std::string m_uiLanguageOverride = "auto";
    bool m_clockUse12Hour = false;
    bool m_webClockSyncEnabled = false;
    bool m_clockSyncToastFailureOnly = false;
    bool m_accessibilityEnabled = true;
    bool m_accessibilitySpeakHints = true;
    bool m_accessibilitySpeakContextEveryFocus = false;
    bool m_accessibilitySpeakPosition = true;
    int m_accessibilitySpeechRate = 190;
    bool m_steamGridDbEnabled = true;
    bool m_steamGridDbShowInGrid = true;
    bool m_steamGridDbShowInDynamicLine = true;
    bool m_steamGridDbShowInFolders = true;
    bool m_steamGridDbShowLogoInDynamicLine = true;
    bool m_steamGridDbHasApiKey = false;
    bool m_steamGridDbRunning = false;
    bool m_steamGridDbFinished = false;
    int m_steamGridDbCompleted = 0;
    int m_steamGridDbTotal = 0;
    int m_steamGridDbMatched = 0;
    int m_steamGridDbFailed = 0;
    std::string m_steamGridDbCurrent;
    std::string m_steamGridDbMessage;
    std::vector<bool> m_loadedTabs;
    std::vector<bool> m_loadingTabs;
    std::vector<std::future<Tab>> m_tabTasks;
    int m_nextPrefetchTab = 0;

    struct ClockSyncFetch {
        bool ok = false;
        std::uint64_t posixUtc = 0;
        std::string error;
    };
    std::future<ClockSyncFetch> m_clockSyncFuture;

    Tab buildTabNow(int tabIndex);
    Tab makeLoadingTab(int tabIndex) const;
    void startAsyncTabLoad(int tabIndex);
    void pollTabLoaders();
    void prefetchOneTab();
    void emitClockSyncToast(const std::string& msg, float holdSeconds = 2.5f);
};
