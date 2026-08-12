#include "WiiUMenuApp.hpp"
#include "themeshop/ThemePackageInstaller.hpp"
#include "widgets/GlossyIcon.hpp"
#include "DebugLog.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace {

bool removeDirectoryRecursive(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
}

bool pathExists(const std::string& path) {
    if (path.empty())
        return false;

    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty())
        return name;
    if (name.empty())
        return base;
    if (base.back() == '/')
        return base + name;
    return base + "/" + name;
}

bool isAbsoluteThemePath(const std::string& path) {
    return (!path.empty() && path.front() == '/') || (path.find(":/") != std::string::npos);
}

std::string trimSlashes(std::string path) {
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    return path;
}

bool readJsonString(const nlohmann::json& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        return false;
    out = it->get<std::string>();
    return !out.empty();
}

std::string installedThemePreviewPath(const ThemePreset& preset) {
    if (preset.installPath.empty())
        return {};

    auto resolveLocal = [&](const std::string& raw) -> std::string {
        if (raw.empty() || raw.rfind("http://", 0) == 0 || raw.rfind("https://", 0) == 0)
            return {};
        std::string path = isAbsoluteThemePath(raw) ? raw : joinPath(preset.installPath, trimSlashes(raw));
        return pathExists(path) ? path : std::string();
    };

    nlohmann::json manifest;
    std::ifstream input(joinPath(preset.installPath, "theme.json"));
    if (input) {
        try {
            input >> manifest;
        } catch (...) {
            manifest = nlohmann::json{};
        }
    }

    if (manifest.is_object()) {
        std::string raw;
        if ((readJsonString(manifest, "cover", raw)
             || readJsonString(manifest, "screenshot", raw)
             || readJsonString(manifest, "thumbnail", raw))) {
            std::string path = resolveLocal(raw);
            if (!path.empty())
                return path;
        }

        auto previewIt = manifest.find("preview");
        if (previewIt != manifest.end() && previewIt->is_object()) {
            raw.clear();
            if ((readJsonString(*previewIt, "cover", raw)
                 || readJsonString(*previewIt, "screenshot", raw)
                 || readJsonString(*previewIt, "thumbnail", raw))) {
                std::string path = resolveLocal(raw);
                if (!path.empty())
                    return path;
            }

            auto screenshotsIt = previewIt->find("screenshots");
            if (screenshotsIt != previewIt->end() && screenshotsIt->is_array()) {
                for (const auto& value : *screenshotsIt) {
                    if (!value.is_string())
                        continue;
                    std::string path = resolveLocal(value.get<std::string>());
                    if (!path.empty())
                        return path;
                }
            }
        }

        auto screenshotsIt = manifest.find("screenshots");
        if (screenshotsIt != manifest.end() && screenshotsIt->is_array()) {
            for (const auto& value : *screenshotsIt) {
                if (!value.is_string())
                    continue;
                std::string path = resolveLocal(value.get<std::string>());
                if (!path.empty())
                    return path;
            }
        }
    }

    static constexpr const char* kFallbacks[] = {
        "preview/cover.png",
        "preview/screenshot.png",
        "preview/cover.jpg",
        "preview/screenshot.jpg",
        "screenshots/cover.png",
        "screenshots/screenshot.png",
        "cover.png",
        "screenshot.png",
    };
    for (const char* fallback : kFallbacks) {
        std::string path = joinPath(preset.installPath, fallback);
        if (pathExists(path))
            return path;
    }

    return {};
}

const char* safeLogPath(const std::string& path) {
    return path.empty() ? "<empty>" : path.c_str();
}

const char* themeSourceName(ThemePresetSource source) {
    switch (source) {
        case ThemePresetSource::BuiltIn: return "builtin";
        case ThemePresetSource::UserPreset: return "user";
        case ThemePresetSource::InstalledPackage: return "package";
        default: return "unknown";
    }
}

WaraWaraBackground::Layout toBackgroundLayout(ThemeBackgroundLayout layout) {
    switch (layout) {
        case ThemeBackgroundLayout::Grid:
            return WaraWaraBackground::Layout::Grid;
        case ThemeBackgroundLayout::Floating:
        default:
            return WaraWaraBackground::Layout::Floating;
    }
}

WaraWaraBackground::ShapeSet toBackgroundShapeSet(ThemeBackgroundShapeSet shapeSet) {
    switch (shapeSet) {
        case ThemeBackgroundShapeSet::Circle:
            return WaraWaraBackground::ShapeSet::Circle;
        case ThemeBackgroundShapeSet::Triangle:
            return WaraWaraBackground::ShapeSet::Triangle;
        case ThemeBackgroundShapeSet::Square:
            return WaraWaraBackground::ShapeSet::Square;
        case ThemeBackgroundShapeSet::Diamond:
            return WaraWaraBackground::ShapeSet::Diamond;
        case ThemeBackgroundShapeSet::Hexagon:
            return WaraWaraBackground::ShapeSet::Hexagon;
        case ThemeBackgroundShapeSet::Mixed:
        default:
            return WaraWaraBackground::ShapeSet::Mixed;
    }
}

WaraWaraBackground::Symmetry toBackgroundSymmetry(ThemeBackgroundSymmetry symmetry) {
    switch (symmetry) {
        case ThemeBackgroundSymmetry::MirrorX:
            return WaraWaraBackground::Symmetry::MirrorHorizontal;
        case ThemeBackgroundSymmetry::MirrorY:
            return WaraWaraBackground::Symmetry::MirrorVertical;
        case ThemeBackgroundSymmetry::Quad:
            return WaraWaraBackground::Symmetry::Quad;
        case ThemeBackgroundSymmetry::None:
        default:
            return WaraWaraBackground::Symmetry::None;
    }
}

std::string sourceLabel(ThemePresetSource source) {
    auto& i18n = nxui::I18n::instance();
    switch (source) {
        case ThemePresetSource::BuiltIn:
            return i18n.tr("themeshop.source.builtin", "Built-in");
        case ThemePresetSource::UserPreset:
            return i18n.tr("themeshop.source.custom", "Custom");
        case ThemePresetSource::InstalledPackage:
            return i18n.tr("themeshop.source.community", "Community");
    }

    return i18n.tr("themeshop.source.unknown", "Unknown");
}

std::string defaultThemeRef() {
    return "builtin:Default Light";
}

} // namespace

void WiiUMenuApp::createSettings() {
    if (m_settings) return;

    m_settings = std::make_shared<SettingsScreen>();
    if (m_overlayLayer) {
        m_overlayLayer->addChild(m_settings);
    }
    m_settings->setFont(&m_fontNormal);
    m_settings->setSmallFont(&m_fontSmall);
    m_settings->setTheme(&m_theme);
    m_settings->setWireframeState(m_showWireframe);
    m_settings->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_settings->setUiLanguageOverride(m_config.uiLanguageOverride);
    m_settings->setDefaultProfileState(m_config.defaultProfileEnabled,
                                       m_config.defaultProfileUid);
    m_settings->setClockUse12HourState(m_config.clockUse12Hour);
    m_settings->setAccessibilityEnabledState(m_config.accessibilityEnabled);
    m_settings->setAccessibilitySpeechState(m_config.accessibilitySpeakHints,
                                            m_config.accessibilitySpeakContextEveryFocus,
                                            m_config.accessibilitySpeakPosition,
                                            m_config.accessibilitySpeechRate);
    m_settings->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                  m_config.accessibilitySpeakPosition);

    m_settings->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_settings->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_settings->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_settings->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_settings->onAccessibilityStructuredAnnouncement([this](const std::string& context,
                                                             const std::string& position,
                                                             const std::string& summary,
                                                             bool forceRepeat,
                                                             bool forceContext) {
        m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
    });
    m_settings->onToggleSfx([this](bool on) {
        m_audio.playSfx(on ? Sfx::ThemeToggle : Sfx::ToggleOff);
    });
    m_settings->onSliderSfx([this](bool up) {
        m_audio.playSfx(up ? Sfx::SliderUp : Sfx::SliderDown);
    });
    m_settings->onWireframeChange([this](bool enabled) {
        m_showWireframe = enabled;
        app().renderer().setBoxWireframeEnabled(enabled);
    });
    m_settings->onGridColumnsChange([this](int cols) {
        cols = std::clamp(cols, 3, 8);
        if (m_config.gridColumns == cols)
            return;
        m_config.gridColumns = cols;
        reflowHomeGrid();
    });
    m_settings->onGridRowsChange([this](int rows) {
        rows = std::clamp(rows, 2, 5);
        if (m_config.gridRows == rows)
            return;
        m_config.gridRows = rows;
        reflowHomeGrid();
    });
    m_settings->onUiLanguageChange([this](const std::string& tag) {
        m_config.uiLanguageOverride = tag;
        if (m_settings) m_settings->setUiLanguageOverride(tag);
        applyUiLanguage();
        app().gpu().waitIdle();
        m_fontNormal.clearCache();
        m_fontSmall.clearCache();
        m_settingsNeedRefresh = true;
    });
    m_settings->onDefaultProfileChange([this](const std::string& uidHex) {
        m_config.defaultProfileEnabled = !uidHex.empty();
        m_config.defaultProfileUid = uidHex;
        if (m_settings)
            m_settings->setDefaultProfileState(m_config.defaultProfileEnabled,
                                               m_config.defaultProfileUid);
    });
    m_settings->onClockUse12HourChange([this](bool enabled) {
        if (m_config.clockUse12Hour == enabled)
            return;
        m_config.clockUse12Hour = enabled;
        if (m_clock)
            m_clock->setUse12HourClock(enabled);
    });
    m_settings->onAccessibilityEnabledChange([this](bool enabled) {
        if (m_config.accessibilityEnabled == enabled)
            return;
        m_config.accessibilityEnabled = enabled;
        if (m_settings)
            m_settings->setAccessibilityVoiceEnabled(enabled);
        if (m_themeShop)
            m_themeShop->setAccessibilityVoiceEnabled(enabled);
        if (m_gameOptions)
            m_gameOptions->setAccessibilityVoiceEnabled(enabled);
        if (m_folderOptions)
            m_folderOptions->setAccessibilityVoiceEnabled(enabled);
        if (enabled) {
            m_accessibility.setEnabled(true);
            // Startup skips espeak init when voice guidance is off, so bring it
            // up on first enable.
            if (!m_accessibilityReady)
                m_accessibilityReady = m_accessibility.initializeConfigured(
                    SD_ASSETS, nxui::I18n::instance().activeLanguageTag(),
                    m_config.accessibilitySpeechRate);
            m_accessibility.announce(nxui::I18n::instance().tr(
                "accessibility.speech.enabled",
                "Voice guidance enabled."));
        } else {
            m_accessibility.announceAndDisable(nxui::I18n::instance().tr(
                "accessibility.speech.disabled",
                "Voice guidance disabled."));
        }
    });
    m_settings->onAccessibilitySpeakHintsChange([this](bool enabled) {
        m_config.accessibilitySpeakHints = enabled;
        m_accessibility.setSpeakHints(enabled);
        if (m_settings)
            m_settings->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                          m_config.accessibilitySpeakPosition);
        if (m_themeShop)
            m_themeShop->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                           m_config.accessibilitySpeakPosition);
        if (m_gameOptions)
            m_gameOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                              m_config.accessibilitySpeakPosition);
        if (m_folderOptions)
            m_folderOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                                m_config.accessibilitySpeakPosition);
        if (m_dialog)
            m_dialog->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                        m_config.accessibilitySpeakPosition);
        if (m_userSelect)
            m_userSelect->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                            m_config.accessibilitySpeakPosition);
    });
    m_settings->onAccessibilitySpeakContextEveryFocusChange([this](bool enabled) {
        m_config.accessibilitySpeakContextEveryFocus = enabled;
        m_accessibility.setSpeakContextEveryFocus(enabled);
    });
    m_settings->onAccessibilitySpeakPositionChange([this](bool enabled) {
        m_config.accessibilitySpeakPosition = enabled;
        if (m_settings)
            m_settings->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                          m_config.accessibilitySpeakPosition);
        if (m_themeShop)
            m_themeShop->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                           m_config.accessibilitySpeakPosition);
        if (m_gameOptions)
            m_gameOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                              m_config.accessibilitySpeakPosition);
        if (m_folderOptions)
            m_folderOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                                m_config.accessibilitySpeakPosition);
        if (m_dialog)
            m_dialog->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                        m_config.accessibilitySpeakPosition);
        if (m_userSelect)
            m_userSelect->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                            m_config.accessibilitySpeakPosition);
    });
    m_settings->onAccessibilitySpeechRateChange([this](int rate) {
        m_config.accessibilitySpeechRate = std::clamp(rate, 120, 320);
        m_accessibility.setSpeechRate(m_config.accessibilitySpeechRate);
    });
    m_settings->onNetConnect([this]() {
        m_pendingNetConnect = true;
        m_settings->hide();
    });
    m_settings->onControllerPairing([this]() {
        if (m_settings) m_settings->hide();
        m_launcher.launchControllerPairing();
    });
    m_settings->onControllerRemapping([this]() {
        if (m_settings) m_settings->hide();
        m_launcher.launchControllerRemapping();
    });
    m_settings->onControllerTest([this]() {
        if (!m_controllerTest) return;
        m_navigator.navigate(switchu::navigation::Route::ControllerTest);
        m_controllerTest->show();
        focusManager().setFocus(m_controllerTest.get());
        m_audio.playSfx(Sfx::ModalShow);
    });
    m_settings->onSoftwareDelete([this](uint64_t titleId, const std::string& title) {
        startSoftwareDeletion(titleId, title, false);
    });
    m_settings->onSleepRequest([this]() {
        if (m_settings) m_settings->hide();
        m_launcher.enterSleep();
    });
    m_settings->onShutdownRequest([this]() {
        if (m_settings) m_settings->hide();
        m_launcher.shutdown();
    });
    m_settings->onRebootRequest([this]() {
        if (m_settings) m_settings->hide();
        m_launcher.reboot();
    });
    m_settings->onDialogRequest([this](const std::string& title,
                                       const std::string& msg,
                                       std::vector<SettingsScreen::DialogButtonDef> buttons) {
        if (!m_dialog) return;
        std::vector<OverlayDialog::ButtonDef> dlgButtons;
        bool preserveReturnFocus = (m_dialog && m_dialog->isActive() && focusManager().current() == m_dialog.get() && m_dialogReturnFocus != nullptr);
        for (size_t i = 0; i < buttons.size(); ++i) {
            auto cb = buttons[i].onPress;
            bool isLast = (i == buttons.size() - 1);
            if (buttons.size() == 1) {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            } else if (isLast) {
                dlgButtons.push_back({buttons[i].label, [cb]() { if (cb) cb(); }, true});
            } else {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            }
        }
        if (!preserveReturnFocus)
            m_dialogReturnFocus = focusManager().current();
        m_dialog->show(title, msg, std::move(dlgButtons));
        focusManager().setFocus(m_dialog.get());
    });
    m_settings->onClosed([this]() {
        m_navigator.routeDidClose(switchu::navigation::Route::Settings);
        if (m_configSaveFuture.valid())
            m_configSaveFuture.wait();
        m_configSaveFuture = m_threadPool.submit([cfg = m_config]() {
            cfg.save();
        });
        DebugLog::log("[config] save queued");
        if (isCurrentFocusableWidget(m_sidebar.settingsButton())) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_sidebar.settingsButton());
        }
    });

}

void WiiUMenuApp::createGameOptions() {
    if (m_gameOptions) return;
    m_gameOptions = std::make_shared<GameOptionsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_gameOptions);
    m_gameOptions->setFont(&m_fontNormal);
    m_gameOptions->setSmallFont(&m_fontSmall);
    m_gameOptions->setTheme(&m_theme);
    m_gameOptions->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_gameOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                     m_config.accessibilitySpeakPosition);
    m_gameOptions->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_gameOptions->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_gameOptions->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_gameOptions->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_gameOptions->onAccessibilityStructuredAnnouncement(
        [this](const std::string& context, const std::string& position,
               const std::string& summary, bool forceRepeat, bool forceContext) {
            m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
        });
    m_gameOptions->onClosed([this]() {
        m_navigator.routeDidClose(switchu::navigation::Route::GameOptions);
        if (focusTitle(m_gameOptionsTitleId)) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
    });
}

void WiiUMenuApp::createFolderOptions() {
    if (m_folderOptions) return;
    m_folderOptions = std::make_shared<FolderOptionsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_folderOptions);
    m_folderOptions->setFont(&m_fontNormal);
    m_folderOptions->setSmallFont(&m_fontSmall);
    m_folderOptions->setTheme(&m_theme);
    m_folderOptions->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_folderOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                       m_config.accessibilitySpeakPosition);
    m_folderOptions->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_folderOptions->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_folderOptions->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_folderOptions->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_folderOptions->onAccessibilityStructuredAnnouncement(
        [this](const std::string& context, const std::string& position,
               const std::string& summary, bool forceRepeat, bool forceContext) {
            m_accessibility.announceStructuredFocus(context, position, summary,
                                                    forceRepeat, forceContext);
        });
    m_folderOptions->onClosed([this]() {
        m_navigator.routeDidClose(switchu::navigation::Route::FolderOptions);
        if (focusTitle(folderTitleId(m_folderOptionsId))) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
    });
}

void WiiUMenuApp::createControllerTest() {
    if (m_controllerTest) return;
    m_controllerTest = std::make_shared<ControllerTestScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_controllerTest);
    m_controllerTest->setFont(&m_fontNormal);
    m_controllerTest->setSmallFont(&m_fontSmall);
    m_controllerTest->setTheme(&m_theme);
    m_controllerTest->setInput(&app().input());
    m_controllerTest->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_controllerTest->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_controllerTest->onClosed([this]() {
        if (m_navigator.route() == switchu::navigation::Route::ControllerTest) {
            if (m_settings && m_settings->isActive())
                m_navigator.navigate(switchu::navigation::Route::Settings);
            else
                m_navigator.resetToHome();
        }
        if (m_settings && m_settings->isActive())
            focusManager().setFocus(m_settings.get());
    });
}

void WiiUMenuApp::createThemeShop() {
    if (m_themeShop) return;

    auto showThemeShopInfo = [this](const std::string& title, const std::string& message) {
        if (!m_dialog) return;
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(title, message, {{"OK", {}, true}});
        focusManager().setFocus(m_dialog.get());
    };
    auto queueThemeTransfer = [this, showThemeShopInfo](const std::string& themeId, bool applyAfterInstall) {
        if (!m_themeShop)
            return;

        const auto* entry = m_themeShop->findCommunityThemeEntry(themeId);
        if (!entry) {
            auto& i18n = nxui::I18n::instance();
            showThemeShopInfo(i18n.tr("sidebar.theme_shop", "Theme Shop"),
                              i18n.tr("themeshop.community.selected_missing",
                                      "The selected community theme is no longer available in the catalog."));
            return;
        }

        ThemeCatalogClient::Entry entryCopy = *entry;
        ThemePackageInstaller::Mode mode = applyAfterInstall
            ? ThemePackageInstaller::Mode::InstallAndApply
            : ThemePackageInstaller::Mode::InstallOnly;
        std::string destination = ThemePackageInstaller::destinationRootFor(entryCopy.id, mode);

        auto startTransfer = [this, entryCopy, applyAfterInstall]() {
            startThemePackageTransfer(entryCopy, applyAfterInstall);
        };

        if (!pathExists(destination)) {
            startTransfer();
            return;
        }

        auto& i18n = nxui::I18n::instance();
        if (!m_dialog) {
            startTransfer();
            return;
        }

        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            applyAfterInstall
                ? i18n.tr("themeshop.community.overwrite_apply_title", "Replace And Apply Theme")
                : i18n.tr("themeshop.community.overwrite_install_title", "Replace Installed Theme"),
            applyAfterInstall
                ? i18n.tr("themeshop.community.overwrite_apply_message",
                          "A package with this theme ID is already installed. Replace it with the GitHub version, then apply it?")
                : i18n.tr("themeshop.community.overwrite_install_message",
                          "A package with this theme ID is already installed. Replace it with the version from GitHub?"),
            {
                {i18n.tr("button.cancel", "Cancel"), {}, true},
                {i18n.tr("button.replace", "Replace"), [startTransfer]() { startTransfer(); }, true}
            },
            1,
            {});
        focusManager().setFocus(m_dialog.get());
    };
    auto clearCompletedThemeTransferState = [this]() {
        if (!m_themePackageTransfer)
            return;

        bool isRunning = false;
        {
            std::lock_guard<std::mutex> lk(m_themePackageTransfer->mutex);
            isRunning = m_themePackageTransfer->state.isRunning();
        }

        if (isRunning)
            return;

        m_themePackageTransfer.reset();
        m_themePackageTransferUiRevision = 0;
        m_themePackageTransferHandledRevision = 0;
        if (m_themeShop) {
            ThemeTransferState cleared;
            m_themeShop->setPackageTransferState(cleared, {}, false);
        }
    };

    m_themeShop = std::make_shared<ThemeShopScreen>();
    if (m_overlayLayer) {
        m_overlayLayer->addChild(m_themeShop);
    }
    m_themeShop->setFont(&m_fontNormal);
    m_themeShop->setSmallFont(&m_fontSmall);
    m_themeShop->setTheme(&m_theme);
    m_themeShop->setThreadPool(&m_threadPool);
    m_themeShop->setRenderContext(&app().gpu(), &app().renderer());
    m_themeShop->setMusicState(m_audio.isPlaying(), m_audio.volume(), m_audio.sfxVolume());
    m_themeShop->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_themeShop->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_themeShop->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                   m_config.accessibilitySpeakPosition);

    m_themeShop->onMusicEnabledChange([this](bool enabled) {
        if (enabled) m_audio.play(); else m_audio.stop();
        m_config.musicEnabled = enabled;
    });
    m_themeShop->onMusicVolumeChange([this](float v) {
        m_audio.setVolume(v);
        m_config.musicVolume = v;
    });
    m_themeShop->onSfxVolumeChange([this](float v) {
        m_audio.setSfxVolume(v);
        m_config.sfxVolume = v;
    });
    m_themeShop->onGridColumnsChange([this](int cols) {
        cols = std::clamp(cols, 3, 8);
        if (m_config.gridColumns == cols)
            return;
        m_config.gridColumns = cols;
        reflowHomeGrid();
    });
    m_themeShop->onGridRowsChange([this](int rows) {
        rows = std::clamp(rows, 2, 5);
        if (m_config.gridRows == rows)
            return;
        m_config.gridRows = rows;
        reflowHomeGrid();
    });
    m_themeShop->onNextTrack([this]() {
        m_audio.nextTrack();
        m_audio.playSfx(Sfx::ConfirmPositive);
    });
    m_themeShop->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_themeShop->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_themeShop->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_themeShop->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_themeShop->onAccessibilityStructuredAnnouncement([this](const std::string& context,
                                                              const std::string& position,
                                                              const std::string& summary,
                                                              bool forceRepeat,
                                                              bool forceContext) {
        m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
    });
    m_themeShop->onToggleSfx([this](bool on) {
        m_audio.playSfx(on ? Sfx::ThemeToggle : Sfx::ToggleOff);
    });
    m_themeShop->onSliderSfx([this](bool up) {
        m_audio.playSfx(up ? Sfx::SliderUp : Sfx::SliderDown);
    });
    m_themeShop->onNetConnectRequest([this]() {
        m_pendingNetConnect = true;
    });
    m_themeShop->onThemeShopApply([this](const std::string& presetId) {
        DebugLog::log("[theme-apply] request from Theme Shop: preset=%s", presetId.c_str());
        ThemePreset* preset = findPresetPtr(presetId);
        if (!preset) {
            DebugLog::log("[theme-apply] preset not found: %s", presetId.c_str());
            return;
        }

        activateThemePreset(preset, true);
    });
    m_themeShop->onThemeShopDelete([this](const std::string& presetId) {
        auto& i18n = nxui::I18n::instance();
        ThemePreset* preset = findPresetPtr(presetId);
        if (!preset || preset->source == ThemePresetSource::BuiltIn) {
            if (!m_dialog) return;
            m_dialogReturnFocus = focusManager().current();
            m_dialog->show(
                i18n.tr("themeshop.installed.remove", "Remove Theme"),
                i18n.tr("settings.theme.builtin_readonly", "Built-in presets cannot be modified."),
                {{ i18n.tr("button.ok", "OK"), {}, true }});
            focusManager().setFocus(m_dialog.get());
            return;
        }

        deletePreset(presetId);
    });
    m_themeShop->onThemeShopDownload([queueThemeTransfer](const std::string& themeId) {
        queueThemeTransfer(themeId, false);
    });
    m_themeShop->onThemeShopDownloadInstall([queueThemeTransfer](const std::string& themeId) {
        queueThemeTransfer(themeId, true);
    });
    m_themeShop->onDialogRequest([this](const std::string& title,
                                        const std::string& msg,
                                        std::vector<ThemeShopScreen::DialogButtonDef> buttons) {
        if (!m_dialog) return;
        std::vector<OverlayDialog::ButtonDef> dlgButtons;
        bool preserveReturnFocus = (m_dialog && m_dialog->isActive() && focusManager().current() == m_dialog.get() && m_dialogReturnFocus != nullptr);
        for (size_t i = 0; i < buttons.size(); ++i) {
            auto cb = buttons[i].onPress;
            bool isLast = (i == buttons.size() - 1);
            if (buttons.size() == 1) {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            } else if (isLast) {
                dlgButtons.push_back({buttons[i].label, [cb]() { if (cb) cb(); }, true});
            } else {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            }
        }
        if (!preserveReturnFocus)
            m_dialogReturnFocus = focusManager().current();
        m_dialog->show(title, msg, std::move(dlgButtons));
        focusManager().setFocus(m_dialog.get());
    });
    m_themeShop->onClosed([this, clearCompletedThemeTransferState]() {
        m_navigator.routeDidClose(switchu::navigation::Route::ThemeShop);
        if (m_configSaveFuture.valid())
            m_configSaveFuture.wait();
        m_configSaveFuture = m_threadPool.submit([cfg = m_config]() {
            cfg.save();
        });
        DebugLog::log("[config] save queued");
        clearCompletedThemeTransferState();
        if (isCurrentFocusableWidget(m_sidebar.themeShopButton())) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_sidebar.themeShopButton());
        }
    });

}

void WiiUMenuApp::reloadThemePresets() {
    m_allPresets = ThemePreset::builtInPresets();
    auto userPresets = ThemePreset::loadUserPresets();
    auto installedPackages = ThemePreset::loadInstalledPackages();
    m_allPresets.insert(m_allPresets.end(), userPresets.begin(), userPresets.end());
    m_allPresets.insert(m_allPresets.end(), installedPackages.begin(), installedPackages.end());
}

void WiiUMenuApp::startThemePackageTransfer(const ThemeCatalogClient::Entry& entry, bool installMode) {
    syncThemePackageTransfer();

    if (m_themePackageTransferFuture.valid()
        && m_themePackageTransferFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        if (m_themeShop) {
            auto& i18n = nxui::I18n::instance();
            m_themeShop->requestToast(i18n.tr("themeshop.community.transfer_busy",
                                              "Another theme transfer is already running."),
                                      2.5f);
        }
        return;
    }

    auto shared = std::make_shared<ThemePackageTransferShared>();
    shared->themeId = entry.id;
    shared->installMode = installMode;
    shared->destinationPath = ThemePackageInstaller::destinationRootFor(
        entry.id,
        installMode ? ThemePackageInstaller::Mode::InstallAndApply
                    : ThemePackageInstaller::Mode::InstallOnly);
    auto& i18n = nxui::I18n::instance();
    const std::string preparingLabel = installMode
        ? i18n.tr("themeshop.transfer.prepare_apply", "Preparing download + apply...")
        : i18n.tr("themeshop.transfer.prepare_install", "Preparing download + install...");
    const std::string unknownTransferError = i18n.tr("themeshop.transfer.unknown_error", "Unknown transfer error");
    shared->state.begin(preparingLabel);
    shared->revision = 1;

    m_themePackageTransfer = shared;
    m_themePackageTransferUiRevision = 0;
    m_themePackageTransferHandledRevision = 0;

    if (m_themeShop)
        m_themeShop->setPackageTransferState(shared->state, shared->themeId, shared->installMode);
    if (m_progressDialog) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->show(i18n.tr("themeshop.transfer.dialog_title", "Downloading Theme"),
                               preparingLabel,
                               -1.f);
        focusManager().setFocus(m_progressDialog.get());
    }

    const std::string catalogUrl = m_themeShop
        ? m_themeShop->communityCatalogUrl()
        : ThemeCatalogClient::kDefaultCatalogUrl;
    ThemePackageInstaller::Mode mode = installMode
        ? ThemePackageInstaller::Mode::InstallAndApply
        : ThemePackageInstaller::Mode::InstallOnly;

    DebugLog::log("[themeshop] package transfer start: id=%s apply=%d",
                  entry.id.c_str(),
                  installMode ? 1 : 0);

    m_themePackageTransferFuture = m_threadPool.submit([shared, catalogUrl, entry, mode, unknownTransferError]() {
        try {
            auto result = ThemePackageInstaller::run(
                catalogUrl,
                entry,
                mode,
                [shared](std::string label, float progress) {
                    std::lock_guard<std::mutex> lk(shared->mutex);
                    shared->state.update(std::move(label), progress);
                    ++shared->revision;
                });

            std::lock_guard<std::mutex> lk(shared->mutex);
            shared->destinationPath = result.destinationPath;
            shared->state.succeed(result.message);
            ++shared->revision;
            DebugLog::log("[themeshop] package transfer success: %s", result.themeId.c_str());
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lk(shared->mutex);
            shared->state.fail(ex.what());
            ++shared->revision;
            DebugLog::log("[themeshop] package transfer failed: %s", ex.what());
        } catch (...) {
            std::lock_guard<std::mutex> lk(shared->mutex);
            shared->state.fail(unknownTransferError);
            ++shared->revision;
            DebugLog::log("[themeshop] package transfer failed: unknown error");
        }
    });
}

void WiiUMenuApp::syncThemePackageTransfer() {
    auto shared = m_themePackageTransfer;
    if (!shared)
        return;

    bool futureReady = false;
    if (m_themePackageTransferFuture.valid()
        && m_themePackageTransferFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_themePackageTransferFuture.get();
        futureReady = true;
    }

    ThemeTransferState state;
    std::string themeId;
    bool installMode = false;
    std::uint64_t revision = 0;
    std::string destinationPath;
    {
        std::lock_guard<std::mutex> lk(shared->mutex);
        state = shared->state;
        themeId = shared->themeId;
        installMode = shared->installMode;
        revision = shared->revision;
        destinationPath = shared->destinationPath;
    }

    if (revision != m_themePackageTransferUiRevision) {
        if (m_themeShop)
            m_themeShop->setPackageTransferState(state, themeId, installMode);
        if (m_progressDialog && state.isRunning()) {
            auto& i18n = nxui::I18n::instance();
            m_progressDialog->updateState(
                state.label().empty()
                    ? i18n.tr("themeshop.transfer.downloading", "Downloading theme...")
                    : state.label(),
                state.progress01());
            if (focusManager().current() != m_progressDialog.get())
                focusManager().setFocus(m_progressDialog.get());
        }
        m_themePackageTransferUiRevision = revision;
    }

    if (!futureReady || revision == m_themePackageTransferHandledRevision)
        return;

    if (state.isReady()) {
        DebugLog::log("[theme-apply] transfer ready: id=%s installMode=%d destination=%s",
                      themeId.c_str(),
                      installMode ? 1 : 0,
                      safeLogPath(destinationPath));

        reloadThemePresets();
        DebugLog::log("[theme-apply] presets reloaded after transfer: count=%zu", m_allPresets.size());

        auto& i18n = nxui::I18n::instance();
        std::string successMessage;
        if (installMode) {
            ThemePreset* preset = findPresetPtr("package:" + themeId);
            if (preset) {
                m_forceThemeResourceReload = !preset->fonts.regularPath.empty()
                    || !preset->fonts.smallPath.empty()
                    || !preset->background.imagePath.empty();
                if (!preset->icons.basePath.empty())
                    m_sidebar.invalidateAssetsCache();
                DebugLog::log("[theme-apply] auto-applying installed package: id=%s", themeId.c_str());
                activateThemePreset(preset, true);
                successMessage = i18n.tr("themeshop.transfer.installed_applied", "Theme installed and applied.");
            } else {
                DebugLog::log("[theme-apply] auto-apply failed, preset missing after install: id=%s", themeId.c_str());
                refreshThemeShopState();
                successMessage = i18n.tr("themeshop.transfer.installed_apply_failed",
                                         "Theme installed, but it could not be applied automatically.");
            }
        } else {
            DebugLog::log("[theme-apply] install-only transfer complete: id=%s", themeId.c_str());
            refreshThemeShopState();
            successMessage = i18n.tr("themeshop.transfer.installed", "Theme installed.");
        }

        state.succeed(successMessage);
        if (m_themeShop)
            m_themeShop->setPackageTransferState(state, themeId, installMode);
    }

    if (m_themeShop && !state.label().empty())
        m_themeShop->requestToast(state.label(), state.hasFailed() ? 3.5f : 2.8f);
    if (m_progressDialog && m_progressDialog->isActive()) {
        m_progressDialog->updateState(state.label(), state.progress01());
        m_progressDialog->hide();
        if (m_themeShop && m_themeShop->isActive())
            focusManager().setFocus(m_themeShop.get());
    }

    m_themePackageTransferHandledRevision = revision;
}

std::vector<ThemeShopScreen::ThemeShopEntry> WiiUMenuApp::buildThemeShopEntries() {
    std::vector<ThemeShopScreen::ThemeShopEntry> entries;
    entries.reserve(m_allPresets.size());

    ThemePreset* activePreset = findPresetPtr(m_activePresetName);
    std::string activeId = activePreset ? (activePreset->id.empty() ? activePreset->name : activePreset->id)
                                        : m_activePresetName;

    for (const auto& preset : m_allPresets) {
        ThemeShopScreen::ThemeShopEntry entry;
        entry.id = preset.id.empty() ? preset.name : preset.id;
        entry.name = preset.name;
        entry.author = preset.author;
        entry.version = preset.version;
        entry.source = sourceLabel(preset.source);
        if (preset.soundPreset.rfind("package:", 0) == 0)
            entry.soundPreset = "Bundled";
        else if (preset.soundPreset == "wiiu")
            entry.soundPreset = "Wii U";
        else
            entry.soundPreset = preset.soundPreset;
        entry.coverPath = installedThemePreviewPath(preset);
        entry.active = (entry.id == activeId);
        entry.removable = (preset.source != ThemePresetSource::BuiltIn);
        entries.push_back(std::move(entry));
    }

    return entries;
}

void WiiUMenuApp::refreshThemeShopState() {
    if (!m_themeShop) return;

    ThemePreset* activePreset = findPresetPtr(m_activePresetName);
    std::string activeId = activePreset ? (activePreset->id.empty() ? activePreset->name : activePreset->id)
                                        : m_activePresetName;

    m_themeShop->setTheme(&m_theme);
    m_themeShop->setMusicState(m_audio.isPlaying(), m_audio.volume(), m_audio.sfxVolume());
    m_themeShop->setThemeShopState(buildThemeShopEntries(), activeId);
    m_themeShop->rebuildCurrentTab();
}

void WiiUMenuApp::activateThemePreset(ThemePreset* preset, bool applyBundledSound) {
    if (!preset) return;

    const std::string presetRef = preset->id.empty() ? preset->name : preset->id;
    DebugLog::log("[theme-apply] begin preset=%s name=%s source=%s installPath=%s applyBundledSound=%d soundPreset=%s",
                  presetRef.c_str(),
                  preset->name.c_str(),
                  themeSourceName(preset->source),
                  safeLogPath(preset->installPath),
                  applyBundledSound ? 1 : 0,
                  safeLogPath(preset->soundPreset));

    m_activePresetName = preset->id.empty() ? preset->name : preset->id;
    m_activeColors = preset->colors;
    m_activeMode = preset->mode;
    m_config.themePreset = m_activePresetName;

    DebugLog::log("[theme-apply] rebuildThemeFromColors start: preset=%s", presetRef.c_str());
    rebuildThemeFromColors();
    DebugLog::log("[theme-apply] rebuildThemeFromColors done: preset=%s", presetRef.c_str());

    if (applyBundledSound && !preset->soundPreset.empty()) {
        DebugLog::log("[theme-apply] changeSoundPreset queued: preset=%s sound=%s",
                      presetRef.c_str(),
                      preset->soundPreset.c_str());
        changeSoundPreset(preset->soundPreset);
        m_config.soundPreset = preset->soundPreset;
    } else {
        DebugLog::log("[theme-apply] bundled sound skipped: preset=%s apply=%d soundPreset=%s",
                      presetRef.c_str(),
                      applyBundledSound ? 1 : 0,
                      safeLogPath(preset->soundPreset));
    }

    DebugLog::log("[theme-apply] refreshThemeShopState start: preset=%s", presetRef.c_str());
    refreshThemeShopState();
    DebugLog::log("[theme-apply] refreshThemeShopState done: preset=%s", presetRef.c_str());
    m_themeRenderDebugFrames = 6;
    if (m_themeShop)
        m_themeShop->requestRenderDiagnostics(6);
    m_audio.playSfx(Sfx::ThemeToggle);
    DebugLog::log("[theme-apply] complete preset=%s", presetRef.c_str());
}

void WiiUMenuApp::applyUiLanguage() {
    auto& i18n = nxui::I18n::instance();
    if (m_config.uiLanguageOverride == "auto" || m_config.uiLanguageOverride.empty())
        i18n.setLanguageAuto();
    else
        i18n.setLanguage(m_config.uiLanguageOverride);
    m_accessibility.setVoiceForLanguageTag(i18n.activeLanguageTag());
}

std::string WiiUMenuApp::resolveThemeAssetPath(const ThemePreset& preset, const std::string& rawPath) const {
    if (rawPath.empty())
        return {};
    if (isAbsoluteThemePath(rawPath))
        return rawPath;
    if ((preset.source == ThemePresetSource::InstalledPackage || preset.source == ThemePresetSource::BuiltIn)
        && !preset.installPath.empty())
        return joinPath(preset.installPath, rawPath);
    return joinPath(SD_ASSETS, rawPath);
}

ThemePreset WiiUMenuApp::buildEffectiveThemePreset() {
    ThemePreset effective;
    if (ThemePreset* preset = findPresetPtr(m_activePresetName))
        effective = *preset;

    effective.mode = m_activeMode;
    effective.colors = m_activeColors;
    return effective;
}

void WiiUMenuApp::applyThemeResources(const ThemePreset& preset) {
    auto& gpu = app().gpu();
    auto& ren = app().renderer();
    const bool forceResourceReload = m_forceThemeResourceReload;

    const std::string fallbackFontPath = std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf";
    const std::string regularFontPath = resolveThemeAssetPath(preset, preset.fonts.regularPath);
    const std::string smallFontPath = resolveThemeAssetPath(
        preset,
        !preset.fonts.smallPath.empty() ? preset.fonts.smallPath : preset.fonts.regularPath);
    const std::string themeIconsBase = resolveThemeAssetPath(preset, preset.icons.basePath);
    const std::string backgroundImagePath = resolveThemeAssetPath(preset, preset.background.imagePath);

    // This application supersedes any retry left by the previous theme.
    m_pendingBackgroundImagePath.clear();
    m_backgroundImageRetryFrames = 0;
    m_backgroundImageRetryAttempts = 0;

    const std::string presetRef = preset.id.empty() ? preset.name : preset.id;
    DebugLog::log("[theme-apply] resources start: preset=%s regularFont=%s smallFont=%s iconsBase=%s bgImage=%s",
                  presetRef.c_str(),
                  safeLogPath(regularFontPath),
                  safeLogPath(smallFontPath),
                  safeLogPath(themeIconsBase),
                  safeLogPath(backgroundImagePath));

    bool settingsLayoutNeedsRebuild = false;

    const bool regularFontExists = !regularFontPath.empty() && pathExists(regularFontPath);
    const std::string desiredRegularFontPath = regularFontExists ? regularFontPath : fallbackFontPath;
    const bool regularFontNeedsReload = forceResourceReload || m_loadedRegularFontPath != desiredRegularFontPath;

    const bool smallFontExists = !smallFontPath.empty() && pathExists(smallFontPath);
    const std::string desiredSmallFontPath = smallFontExists ? smallFontPath : fallbackFontPath;
    const bool smallFontNeedsReload = forceResourceReload || m_loadedSmallFontPath != desiredSmallFontPath;

    const std::string defaultGameCardPath = std::string(SD_ASSETS) + "/icons/gamecard.png";
    const std::string gameCardPath = !themeIconsBase.empty() ? joinPath(themeIconsBase, "gamecard.png") : std::string();
    const bool gameCardExists = !gameCardPath.empty() && pathExists(gameCardPath);
    const std::string desiredGameCardPath = gameCardExists ? gameCardPath : defaultGameCardPath;
    const bool gameCardNeedsReload = forceResourceReload || m_loadedGameCardPath != desiredGameCardPath;

    const bool imageExists = !backgroundImagePath.empty() && pathExists(backgroundImagePath);
    const bool wantsBackgroundImage = imageExists;
    const bool backgroundImageNeedsReload = forceResourceReload
        || m_loadedBackgroundImagePath != backgroundImagePath
        || m_backgroundImageLoaded != wantsBackgroundImage;

    const bool needsGpuResourceReload = regularFontNeedsReload
        || smallFontNeedsReload
        || gameCardNeedsReload
        || backgroundImageNeedsReload;
    if (needsGpuResourceReload) {
        DebugLog::log("[theme-apply] waiting for GPU idle before reloading theme resources");
        gpu.waitIdle();
    }

    if (regularFontNeedsReload) {
        bool regularFontLoaded = regularFontExists && m_fontNormal.load(gpu, ren, regularFontPath, 24);
        DebugLog::log("[theme-apply] regular font: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(regularFontPath),
                      regularFontExists ? 1 : 0,
                      1,
                      regularFontLoaded ? 1 : 0);
        if (!regularFontLoaded) {
            regularFontLoaded = m_fontNormal.load(gpu, ren, fallbackFontPath, 24);
            DebugLog::log("[theme-apply] regular font fallback: path=%s loaded=%d",
                          fallbackFontPath.c_str(),
                          regularFontLoaded ? 1 : 0);
        }
        if (regularFontLoaded) {
            m_loadedRegularFontPath = desiredRegularFontPath;
            settingsLayoutNeedsRebuild = true;
        }
    } else {
        DebugLog::log("[theme-apply] regular font: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(regularFontPath),
                      regularFontExists ? 1 : 0,
                      0,
                      1);
    }

    if (smallFontNeedsReload) {
        bool smallFontLoaded = smallFontExists && m_fontSmall.load(gpu, ren, smallFontPath, 18);
        DebugLog::log("[theme-apply] small font: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(smallFontPath),
                      smallFontExists ? 1 : 0,
                      1,
                      smallFontLoaded ? 1 : 0);
        if (!smallFontLoaded) {
            smallFontLoaded = m_fontSmall.load(gpu, ren, fallbackFontPath, 18);
            DebugLog::log("[theme-apply] small font fallback: path=%s loaded=%d",
                          fallbackFontPath.c_str(),
                          smallFontLoaded ? 1 : 0);
        }
        if (smallFontLoaded) {
            m_loadedSmallFontPath = desiredSmallFontPath;
            settingsLayoutNeedsRebuild = true;
        }
    } else {
        DebugLog::log("[theme-apply] small font: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(smallFontPath),
                      smallFontExists ? 1 : 0,
                      0,
                      1);
    }

    if (gameCardNeedsReload) {
        bool gameCardLoaded = gameCardExists && m_gameCardTex.loadFromFile(gpu, ren, gameCardPath);
        DebugLog::log("[theme-apply] gamecard icon: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(gameCardPath),
                      gameCardExists ? 1 : 0,
                      1,
                      gameCardLoaded ? 1 : 0);
        if (!gameCardLoaded) {
            gameCardLoaded = m_gameCardTex.loadFromFile(gpu, ren, defaultGameCardPath);
            DebugLog::log("[theme-apply] gamecard fallback: path=%s loaded=%d",
                          defaultGameCardPath.c_str(),
                          gameCardLoaded ? 1 : 0);
        }
        if (gameCardLoaded)
            m_loadedGameCardPath = desiredGameCardPath;
    } else {
        DebugLog::log("[theme-apply] gamecard icon: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(gameCardPath),
                      gameCardExists ? 1 : 0,
                      0,
                      1);
    }

    if (m_background) {
        DebugLog::log("[theme-apply] background config start: preset=%s", presetRef.c_str());
        WaraWaraBackground::Config backgroundConfig;
        backgroundConfig.layout = toBackgroundLayout(preset.background.layout);
        backgroundConfig.shapeSet = toBackgroundShapeSet(preset.background.shapeSet);
        backgroundConfig.symmetry = toBackgroundSymmetry(preset.background.symmetry);
        backgroundConfig.shapeCount = preset.background.shapeCount;
        backgroundConfig.gridColumns = preset.background.gridColumns;
        backgroundConfig.gridRows = preset.background.gridRows;
        backgroundConfig.spacingX = preset.background.spacingX;
        backgroundConfig.spacingY = preset.background.spacingY;
        backgroundConfig.sizeMin = preset.background.sizeMin;
        backgroundConfig.sizeMax = preset.background.sizeMax;
        backgroundConfig.speedMin = preset.background.speedMin;
        backgroundConfig.speedMax = preset.background.speedMax;
        backgroundConfig.wobble = preset.background.wobble;
        backgroundConfig.opacity = preset.background.opacity;
        backgroundConfig.rotationSpeed = preset.background.rotationSpeed;
        backgroundConfig.fixedOrientation = preset.background.fixedOrientation;
        backgroundConfig.orientationDegrees = preset.background.orientationDegrees;
        backgroundConfig.cornerRoundness = preset.background.cornerRoundness;
        backgroundConfig.imageOpacity = preset.background.imageOpacity;
        backgroundConfig.imageCover = preset.background.imageCover;
        m_background->setConfig(backgroundConfig);

        if (backgroundImageNeedsReload) {
            const bool imageLoaded = imageExists && m_background->loadImage(gpu, ren, backgroundImagePath);
            DebugLog::log("[theme-apply] background image: path=%s exists=%d reloaded=%d loaded=%d",
                          safeLogPath(backgroundImagePath),
                          imageExists ? 1 : 0,
                          1,
                          imageLoaded ? 1 : 0);
            if (!imageLoaded) {
                if (m_backgroundImageLoaded)
                    m_background->clearImage();
                m_backgroundImageLoaded = false;
                m_loadedBackgroundImagePath.clear();
                DebugLog::log("[theme-apply] background image cleared");
                if (!backgroundImagePath.empty()) {
                    m_pendingBackgroundImagePath = backgroundImagePath;
                    m_backgroundImageRetryFrames = 2;
                    DebugLog::log("[theme-apply] background image retry scheduled: path=%s",
                                  safeLogPath(backgroundImagePath));
                }
            } else {
                m_backgroundImageLoaded = true;
                m_loadedBackgroundImagePath = backgroundImagePath;
            }
        } else {
            DebugLog::log("[theme-apply] background image: path=%s exists=%d reloaded=%d loaded=%d",
                          safeLogPath(backgroundImagePath),
                          imageExists ? 1 : 0,
                          0,
                          m_backgroundImageLoaded ? 1 : 0);
        }
    } else {
        DebugLog::log("[theme-apply] background widget missing");
    }

    if (settingsLayoutNeedsRebuild && m_settings && m_settings->isActive()) {
        m_settings->rebuildCurrentTab();
        DebugLog::log("[theme-apply] settings current tab rebuilt for font change");
    }

    DebugLog::log("[theme-apply] resources done: preset=%s", presetRef.c_str());
}

void WiiUMenuApp::retryPendingBackgroundImage() {
    if (m_pendingBackgroundImagePath.empty() || !m_background)
        return;
    if (m_backgroundImageRetryFrames > 0) {
        --m_backgroundImageRetryFrames;
        return;
    }

    const std::string expectedPath = resolveThemeAssetPath(
        m_effectivePreset, m_effectivePreset.background.imagePath);
    if (expectedPath != m_pendingBackgroundImagePath) {
        DebugLog::log("[theme-apply] background retry cancelled after theme changed");
        m_pendingBackgroundImagePath.clear();
        return;
    }

    ++m_backgroundImageRetryAttempts;
    const bool exists = pathExists(m_pendingBackgroundImagePath);
    bool loaded = false;
    if (exists) {
        app().gpu().waitIdle();
        loaded = m_background->loadImage(app().gpu(), app().renderer(),
                                         m_pendingBackgroundImagePath);
    }
    DebugLog::log("[theme-apply] background retry %d/3: path=%s exists=%d loaded=%d",
                  m_backgroundImageRetryAttempts,
                  safeLogPath(m_pendingBackgroundImagePath),
                  exists ? 1 : 0, loaded ? 1 : 0);

    if (loaded) {
        m_backgroundImageLoaded = true;
        m_loadedBackgroundImagePath = m_pendingBackgroundImagePath;
        m_pendingBackgroundImagePath.clear();
        m_backgroundImageRetryAttempts = 0;
        return;
    }

    if (m_backgroundImageRetryAttempts >= 3) {
        DebugLog::log("[theme-apply] background retry exhausted: path=%s",
                      safeLogPath(m_pendingBackgroundImagePath));
        m_pendingBackgroundImagePath.clear();
        return;
    }
    m_backgroundImageRetryFrames = 2 << (m_backgroundImageRetryAttempts - 1);
}

void WiiUMenuApp::applyTheme() {
    DebugLog::log("[theme-apply] widget recolor start");
    m_background->setAccentColor(m_theme.backgroundAccent);
    m_background->setSecondaryColor(m_theme.background);
    m_background->setShapeColor(m_theme.shapeColor);
    DebugLog::log("[theme-apply] widget recolor background done");

    for (auto& icon : m_grid->allIcons()) {
        icon->setBaseColor(m_theme.iconDefault);
        icon->setBorderColor(m_theme.panelBorder);
        icon->setHighlightColor(m_theme.panelHighlight);
        icon->setCornerRadius(m_theme.iconCornerRadius);
        icon->setLoadingColor(m_theme.cursorNormal);
    }
    DebugLog::log("[theme-apply] widget recolor grid icons done");

    m_cursor->setColor(m_theme.cursorNormal);
    m_cursor->setCornerRadius(m_theme.cursorCornerRadius);
    m_cursor->setBorderWidth(m_theme.cursorBorderWidth);
    if (m_pointerCursor) {
        m_pointerCursor->setColor(m_theme.cursorNormal);
        m_pointerCursor->setCornerRadius(15.f);
        m_pointerCursor->setBorderWidth(2.5f);
    }
    for (auto& avatar : m_userAvatarButtons) {
        if (avatar)
            avatar->setTheme(&m_theme);
    }
    DebugLog::log("[theme-apply] widget recolor cursors done");

    m_clock->setBaseColor(m_theme.panelBase);
    m_clock->setBorderColor(m_theme.panelBorder);
    m_clock->setHighlightColor(m_theme.panelHighlight);
    m_clock->setTextColor(m_theme.textPrimary);
    m_clock->setSecondaryTextColor(m_theme.textSecondary);

    m_battery->setBaseColor(m_theme.panelBase);
    m_battery->setBorderColor(m_theme.panelBorder);
    m_battery->setHighlightColor(m_theme.panelHighlight);
    m_battery->setTextColor(m_theme.textPrimary);

    m_titlePill->setBaseColor(m_theme.panelBase.withAlpha(
        m_theme.mode == nxui::ThemeMode::Dark ? 0.90f : 0.86f));
    m_titlePill->setBorderColor(m_theme.panelBorder.withAlpha(0.84f));
    m_titlePill->setHighlightColor(m_theme.panelHighlight.withAlpha(0.42f));
    m_titlePill->setTextColor(m_theme.textPrimary);

    m_pageIndicator->setBaseColor(m_theme.panelBase);
    m_pageIndicator->setBorderColor(m_theme.panelBorder);
    m_pageIndicator->setHighlightColor(m_theme.panelHighlight);
    m_pageIndicator->setTheme(&m_theme);
    if (m_folderHeader) {
        m_folderHeader->setBaseColor(m_theme.panelBase);
        m_folderHeader->setBorderColor(m_theme.panelBorder);
        m_folderHeader->setHighlightColor(m_theme.panelHighlight);
    }
    if (m_folderHeaderLabel)
        m_folderHeaderLabel->setTextColor(m_theme.textPrimary);
    for (auto& avatar : m_userAvatarButtons) {
        avatar->setBaseColor(m_theme.iconDefault.withAlpha(
            m_theme.mode == nxui::ThemeMode::Dark ? 0.92f : 0.94f));
        avatar->setBorderColor(m_theme.panelBorder);
        avatar->setHighlightColor(m_theme.panelHighlight);
        avatar->setCornerRadius(28.f);
        avatar->setChromeEnabled(true);
    }
    DebugLog::log("[theme-apply] widget recolor HUD done");

    m_userSelect->setTheme(&m_theme);
    m_userSelect->cursor().setColor(m_theme.cursorNormal);

    if (m_dialog) {
        m_dialog->setTheme(&m_theme);
        m_dialog->setBaseColor(m_theme.panelBase);
        m_dialog->setBorderColor(m_theme.panelBorder);
        m_dialog->setHighlightColor(m_theme.panelHighlight);
        m_dialog->cursor().setColor(m_theme.cursorNormal);
    }
    DebugLog::log("[theme-apply] widget recolor overlays done");

    if (m_settings)
        m_settings->setTheme(&m_theme);
    if (m_themeShop)
        m_themeShop->setTheme(&m_theme);
    if (m_gameOptions)
        m_gameOptions->setTheme(&m_theme);
    if (m_folderOptions)
        m_folderOptions->setTheme(&m_theme);
    if (m_controllerTest)
        m_controllerTest->setTheme(&m_theme);

    m_sidebar.applyTheme(m_theme);
    DebugLog::log("[theme-apply] widget recolor complete");
}

void WiiUMenuApp::rebuildThemeFromColors() {
    DebugLog::log("[theme-apply] rebuild start: activePreset=%s", m_activePresetName.c_str());
    m_effectivePreset = buildEffectiveThemePreset();
    DebugLog::log("[theme-apply] effective preset resolved: id=%s name=%s source=%s installPath=%s",
                  safeLogPath(m_effectivePreset.id),
                  m_effectivePreset.name.c_str(),
                  themeSourceName(m_effectivePreset.source),
                  safeLogPath(m_effectivePreset.installPath));
    m_theme = m_effectivePreset.toTheme();
    DebugLog::log("[theme-apply] theme object rebuilt");

    DebugLog::log("[theme-apply] applyThemeResources start: preset=%s",
                  safeLogPath(m_effectivePreset.id.empty() ? m_effectivePreset.name : m_effectivePreset.id));
    applyThemeResources(m_effectivePreset);
    DebugLog::log("[theme-apply] applyThemeResources done: preset=%s",
                  safeLogPath(m_effectivePreset.id.empty() ? m_effectivePreset.name : m_effectivePreset.id));

    const std::string customIconsBase = resolveThemeAssetPath(m_effectivePreset, m_effectivePreset.icons.basePath);
    DebugLog::log("[theme-apply] sidebar.reloadAssets start: customIcons=%s",
                  safeLogPath(customIconsBase));
    m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS, customIconsBase);
    DebugLog::log("[theme-apply] sidebar.reloadAssets done");

    DebugLog::log("[theme-apply] applyTheme start");
    applyTheme();
    m_forceThemeResourceReload = false;
    DebugLog::log("[theme-apply] rebuild complete: activePreset=%s", m_activePresetName.c_str());
}

ThemePreset* WiiUMenuApp::findPresetPtr(const std::string& name) {
    for (auto& p : m_allPresets)
        if (p.id == name || p.name == name) return &p;
    return nullptr;
}

void WiiUMenuApp::deletePreset(const std::string& presetId) {
    ThemePreset* preset = findPresetPtr(presetId);
    if (!preset) return;

    ThemePreset* activePreset = findPresetPtr(m_activePresetName);
    std::string activeId = activePreset ? (activePreset->id.empty() ? activePreset->name : activePreset->id)
                                        : m_activePresetName;
    std::string idToDelete = preset->id.empty() ? preset->name : preset->id;
    std::string nameToDelete = preset->name;
    std::string installPath = preset->installPath;
    std::string soundPreset = preset->soundPreset;
    ThemePresetSource source = preset->source;
    bool deletingActive = (activeId == idToDelete);

    m_allPresets.erase(
        std::remove_if(m_allPresets.begin(), m_allPresets.end(),
            [&](const ThemePreset& p) { return p.id == idToDelete || p.name == nameToDelete; }),
        m_allPresets.end());

    auto userPresets = ThemePreset::loadUserPresets();
    userPresets.erase(
        std::remove_if(userPresets.begin(), userPresets.end(),
            [&](const ThemePreset& p) { return p.id == idToDelete || p.name == nameToDelete; }),
        userPresets.end());
    ThemePreset::saveUserPresets(userPresets);

    if (source == ThemePresetSource::InstalledPackage && !installPath.empty()) {
        if (m_themeDeleteFuture.valid())
            m_themeDeleteFuture.wait();
        m_themeDeleteFuture = m_threadPool.submit([installPath]() {
            const bool removed = removeDirectoryRecursive(installPath);
            DebugLog::log("[theme-delete] async removal %s: %s",
                          removed ? "complete" : "failed", safeLogPath(installPath));
        });
    }

    if (!soundPreset.empty() && m_config.soundPreset == soundPreset) {
        m_config.soundPreset = "wiiu";
        changeSoundPreset(m_config.soundPreset);
    }

    if (deletingActive) {
        m_activePresetName = defaultThemeRef();
        m_config.themePreset = defaultThemeRef();

        ThemePreset* fallback = findPresetPtr(defaultThemeRef());
        if (!fallback)
            fallback = findPresetPtr("Default Light");
        if (fallback) {
            m_activePresetName = fallback->id.empty() ? fallback->name : fallback->id;
            m_config.themePreset = m_activePresetName;
            m_activeColors = fallback->colors;
            m_activeMode = fallback->mode;
        }
        rebuildThemeFromColors();
    }

    refreshThemeShopState();
    m_audio.playSfx(Sfx::ConfirmPositive);
}

void WiiUMenuApp::startSoftwareDeletion(uint64_t titleId, const std::string& title,
                                        bool closeGameOptionsOnSuccess) {
    if (m_softwareDeleteFuture.valid()
        && m_softwareDeleteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    auto& i18n = nxui::I18n::instance();
    m_softwareDeleteTitle = title;
    m_softwareDeleteClosesGameOptions = closeGameOptionsOnSuccess;
    m_dialogReturnFocus = closeGameOptionsOnSuccess
        ? static_cast<nxui::Widget*>(m_gameOptions.get())
        : static_cast<nxui::Widget*>(m_settings.get());
    if (m_progressDialog) {
        m_progressDialog->show(
            i18n.tr("game.uninstall_progress_title", "Deleting software"),
            i18n.tr("game.uninstall_progress", "Please wait. Do not turn off the console.")
                + std::string("\n") + title,
            -1.f);
        focusManager().setFocus(m_progressDialog.get());
    }

    DebugLog::log("[software-delete] queued title=%016llX",
                  static_cast<unsigned long long>(titleId));
    m_softwareDeleteResult = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    m_softwareDeleteFuture = m_threadPool.submit([this, titleId]() {
        m_softwareDeleteResult = nsDeleteApplicationCompletely(titleId);
    });
}

void WiiUMenuApp::syncSoftwareDeletion() {
    if (!m_softwareDeleteFuture.valid()
        || m_softwareDeleteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    Result rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    try {
        m_softwareDeleteFuture.get();
        rc = m_softwareDeleteResult;
    } catch (const std::exception& ex) {
        DebugLog::log("[software-delete] worker exception: %s", ex.what());
    } catch (...) {
        DebugLog::log("[software-delete] worker exception: unknown");
    }

    if (m_progressDialog)
        m_progressDialog->hide();

    auto& i18n = nxui::I18n::instance();
    if (R_SUCCEEDED(rc)) {
        DebugLog::log("[software-delete] complete: %s", m_softwareDeleteTitle.c_str());
        m_audio.playSfx(Sfx::ConfirmPositive);
        if (m_softwareDeleteClosesGameOptions && m_gameOptions) {
            m_gameOptions->hide();
        } else if (m_settings) {
            m_settings->requestToast(
                i18n.tr("settings.storage.uninstall_success", "Uninstalled successfully."), 2.8f);
            m_settings->rebuildCurrentTab();
            focusManager().setFocus(m_settings.get());
        }
        m_refreshQueued = true;
        m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 3);
    } else {
        DebugLog::log("[software-delete] failed rc=0x%08X module=%u description=%u",
                      rc, R_MODULE(rc), R_DESCRIPTION(rc));
        const std::string details = fmt::format(
            "{}\nResult: 0x{:08X} (module {}, description {})",
            i18n.tr("settings.storage.uninstall_failed", "Failed to uninstall the selected title."),
            rc, R_MODULE(rc), R_DESCRIPTION(rc));
        if (m_dialog) {
            m_dialog->show(
                i18n.tr("settings.storage.uninstall_failed_title", "Uninstall Failed"),
                details,
                {{i18n.tr("button.ok", "OK"), []() {}, true}});
            focusManager().setFocus(m_dialog.get());
        }
    }

    m_softwareDeleteTitle.clear();
    m_softwareDeleteClosesGameOptions = false;
}
