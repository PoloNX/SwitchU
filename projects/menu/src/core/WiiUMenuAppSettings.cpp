#include "WiiUMenuApp.hpp"
#include "icons/IconPackInstaller.hpp"
#include "icons/IconPackManager.hpp"
#include "themeshop/ThemePackageInstaller.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "widgets/AdaptiveIconLoad.hpp"
#include "widgets/GlossyIcon.hpp"
#include "DebugLog.hpp"
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#endif

#include <nxui/core/I18n.hpp>
#include <switchu/control_cache.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

// Defined in WiiUMenuApp.cpp (shared theme-sound helpers).
std::string resolveThemeSoundBase(const std::string& installPath);
size_t appendMp3TracksFromDir(AudioManager& audio,
                              const std::string& musicDir,
                              const std::string& albumFolder);

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

std::string themeFolderFromInstallPath(const std::string& installPath) {
    if (installPath.empty())
        return {};
    const auto pos = installPath.find_last_of("/\\");
    return (pos == std::string::npos) ? installPath : installPath.substr(pos + 1);
}

std::string themeFolderFromPreset(const ThemePreset& preset) {
    std::string folder = themeFolderFromInstallPath(preset.installPath);
    if (!folder.empty())
        return folder;
    std::string id = preset.id.empty() ? preset.name : preset.id;
    if (id.rfind("package:", 0) == 0)
        id = id.substr(8);
    return id;
}

std::string resolvedThemeIconPackId(const ThemePreset& preset, const std::string& themeFolder) {
    if (!preset.icons.packId.empty())
        return preset.icons.packId;
    return themeFolder;
}

bool writeIconPackManifest(const std::string& destDir,
                           const std::string& packId,
                           const std::string& name,
                           const std::string& author) {
    if (destDir.empty() || packId.empty())
        return false;
    nlohmann::json root = {
        {"id", packId},
        {"name", name.empty() ? packId : name},
        {"author", author.empty() ? "Theme" : author},
        {"version", "1.0.0"},
    };
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::ofstream out(joinPath(destDir, "pack.json"), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << root.dump(2) << '\n';
    return static_cast<bool>(out);
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
        "media/screenshots/00.png",
        "media/screenshots/cover.png",
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
    m_settings->setInstantCursorMotion(m_config.cursorMotionMode == 1);
    m_settings->setWireframeState(m_showWireframe);
    m_settings->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_settings->setUiLanguageOverride(m_config.uiLanguageOverride);
    m_settings->setClockUse12HourState(m_config.clockUse12Hour);
    m_settings->setWebClockSyncState(m_config.webClockSyncEnabled);
    m_settings->setAccessibilityEnabledState(m_config.accessibilityEnabled);
    m_settings->setAccessibilitySpeechState(m_config.accessibilitySpeakHints,
                                            m_config.accessibilitySpeakContextEveryFocus,
                                            m_config.accessibilitySpeakPosition,
                                            m_config.accessibilitySpeechRate);
    m_settings->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                  m_config.accessibilitySpeakPosition);
    m_settings->setSteamGridDbState(m_config.steamGridDbEnabled,
                                    !m_config.steamGridDbApiKey.empty(),
                                    m_config.steamGridDbShowInGrid,
                                    m_config.steamGridDbShowInDynamicLine,
                                    m_config.steamGridDbShowInFolders,
                                    m_config.steamGridDbShowPosterInDynamicLine,
                                    m_config.steamGridDbShowLogoInDynamicLine);

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
        cols = std::clamp(cols, 1, 8);
        if (m_config.gridColumns == cols)
            return;
        m_config.gridColumns = cols;
        reflowHomeGrid();
    });
    m_settings->onGridRowsChange([this](int rows) {
        rows = std::clamp(rows, 1, 5);
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
#ifdef NXUI_BACKEND_DEKO3D
        app().renderer().reclaimReleasedTextureSlotsAfterIdle();
#endif
        m_settingsNeedRefresh = true;
    });
    m_settings->onClockUse12HourChange([this](bool enabled) {
        if (m_config.clockUse12Hour == enabled)
            return;
        m_config.clockUse12Hour = enabled;
        if (m_clock)
            m_clock->setUse12HourClock(enabled);
    });
    m_settings->onWebClockSyncChange([this](bool enabled) {
        if (m_config.webClockSyncEnabled == enabled)
            return;
        m_config.webClockSyncEnabled = enabled;
    });
    m_settings->onExternalToast([this](const std::string& msg, float) {
        if (!m_dialog || msg.empty())
            return;
        auto& i18n = nxui::I18n::instance();
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            i18n.tr("settings.system.web_time",
                    "Synchronize Clock via Public Servers"),
            msg,
            {
                { i18n.tr("button.ok", "OK"), []() {} }
            });
        focusManager().setFocus(m_dialog.get());
    });
    m_settings->onClockSyncBootRetry([this](const std::string& error) {
        constexpr int kMaxBootClockSyncFailures = 12;
        m_webClockBootSyncStarted = false;
        ++m_webClockBootSyncFailures;
        DebugLog::log("[boot] public web clock sync retry %d/%d: %s",
                      m_webClockBootSyncFailures,
                      kMaxBootClockSyncFailures,
                      error.c_str());
        if (m_webClockBootSyncFailures >= kMaxBootClockSyncFailures) {
            m_webClockBootSyncStarted = true;
            if (!m_settings)
                return;
            auto& i18n = nxui::I18n::instance();
            const std::string prefix = i18n.tr(
                "settings.system.web_time_failed",
                "Could not sync clock from the web.");
            const std::string msg = error.empty() ? prefix : prefix + " " + error;
            if (m_dialog) {
                m_dialogReturnFocus = focusManager().current();
                m_dialog->show(
                    i18n.tr("settings.system.web_time",
                            "Synchronize Clock via Public Servers"),
                    msg,
                    {{ i18n.tr("button.ok", "OK"), []() {} }});
                focusManager().setFocus(m_dialog.get());
            } else {
                m_settings->requestToast(msg, 3.5f);
            }
            return;
        }
        m_webClockBootSyncDelayFrames = 120;
    });
    m_settings->onTextEntryRequest(
        [this](const std::string& title, const std::string& guide,
               const std::string& initial, int maxLength,
               TabbedOverlayScreen::TextEntryAcceptCb onAccept) {
            requestTextEntry(title, guide, initial, maxLength, false,
                             std::move(onAccept));
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
        m_navigator.resetToHome();
    });
    m_settings->onSteamGridDbEnabledChange([this](bool enabled) {
        m_config.steamGridDbEnabled = enabled;
        if (m_steamGridDbBackdrop) {
            m_steamGridDbBackdrop->invalidateArtworkCaches();
            m_steamGridDbBackdrop->setEnabled(enabled && steamGridDbArtworkAllowedHere());
            if (enabled) showFocusedSteamGridDbArtwork(true);
            else m_steamGridDbBackdrop->showTitle(0, true);
        }
        syncSteamGridDbLogoArea();
        syncSingleRowPosterPresentation(true);
    });
    m_settings->onSteamGridDbViewFlagsChange([this](bool grid, bool line, bool folders) {
        m_config.steamGridDbShowInGrid = grid;
        m_config.steamGridDbShowInDynamicLine = line;
        m_config.steamGridDbShowInFolders = folders;
        m_config.save();
        if (m_steamGridDbBackdrop) {
            // The focused title may already have been classified as "no
            // artwork" or kept a stale set from before the toggle; drop both so
            // the hero comes back instead of staying blank.
            m_steamGridDbBackdrop->invalidateArtworkCaches();
            m_steamGridDbBackdrop->setEnabled(m_config.steamGridDbEnabled
                                              && steamGridDbArtworkAllowedHere());
        }
        showFocusedSteamGridDbArtwork(true);
    });
    m_settings->onSteamGridDbPosterInLineChange([this](bool enabled) {
        m_config.steamGridDbShowPosterInDynamicLine = enabled;
        m_config.save();
        syncSingleRowPosterPresentation(true);
    });
    m_settings->onSteamGridDbLogoInLineChange([this](bool enabled) {
        m_config.steamGridDbShowLogoInDynamicLine = enabled;
        m_config.save();
        syncSteamGridDbLogoArea();
    });
    m_settings->onSteamGridDbApiKeyRequest([this]() {
        editSteamGridDbApiKey();
    });
    m_settings->onSteamGridDbScrapeRequest([this]() {
        startSteamGridDbScrape();
    });
    m_settings->onControllerPairing([this]() {
        if (m_settings) m_settings->hide();
        m_navigator.resetToHome();
        scheduleLeaveCapture([this]() { m_launcher.launchControllerPairing(); });
    });
    m_settings->onControllerRemapping([this]() {
        if (m_settings) m_settings->hide();
        m_navigator.resetToHome();
        scheduleLeaveCapture([this]() { m_launcher.launchControllerRemapping(); });
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
    m_settings->onDateTimeEditorRequest(
        [this](const TabbedOverlayScreen::DateTimeEditorValue& initial,
               TabbedOverlayScreen::DateTimeCommitCb onCommit) {
            if (!m_dialog) return;
            m_dialogReturnFocus = m_settings.get();
            OverlayDialog::DateTimeValue dialogValue;
            dialogValue.year = initial.year;
            dialogValue.month = initial.month;
            dialogValue.day = initial.day;
            dialogValue.hour = initial.hour;
            dialogValue.minute = initial.minute;
            m_dialog->showDateTimeEditor(
                dialogValue,
                [this, onCommit = std::move(onCommit)](
                    const OverlayDialog::DateTimeValue& value) mutable {
                    TabbedOverlayScreen::DateTimeEditorValue committed;
                    committed.year = value.year;
                    committed.month = value.month;
                    committed.day = value.day;
                    committed.hour = value.hour;
                    committed.minute = value.minute;
                    const bool saved = !onCommit || onCommit(committed);
                    if (saved)
                        m_clockService.invalidate();
                    return saved;
                });
            focusManager().setFocus(m_dialog.get());
        });
    m_settings->onTimezonePickerRequest(
        [this](const std::vector<std::string>& zones, int selectedIndex,
               TabbedOverlayScreen::TimezoneSelectCb onSelect,
               TabbedOverlayScreen::TimezoneSearchCb onSearch) {
            if (!m_dialog) return;
            m_dialogReturnFocus = m_settings.get();
            m_dialog->showTimezonePicker(
                zones,
                selectedIndex,
                [this, onSelect = std::move(onSelect)](const std::string& zone) mutable {
                    if (onSelect) onSelect(zone);
                    m_clockService.invalidate();
                },
                [this, onSearch = std::move(onSearch)]() mutable {
                    if (onSearch) onSearch();
                });
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
        // SteamGridDB toggles / scans may have changed what artwork exists while
        // the overlay owned focus; re-evaluate the hero once the menu is back.
        showFocusedSteamGridDbArtwork(true);
        if (isCurrentFocusableWidget(m_sidebar.settingsButton())) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_sidebar.settingsButton());
        }
    });

}

void WiiUMenuApp::editSteamGridDbApiKey() {
    auto& i18n = nxui::I18n::instance();
    requestTextEntry(i18n.tr("settings.steamgriddb.api_key", "SteamGridDB API key"),
        i18n.tr("settings.steamgriddb.api_key_guide", "Enter your API key"),
        m_config.steamGridDbApiKey, 192, true,
        [this](const std::string& value) {
            m_config.steamGridDbApiKey = value;
            m_config.save();
            if (!m_settings) return;
            m_settings->setSteamGridDbState(m_config.steamGridDbEnabled,
                                             !value.empty(),
                                             m_config.steamGridDbShowInGrid,
                                             m_config.steamGridDbShowInDynamicLine,
                                             m_config.steamGridDbShowInFolders,
                                             m_config.steamGridDbShowPosterInDynamicLine,
                                             m_config.steamGridDbShowLogoInDynamicLine);
            m_settings->refreshCurrentTabWidgets();
            m_settings->requestToast(value.empty()
                ? nxui::I18n::instance().tr("settings.steamgriddb.key_cleared", "API key cleared.")
                : nxui::I18n::instance().tr("settings.steamgriddb.key_saved", "API key saved."));
        });
}

void WiiUMenuApp::createQuickSettings() {
    if (m_quickSettings) return;

    m_quickSettings = std::make_shared<QuickSettingsOverlay>();
    m_quickSettings->setFont(&m_fontNormal);
    m_quickSettings->setSmallFont(&m_fontSmall);
    m_quickSettings->setIconFont(&m_fontIcons);
    m_quickSettings->setTheme(&m_theme);
    m_quickSettings->setInstantCursorMotion(m_config.cursorMotionMode == 1);
    m_quickSettings->setInput(&app().input());

    QuickSettingsOverlay::Callbacks callbacks;
    callbacks.onBgmVolumeChanged = [this](float value) {
        m_config.musicVolume = value;
        m_audio.setVolume(value);
    };
    callbacks.onSfxVolumeChanged = [this](float value) {
        m_config.sfxVolume = value;
        m_audio.setSfxVolume(value);
    };
    callbacks.onMusicPlayPause = [this]() {
        m_audio.togglePlayPause();
        m_config.musicEnabled = m_audio.isPlaying();
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicPrev = [this]() {
        m_audio.prevTrack();
        persistMusicPlaybackState(false);
        m_config.musicEnabled = true;
        m_config.save();
    };
    callbacks.onMusicNext = [this]() {
        m_audio.nextTrack();
        persistMusicPlaybackState(false);
        m_config.musicEnabled = true;
        m_config.save();
    };
    callbacks.onMusicToggleShuffle = [this]() {
        m_audio.setShuffle(!m_audio.shuffle());
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicCycleRepeat = [this]() {
        const int next = (static_cast<int>(m_audio.repeatMode()) + 1) % 3;
        m_audio.setRepeatMode(static_cast<MusicRepeatMode>(next));
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicSelectTrack = [this](int index) {
        m_audio.playTrackAt(index);
        m_config.musicEnabled = true;
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicMoveTrack = [this](int from, int to) {
        if (!m_audio.moveTrack(from, to))
            return;
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicSeek = [this](float seconds) {
        m_audio.seekTo(seconds);
    };
    callbacks.onMusicQueryState = [this]() {
        QuickSettingsOverlay::MusicUiState state;
        state.nowPlaying = m_audio.currentDisplayTitle();
        if (state.nowPlaying.empty())
            state.nowPlaying = nxui::I18n::instance().tr(
                "quicksettings.music_empty", "No music loaded");
        state.playing = m_audio.isPlaying();
        state.shuffle = m_audio.shuffle();
        state.repeat = m_audio.repeatMode();
        state.volume = m_audio.volume();
        state.positionSeconds = m_audio.positionSeconds();
        state.durationSeconds = m_audio.durationSeconds();
        state.currentIndex = m_audio.currentIndex();
        state.hasAlbumFolders = m_audio.hasAlbumFolders();
        state.playlistFilenames = m_audio.playlistFilenames();
        state.playlistOrderKeys = m_audio.playlistOrderKeys();
        state.playlistTitles.reserve(m_audio.tracks().size());
        state.playlistAlbumFolders.reserve(m_audio.tracks().size());
        for (const auto& track : m_audio.tracks()) {
            state.playlistTitles.push_back(track.displayTitle());
            state.playlistAlbumFolders.push_back(track.albumFolder);
        }
        return state;
    };
    callbacks.onMusicQueryCoverArt = [this]() {
        return m_audio.currentCoverArt();
    };
    callbacks.onMusicQueryTrackCover = [this](int index) -> const std::vector<uint8_t>* {
        return m_audio.trackCoverArt(index);
    };
    callbacks.onMusicQueryFolderCover = [this](const std::string& folder) -> const std::vector<uint8_t>* {
        return m_audio.folderCoverArt(folder);
    };
    callbacks.onRenderMenuBackground = [this](nxui::Renderer& ren, float alpha) {
        if (m_background)
            m_background->renderLetterboxOverlay(ren, alpha);
    };
    callbacks.onSleepRequested = [this]() {
        if (!m_dialog) return;
        auto& i18n = nxui::I18n::instance();
        m_dialogReturnFocus = m_quickSettings.get();
        m_dialog->show(i18n.tr("power.title", "Power"),
            i18n.tr("settings.sleep.sleep_confirm", "Put the console into sleep mode?"),
            {{i18n.tr("button.cancel", "Cancel"), []() {}, true},
             {i18n.tr("power.sleep", "Sleep"), [this]() {
#ifdef SWITCHU_MENU
                 m_launcher.enterSleep();
#else
                 app().requestExit();
#endif
             }, true}}, 1, {});
        focusManager().setFocus(m_dialog.get());
    };
    callbacks.onLockRequested = [this]() {
        if (!m_config.lockScreenEnabled)
            return;
        if (m_quickSettings)
            m_quickSettings->hideCursorInstant();
        closeQuickSettings();
        m_audio.playSfx(Sfx::ConfirmPositive);
        enterLockView(false);
    };
    callbacks.onRebootRequested = [this]() {
        if (!m_dialog) return;
        auto& i18n = nxui::I18n::instance();
        m_dialogReturnFocus = m_quickSettings.get();
        m_dialog->show(i18n.tr("power.title", "Power"),
            i18n.tr("settings.sleep.reboot_confirm", "Restart the console?"),
            {{i18n.tr("button.cancel", "Cancel"), []() {}, true},
             {i18n.tr("power.reboot", "Reboot"), [this]() {
#ifdef SWITCHU_MENU
                 m_launcher.reboot();
#else
                 app().requestExit();
#endif
             }, true}}, 1, {});
        focusManager().setFocus(m_dialog.get());
    };
    callbacks.onShutdownRequested = [this]() {
        if (!m_dialog) return;
        auto& i18n = nxui::I18n::instance();
        m_dialogReturnFocus = m_quickSettings.get();
        m_dialog->show(i18n.tr("power.title", "Power"),
            i18n.tr("settings.sleep.shutdown_confirm", "Power off the console?"),
            {{i18n.tr("button.cancel", "Cancel"), []() {}, true},
             {i18n.tr("power.shutdown", "Shutdown"), [this]() {
#ifdef SWITCHU_MENU
                 m_launcher.shutdown();
#else
                 app().requestExit();
#endif
             }, true}}, 1, {});
        focusManager().setFocus(m_dialog.get());
    };
    callbacks.onClose = [this]() { closeQuickSettings(); };
    callbacks.onNavigateSfx = [this]() { m_audio.playSfx(Sfx::Navigate); };
    callbacks.onActivateSfx = [this]() { m_audio.playSfx(Sfx::Activate); };
    callbacks.onToggleOffSfx = [this]() { m_audio.playSfx(Sfx::ToggleOff); };
    callbacks.onBatterySaverToggled = [this](bool enabled) {
        applyBatterySaver(enabled);
    };
    m_quickSettings->setCallbacks(callbacks);
    m_quickSettings->setBatterySaver(m_config.batterySaver);
    m_quickSettings->setLockScreenEnabled(m_config.lockScreenEnabled);
    m_quickSettings->setIconGlyphResolver([this](const char* fileName) {
        return resolveActiveIconGlyphPath(fileName);
    });

    if (m_overlayLayer) {
        m_overlayLayer->addChild(m_quickSettings);
        // Confirmation dialogs and transition overlays must remain above the
        // drawer even though it is created lazily.
        for (const auto& overlay : {std::static_pointer_cast<nxui::Widget>(m_dialog),
                                   std::static_pointer_cast<nxui::Widget>(m_progressDialog),
                                   std::static_pointer_cast<nxui::Widget>(m_launchAnim)}) {
            if (!overlay) continue;
            m_overlayLayer->removeChild(overlay.get());
            m_overlayLayer->addChild(overlay);
        }
    }
}

void WiiUMenuApp::openQuickSettings() {
    createQuickSettings();
    if (!m_quickSettings || m_quickSettings->isActive() || m_editMode ||
        (m_dialog && m_dialog->isActive()) ||
        (m_userSelect && m_userSelect->isActive()))
        return;
    m_dialogReturnFocus = focusManager().current();
    m_quickSettings->setInitialValues(0.5f, m_config.musicVolume,
                                      m_config.sfxVolume, false, true);
    m_quickSettings->setBatteryStatus(m_consoleBatteryPercent,
                                      m_consoleBatteryCharging);
    m_quickSettings->setBatterySaver(m_config.batterySaver);
    m_quickSettings->setFrameTapLiteEngaged(m_frameTapStatus.viewerConnected());
    m_quickSettings->show();
    focusManager().setFocus(m_quickSettings.get());
    if (m_cursor) m_cursor->setVisible(false);
    m_audio.playSfx(Sfx::ModalShow);
}

void WiiUMenuApp::closeQuickSettings() {
    if (!m_quickSettings || !m_quickSettings->isActive()) return;
    m_quickSettings->hide();
    persistMusicPlaybackState(false);
    m_config.save();
    nxui::Widget* target = m_dialogReturnFocus;
    if (!isCurrentFocusableWidget(target) && m_grid)
        target = m_grid->focusManager().current();
    if (isCurrentFocusableWidget(target)) {
        m_suppressNextNavigateSfx = true;
        focusManager().setFocus(target);
    }
    m_dialogReturnFocus = nullptr;
    m_audio.playSfx(Sfx::ModalHide);
}

void WiiUMenuApp::startSteamGridDbScrape() {
    if (m_config.steamGridDbApiKey.empty()) {
        if (m_settings)
            m_settings->requestToast(nxui::I18n::instance().tr(
                "settings.steamgriddb.need_key", "Configure an API key first."));
        return;
    }
    if (m_steamGridDbBrowseFuture.valid() || m_steamGridDbApplyFuture.valid()) {
        if (m_settings)
            m_settings->requestToast(nxui::I18n::instance().tr(
                "settings.steamgriddb.already_running", "A SteamGridDB operation is already running."));
        return;
    }
    // Stamp grid tile sizes so the scrape can pull landscape grids for 2x1/4x2
    // titles even when HOME is currently in single-row (where sizes force 1x1).
    std::vector<AppEntry> scrapeApps = m_allApps;
    for (auto& app : scrapeApps) {
        if (!app.isApplication() || app.titleId == 0)
            continue;
        const auto size = gameGridSize(app.titleId, AppLayoutMode::Grid);
        app.widgetColumns = size.columns;
        app.widgetRows = size.rows;
    }
    if (!m_steamGridDb.start(m_config.steamGridDbApiKey, scrapeApps)) {
        if (m_settings)
            m_settings->requestToast(nxui::I18n::instance().tr(
                "settings.steamgriddb.start_failed", "The artwork scan could not be started."));
        return;
    }
    m_steamGridDbWasRunning = true;
    if (m_progressDialog) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->setCancellable(true, [this]() {
            cancelSteamGridDbScrape();
        });
        raiseProgressDialogAboveOverlays();
        m_progressDialog->show(nxui::I18n::instance().tr(
            "settings.steamgriddb.download_title", "Downloading artwork"),
            nxui::I18n::instance().tr(
                "settings.steamgriddb.download_start", "Searching SteamGridDB..."), 0.f);
        focusManager().setFocus(m_progressDialog.get());
    }
    if (m_settings)
        m_settings->requestToast(nxui::I18n::instance().tr(
            "settings.steamgriddb.started", "SteamGridDB scan started."));
}

void WiiUMenuApp::cancelSteamGridDbScrape() {
    if (!m_steamGridDb.running())
        return;
    m_steamGridDb.requestCancel();
    if (m_progressDialog)
        m_progressDialog->updateState(nxui::I18n::instance().tr(
            "settings.steamgriddb.cancelling", "Cancelling..."), -1.f);
}

void WiiUMenuApp::raiseProgressDialogAboveOverlays() {
    if (!m_overlayLayer || !m_progressDialog)
        return;
    // Picker show() re-parents itself to the top of the overlay stack, which
    // buries the download dialog. Keep progress on top while it is active.
    m_overlayLayer->removeChild(m_progressDialog.get());
    m_overlayLayer->addChild(m_progressDialog);
}

void WiiUMenuApp::cancelSteamGridDbApply() {
    if (!m_steamGridDbApplyProgress)
        return;
    m_steamGridDbApplyProgress->cancelRequested.store(true);
    {
        std::lock_guard<std::mutex> lock(m_steamGridDbApplyProgress->mutex);
        m_steamGridDbApplyProgress->message = nxui::I18n::instance().tr(
            "settings.steamgriddb.cancelling", "Cancelling...");
        m_steamGridDbApplyProgress->progress01 = -1.f;
        ++m_steamGridDbApplyProgress->revision;
    }
    if (m_progressDialog)
        m_progressDialog->updateState(nxui::I18n::instance().tr(
            "settings.steamgriddb.cancelling", "Cancelling..."), -1.f);
}

void WiiUMenuApp::openSteamGridDbPicker(GameOptionsScreen::ArtworkKind kind,
                                        const std::string& requestedQuery) {
    if (!m_gameOptions || m_gameOptionsTitleId == 0) return;
    std::string title;
    std::string defaultQuery;
    for (const auto& appEntry : m_allApps) {
        if (appEntry.titleId == m_gameOptionsTitleId) {
            title = appEntry.title;
            defaultQuery = appEntry.steamGridDbTitle();
            break;
        }
    }
    // Prefer an explicit query, else the per-title saved search, else the
    // English/display title — so Hero/Logo/Icon/Poster share one search.
    std::string query = requestedQuery;
    if (query.empty())
        query = SteamGridDbManager::savedSearchQuery(m_gameOptionsTitleId);
    if (query.empty())
        query = defaultQuery;
    openSteamGridDbPickerForTitle(m_gameOptionsTitleId, title, kind, query);
}

void WiiUMenuApp::openSteamGridDbPickerForTitle(std::uint64_t titleId,
                                                const std::string& title,
                                                GameOptionsScreen::ArtworkKind kind,
                                                const std::string& requestedQuery) {
    if (titleId == 0) return;
    auto toast = [&](const std::string& msg) {
        if (m_gameOptions && m_gameOptions->isActive()) m_gameOptions->requestToast(msg);
        else if (m_folderOptions && m_folderOptions->isActive()) m_folderOptions->requestToast(msg);
        else if (m_settings) m_settings->requestToast(msg);
    };
    if (m_config.steamGridDbApiKey.empty()) {
        toast(nxui::I18n::instance().tr(
            "settings.steamgriddb.need_key", "Configure an API key first."));
        return;
    }
    SteamGridDbManager::ArtworkKind managerKind = SteamGridDbManager::ArtworkKind::Hero;
    if (kind == GameOptionsScreen::ArtworkKind::Logo)
        managerKind = SteamGridDbManager::ArtworkKind::Logo;
    else if (kind == GameOptionsScreen::ArtworkKind::Icon)
        managerKind = SteamGridDbManager::ArtworkKind::Icon;
    else if (kind == GameOptionsScreen::ArtworkKind::Poster)
        managerKind = SteamGridDbManager::ArtworkKind::Poster;
    else if (kind == GameOptionsScreen::ArtworkKind::PosterWide)
        managerKind = SteamGridDbManager::ArtworkKind::GridWide;

    // Apply / scrape still block; an in-flight browse is abandoned so X Search
    // from inside a match folder can always return to a fresh top-level list.
    if (m_steamGridDbApplyFuture.valid() || m_steamGridDb.running()) {
        toast(nxui::I18n::instance().tr(
            "settings.steamgriddb.already_running", "A SteamGridDB operation is already running."));
        return;
    }
    std::string query = requestedQuery;
    if (query.empty())
        query = SteamGridDbManager::savedSearchQuery(titleId);
    if (query.empty())
        query = title;
    if (query.empty()) {
        toast(nxui::I18n::instance().tr(
            "settings.steamgriddb.need_query", "Enter a search name first."));
        return;
    }
    SteamGridDbManager::saveSearchQuery(titleId, query);
    ++m_steamGridDbBrowseGeneration;
    m_steamGridDbBrowseFutureGeneration = m_steamGridDbBrowseGeneration;
    // Park any in-flight browse so X Search from a match folder can start a
    // fresh top-level list without blocking the UI thread on the old request.
    if (m_steamGridDbBrowseFuture.valid())
        m_steamGridDbRetiredBrowses.push_back(std::move(m_steamGridDbBrowseFuture));
    m_gameOptionsTitleId = titleId;
    if (m_overlayLayer && m_steamGridDbPicker) {
        m_overlayLayer->removeChild(m_steamGridDbPicker.get());
        m_overlayLayer->addChild(m_steamGridDbPicker);
    }
    // Top-level browse — never preserve a drilled-in game folder.
    m_steamGridDbPicker->showLoading(titleId, title, query, managerKind, false);
    focusManager().setFocus(m_steamGridDbPicker.get());
    m_steamGridDbBrowseFuture = std::async(std::launch::async,
        [apiKey = m_config.steamGridDbApiKey, titleId, title, query, managerKind]() {
            return SteamGridDbManager::browse(apiKey, titleId, title, query, managerKind);
        });
}

void WiiUMenuApp::clearSteamGridDbArtwork(std::uint64_t titleId,
                                          GameOptionsScreen::ArtworkKind kind) {
    if (titleId == 0) return;
    SteamGridDbManager::ArtworkKind managerKind = SteamGridDbManager::ArtworkKind::Hero;
    if (kind == GameOptionsScreen::ArtworkKind::Logo)
        managerKind = SteamGridDbManager::ArtworkKind::Logo;
    else if (kind == GameOptionsScreen::ArtworkKind::Icon)
        managerKind = SteamGridDbManager::ArtworkKind::Icon;
    else if (kind == GameOptionsScreen::ArtworkKind::Poster)
        managerKind = SteamGridDbManager::ArtworkKind::Poster;
    else if (kind == GameOptionsScreen::ArtworkKind::PosterWide)
        managerKind = SteamGridDbManager::ArtworkKind::GridWide;

    const bool removed = SteamGridDbManager::clearArtwork(titleId, managerKind);
    auto& i18n = nxui::I18n::instance();
    const std::string msg = removed
        ? i18n.tr("game.steamgriddb.cleared", "SteamGridDB artwork cleared.")
        : i18n.tr("game.steamgriddb.nothing_to_clear", "No SteamGridDB artwork to clear.");

    if ((managerKind == SteamGridDbManager::ArtworkKind::Icon
         || managerKind == SteamGridDbManager::ArtworkKind::Poster) && m_grid) {
        m_folderCoverCache.erase(titleId);
        m_folderPeekCache.erase(titleId);
        m_folderPeekBorrowed.erase(titleId);
        if (titleId >= kFolderTitleIdPrefix)
            invalidateFolderPeekComposite(static_cast<std::uint32_t>(
                titleId & 0xFFFFFFFFu));
        else
            invalidateFolderPeekCompositesForTitle(titleId);
        const bool preferPoster = preferSingleRowPosters();
        const bool isFolderArt = titleId >= kFolderTitleIdPrefix;
        if (managerKind == SteamGridDbManager::ArtworkKind::Poster) {
            for (const auto& icon : m_grid->allIcons()) {
                if (!icon || icon->titleId() != titleId) continue;
                // Folders stay tall in single-row poster mode even without art.
                if (icon->entryKind() == GridEntryKind::Folder)
                    icon->setPosterFrame(preferPoster);
                else
                    icon->setPosterFrame(false);
            }
            m_grid->invalidateLineLayout();
        }
        if (!isFolderArt) {
            m_iconStreamer.reloadTitle(titleId, m_grid->currentPage(),
                                       m_grid->iconsPerPage(),
                                       app().gpu(), app().renderer(), m_grid->allIcons());
        }
        applyFolderCoversToIcons();
        // Sync-load the post-clear icon so Game Options header updates immediately
        // instead of waiting for the async grid streamer (same approach as folders).
        if (m_gameOptions && m_gameOptions->isActive()
            && titleId < kFolderTitleIdPrefix)
            m_gameOptions->setGameIcon(folderCoverTexture(titleId));
        else if (m_gameOptions)
            m_gameOptions->setGameIcon(nullptr);
    }
    if (managerKind == SteamGridDbManager::ArtworkKind::GridWide
        || managerKind == SteamGridDbManager::ArtworkKind::Hero
        || managerKind == SteamGridDbManager::ArtworkKind::Logo) {
        m_gameArtwork.erase(titleId);
        const auto size = gameGridSize(titleId, AppLayoutMode::Grid);
        if (switchu::widgets::isLandscapeWideGameTile(size))
            ensureGameArtwork(titleId);
    }
    showFocusedSteamGridDbArtwork(true);
    m_audio.playSfx(removed ? Sfx::ConfirmPositive : Sfx::ModalHide);
    if (m_gameOptions && m_gameOptions->isActive()) {
        std::error_code ec;
        const bool hasHero = std::filesystem::exists(
            SteamGridDbManager::heroPath(titleId), ec);
        ec.clear();
        const bool hasLogo = std::filesystem::exists(
            SteamGridDbManager::logoPath(titleId), ec);
        m_gameOptions->refreshArtworkPresence(
            hasHero, hasLogo, SteamGridDbManager::hasIcon(titleId),
            SteamGridDbManager::hasPoster(titleId),
            SteamGridDbManager::hasGridWide(titleId));
        m_gameOptions->requestToast(msg, 2.6f);
    } else if (m_folderOptions && m_folderOptions->isActive())
        m_folderOptions->requestToast(msg, 2.6f);
}

void WiiUMenuApp::editSteamGridDbPickerQuery() {
    if (!m_steamGridDbPicker || !m_steamGridDbPicker->isActive()) return;
    const auto kind = m_steamGridDbPicker->artworkKind();
    const auto mappedKind = kind == SteamGridDbManager::ArtworkKind::Logo
        ? GameOptionsScreen::ArtworkKind::Logo
        : kind == SteamGridDbManager::ArtworkKind::Icon
            ? GameOptionsScreen::ArtworkKind::Icon
            : kind == SteamGridDbManager::ArtworkKind::Poster
                ? GameOptionsScreen::ArtworkKind::Poster
                : kind == SteamGridDbManager::ArtworkKind::GridWide
                    ? GameOptionsScreen::ArtworkKind::PosterWide
                    : GameOptionsScreen::ArtworkKind::Hero;
    const std::uint64_t titleId = m_steamGridDbPicker->titleId();
    const std::string title = m_steamGridDbPicker->title();
    // Prefer the picker's live search string (survives match-folder drill-in).
    const std::string initial = !m_steamGridDbPicker->query().empty()
        ? m_steamGridDbPicker->query()
        : SteamGridDbManager::savedSearchQuery(titleId);
    requestTextEntry("SteamGridDB", "Search name", initial,
        128, false, [this, mappedKind, titleId, title](const std::string& value) {
            if (value.empty() || titleId == 0)
                return;
            SteamGridDbManager::saveSearchQuery(titleId, value);
            // Always top-level browse — exits any match folder and refreshes the list.
            openSteamGridDbPickerForTitle(titleId, title, mappedKind, value);
        });
}

void WiiUMenuApp::applySteamGridDbCandidate(
    const SteamGridDbManager::BrowseResult& browse,
    const SteamGridDbManager::Candidate& candidate) {
    if (m_steamGridDbApplyFuture.valid() || m_steamGridDb.running()) return;
    auto progress = std::make_shared<SteamGridDbApplyProgressShared>();
    progress->message = nxui::I18n::instance().tr(
        "settings.steamgriddb.download_start", "Starting download...");
    m_steamGridDbApplyProgress = progress;
    m_steamGridDbApplyProgressUiRevision = 0;
    if (m_progressDialog) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->setCancellable(true, [this]() {
            cancelSteamGridDbApply();
        });
        raiseProgressDialogAboveOverlays();
        m_progressDialog->show(nxui::I18n::instance().tr(
            "settings.steamgriddb.download_title", "Downloading artwork"),
            progress->message, 0.f);
        focusManager().setFocus(m_progressDialog.get());
    }
    m_steamGridDbApplyFuture = std::async(std::launch::async, [browse, candidate, progress]() {
        return SteamGridDbManager::applyCandidate(
            browse, candidate,
            [progress](const std::string& message, float value) {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->message = message;
                progress->progress01 = value;
                ++progress->revision;
            },
            [progress]() {
                return progress->cancelRequested.load();
            });
    });
}

void WiiUMenuApp::syncSteamGridDb() {
    for (auto it = m_steamGridDbRetiredBrowses.begin();
         it != m_steamGridDbRetiredBrowses.end();) {
        if (it->valid()
            && it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it->get(); // discard abandoned browse
            it = m_steamGridDbRetiredBrowses.erase(it);
        } else {
            ++it;
        }
    }
    if (m_steamGridDbBrowseFuture.valid()
        && m_steamGridDbBrowseFuture.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        const std::uint64_t futureGen = m_steamGridDbBrowseFutureGeneration;
        auto result = m_steamGridDbBrowseFuture.get();
        // Abandon results from a browse that X Search already replaced.
        if (futureGen != m_steamGridDbBrowseGeneration)
            return;
        if (m_steamGridDbPicker && m_steamGridDbPicker->isActive()
            && m_steamGridDbPicker->titleId() == result.titleId) {
            if (result.query.empty())
                result.query = m_steamGridDbPicker->query();
            m_steamGridDbPicker->setResult(std::move(result));
            focusManager().setFocus(m_steamGridDbPicker.get());
        }
    }
    if (m_steamGridDbApplyProgress) {
        std::string message;
        float progress01 = 0.f;
        std::uint64_t revision = 0;
        {
            std::lock_guard<std::mutex> lock(m_steamGridDbApplyProgress->mutex);
            message = m_steamGridDbApplyProgress->message;
            progress01 = m_steamGridDbApplyProgress->progress01;
            revision = m_steamGridDbApplyProgress->revision;
        }
        if (revision != m_steamGridDbApplyProgressUiRevision && m_progressDialog) {
            m_steamGridDbApplyProgressUiRevision = revision;
            m_progressDialog->updateState(message, progress01);
        }
        if (m_progressDialog && m_progressDialog->isActive()) {
            raiseProgressDialogAboveOverlays();
            if (focusManager().current() != m_progressDialog.get())
                focusManager().setFocus(m_progressDialog.get());
        }
    }
    if (m_steamGridDbApplyFuture.valid()
        && m_steamGridDbApplyFuture.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        const auto result = m_steamGridDbApplyFuture.get();
        if (result.success) {
            // Wide tiles and recent widgets can still be referenced by frames
            // already submitted to deko3d. Drain them before destroying their
            // old textures after a manual artwork replacement.
            DebugLog::log("[steamgriddb-ui] draining GPU before replacing artwork title=%016lX",
                          static_cast<unsigned long>(result.titleId));
            app().gpu().waitIdle();
#ifdef NXUI_BACKEND_DEKO3D
            app().renderer().reclaimReleasedTextureSlotsAfterIdle();
#endif
            m_gameArtwork.erase(result.titleId);
            if (m_recentWidgetAssetTitleId == result.titleId) {
                m_recentWidgetAssetTitleId = 0;
                m_recentWidgetHero.reset();
                m_recentWidgetLogo.reset();
                m_recentWidgetIcon.reset();
            }
            if ((result.kind == SteamGridDbManager::ArtworkKind::Icon
                 || result.kind == SteamGridDbManager::ArtworkKind::Poster) && m_grid) {
                m_folderCoverCache.erase(result.titleId);
                m_folderPeekCache.erase(result.titleId);
                m_folderPeekBorrowed.erase(result.titleId);
                const bool preferPoster = preferSingleRowPosters();
                const bool isFolderArt = result.titleId >= kFolderTitleIdPrefix;
                for (const auto& icon : m_grid->allIcons()) {
                    if (!icon || icon->titleId() != result.titleId) continue;
                    if (icon->entryKind() == GridEntryKind::Folder)
                        icon->setPosterFrame(preferPoster);
                    else if (icon->entryKind() == GridEntryKind::Application)
                        icon->setPosterFrame(
                            preferPoster
                            && SteamGridDbManager::hasPoster(result.titleId));
                    break;
                }
                m_grid->invalidateLineLayout();
                if (!isFolderArt) {
                    m_iconStreamer.reloadTitle(result.titleId, m_grid->currentPage(),
                                               m_grid->iconsPerPage(), app().gpu(),
                                               app().renderer(), m_grid->allIcons());
                }
                // Folder custom icon/poster: refresh cover tiles (+ enable Show
                // cover when applying an icon so grid/cover styles can use it).
                if (isFolderArt) {
                    if (result.kind == SteamGridDbManager::ArtworkKind::Icon
                        && !m_config.folderShowCover) {
                        m_config.folderShowCover = true;
                        if (m_configSaveFuture.valid())
                            m_configSaveFuture.wait();
                        m_configSaveFuture = m_threadPool.submit([config = m_config]() {
                            config.save();
                        });
                    }
                    if (m_openFolderId == 0)
                        applyDisplayModel(buildRootFolderModel(), result.titleId, false);
                    else
                        applyFolderCoversToIcons();
                    if (m_folderOptions && m_folderOptions->isActive()
                        && m_folderOptionsId != 0) {
                        if (const auto* folder = m_folderStore.find(m_folderOptionsId)) {
                            FolderOptionsScreen::FolderInfo info;
                            info.id = folder->id;
                            info.name = folder->name;
                            info.itemCount = static_cast<int>(folder->titleCount());
                            info.colorIndex = folder->colorIndex;
                            info.sizeIndex = folder->sizeIndex;
                            info.styleIndex = m_config.folderStyle;
                            info.defaultViewIndex =
                                std::clamp(m_config.folderDefaultView, 0, 2);
                            info.showCover = m_config.folderShowCover;
                            info.hasCustomIcon =
                                SteamGridDbManager::hasIcon(result.titleId);
                            info.hasCustomPoster =
                                SteamGridDbManager::hasPoster(result.titleId);
                            const auto coverId = resolveFolderCoverTitleId(
                                *folder, preferPoster);
                            info.cover = folderCoverTexture(coverId, preferPoster);
                            m_folderOptions->setFolder(info);
                        }
                    }
                } else {
                    invalidateFolderPeekCompositesForTitle(result.titleId);
                    applyFolderCoversToIcons();
                }
            } else {
                showFocusedSteamGridDbArtwork(true);
            }
            if (result.kind == SteamGridDbManager::ArtworkKind::GridWide
                || result.kind == SteamGridDbManager::ArtworkKind::Hero
                || result.kind == SteamGridDbManager::ArtworkKind::Logo) {
                m_gameArtwork.erase(result.titleId);
                const auto size = gameGridSize(result.titleId, AppLayoutMode::Grid);
                if (switchu::widgets::isLandscapeWideGameTile(size))
                    ensureGameArtwork(result.titleId);
            }
            if (m_grid && m_openFolderId == 0 &&
                gameGridSize(result.titleId, AppLayoutMode::Grid) !=
                    switchu::widgets::WidgetSize{1, 1})
                applyDisplayModel(buildRootFolderModel(), result.titleId, false);
            if (m_gameOptions && m_gameOptions->isActive()
                && m_gameOptionsTitleId == result.titleId) {
                std::error_code ec;
                const bool hasHero = std::filesystem::exists(
                    SteamGridDbManager::heroPath(result.titleId), ec);
                ec.clear();
                const bool hasLogo = std::filesystem::exists(
                    SteamGridDbManager::logoPath(result.titleId), ec);
                m_gameOptions->refreshArtworkPresence(
                    hasHero, hasLogo, SteamGridDbManager::hasIcon(result.titleId),
                    SteamGridDbManager::hasPoster(result.titleId),
                    SteamGridDbManager::hasGridWide(result.titleId));
                if (result.kind == SteamGridDbManager::ArtworkKind::Icon
                    && result.titleId < kFolderTitleIdPrefix) {
                    // Load from disk immediately - grid reloadTitle is async and
                    // still holds the previous texture when we get here.
                    m_gameOptions->setGameIcon(folderCoverTexture(result.titleId));
                }
            }
        }
        if (m_progressDialog) m_progressDialog->hide();
        m_steamGridDbApplyProgress.reset();
        m_steamGridDbApplyProgressUiRevision = 0;
        auto toast = [&](const std::string& msg) {
            if (m_gameOptions && m_gameOptions->isActive())
                m_gameOptions->requestToast(msg, 2.6f);
            else if (m_folderOptions && m_folderOptions->isActive())
                m_folderOptions->requestToast(msg, 2.6f);
            else if (m_settings) m_settings->requestToast(msg, 2.6f);
        };
        if (result.success) {
            // Close the picker after a successful apply so the user lands back
            // on game/folder options with the new artwork visible.
            if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
                m_steamGridDbPicker->hide();
            if (!result.message.empty()) toast(result.message);
        } else if (result.cancelled) {
            toast(nxui::I18n::instance().tr(
                "settings.steamgriddb.cancelled", "Artwork download cancelled."));
            if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
                focusManager().setFocus(m_steamGridDbPicker.get());
        } else if (m_steamGridDbPicker && m_steamGridDbPicker->isActive()) {
            m_steamGridDbPicker->setMessage(result.message, false);
            focusManager().setFocus(m_steamGridDbPicker.get());
        }
    }

    const auto state = m_steamGridDb.status();
    if (state.revision != m_steamGridDbUiRevision) {
        m_steamGridDbUiRevision = state.revision;
        if (m_settings) {
            m_settings->setSteamGridDbProgress(
                state.running, state.finished, state.completed, state.total,
                state.matched, state.failed, state.currentTitle, state.message);
        }
        if (state.running && m_steamGridDbWasRunning && m_progressDialog) {
            m_progressDialog->updateState(
                state.message.empty() ? state.currentTitle : state.message,
                state.progress01);
            if (focusManager().current() != m_progressDialog.get())
                focusManager().setFocus(m_progressDialog.get());
        }
        if (state.lastCompletedTitleId != 0
            && state.lastCompletedTitleId != m_steamGridDbLastCompletedTitleId) {
            m_steamGridDbLastCompletedTitleId = state.lastCompletedTitleId;
            if (m_grid && m_grid->focusManager().current()) {
                auto* focused = m_grid->focusManager().current();
                if (focused->tag() == "glossy_icon"
                    && static_cast<GlossyIcon*>(focused)->titleId() == state.lastCompletedTitleId) {
                    showFocusedSteamGridDbArtwork(true);
                }
            }
        }
    }

    if (m_steamGridDbWasRunning && !state.running && state.finished) {
        m_steamGridDbWasRunning = false;
        if (m_progressDialog) m_progressDialog->hide();
        // The scan wrote new files under steamgriddb/; drop the backdrop caches
        // so the focused hero and the prefetch ring pick them up immediately
        // instead of serving a set (or a "no artwork" verdict) from before.
        if (m_steamGridDbBackdrop)
            m_steamGridDbBackdrop->invalidateArtworkCaches();
        showFocusedSteamGridDbArtwork(true);
        const bool preferPosterTiles =
            m_appLayoutMode == AppLayoutMode::DynamicLine
            && m_config.steamGridDbEnabled
            && m_config.steamGridDbShowPosterInDynamicLine;
        if (state.matched > 0 && m_grid
            && (state.selectedKind == SteamGridDbManager::ArtworkKind::Icon
                || state.selectedKind == SteamGridDbManager::ArtworkKind::Poster
                || state.selectedKind == SteamGridDbManager::ArtworkKind::GridWide
                || preferPosterTiles)) {
            // forceReload releases the pool texture used by the panel header;
            // detach it first so the overlay never renders a stale pointer.
            if (m_gameOptions) m_gameOptions->setGameIcon(nullptr);
            // Also re-mark per-tile Poster frames so new poster.img files get
            // tall cells immediately (without a grid ↔ single-row toggle).
            syncSingleRowPosterPresentation(true);
        }
        if (state.matched > 0 && m_grid) {
            for (const auto& icon : m_grid->allIcons()) {
                if (!icon || icon->entryKind() != GridEntryKind::Application)
                    continue;
                const auto tid = icon->titleId();
                const int cols = icon->gridSpanColumns();
                const int rows = icon->gridSpanRows();
                if (switchu::widgets::isLandscapeWideGameTile(cols, rows)) {
                    m_gameArtwork.erase(tid);
                    ensureGameArtwork(tid);
                } else if (preferPosterIconData(tid) && !preferPosterTiles) {
                    m_iconStreamer.reloadTitle(tid, m_grid->currentPage(),
                                               m_grid->iconsPerPage(),
                                               app().gpu(), app().renderer(),
                                               m_grid->allIcons());
                }
            }
        }
        if (state.selectedKind != SteamGridDbManager::ArtworkKind::None
            && m_gameOptions && m_gameOptions->isActive()) {
            m_gameOptions->requestToast(state.message, 3.2f);
        }
        if (m_settings && m_settings->isActive()) {
            const bool cancelled = state.message.find("cancelled") != std::string::npos;
            m_settings->requestToast(
                cancelled
                    ? nxui::I18n::instance().tr("settings.steamgriddb.scan_cancelled",
                                                "Artwork scan cancelled.")
                    : (std::to_string(state.matched) + " artwork sets found, "
                       + std::to_string(state.failed) + " missing."),
                3.2f);
            focusManager().setFocus(m_settings.get());
        }
    }
}

AppLayoutMode WiiUMenuApp::presentationLayoutMode() const {
    // During prefetch the logical mode is already single-row, but tiles and
    // hero must keep the previous arrangement until the morph commits.
    if (m_layoutTransition.kind == LayoutTransitionKind::PrefetchThenMorph)
        return m_layoutTransition.visualMode;
    if (m_grid)
        return m_grid->layoutMode();
    return m_appLayoutMode;
}

bool WiiUMenuApp::steamGridDbArtworkAllowedHere() const {
    if (!m_config.steamGridDbEnabled)
        return false;
    if (m_openFolderId != 0)
        return m_config.steamGridDbShowInFolders;
    if (presentationLayoutMode() == AppLayoutMode::DynamicLine)
        return m_config.steamGridDbShowInDynamicLine;
    return m_config.steamGridDbShowInGrid;
}

void WiiUMenuApp::showFocusedSteamGridDbArtwork(bool forceReload) {
    if (!m_steamGridDbBackdrop) return;
    // FT lite / NeedHeadroom hold: heroes (~8 MiB) stay down so posters can
    // keep a wider radius. All call sites (splash handoff, morph, focus) hit
    // this — do not reload art while light is active.
    if (m_frameTapLightActive || m_ftHeadroomHoldActive) {
        m_steamGridDbBackdrop->setPreloadPaused(true);
        m_steamGridDbBackdrop->releaseGpuArtwork();
        m_steamGridDbBackdrop->setEnabled(false);
        m_steamGridDbBackdrop->showTitle(0, true);
        syncHeroBackdropPresentation();
        return;
    }
    if (!steamGridDbArtworkAllowedHere()) {
        m_steamGridDbBackdrop->setEnabled(false);
        m_steamGridDbBackdrop->showTitle(0, forceReload);
        syncHeroBackdropPresentation();
        return;
    }
    m_steamGridDbBackdrop->setEnabled(true);
    m_steamGridDbBackdrop->setLayoutMode(presentationLayoutMode());

    std::uint64_t titleId = 0;
    std::vector<std::uint64_t> nearbyTitleIds;
    // While carrying a title, keep its hero — hopping focus across slots for
    // placement would otherwise thrash hero loads and lag the folder.
    if (m_editMode && m_editHeldTitleId != 0
        && m_editHeldTitleId < kFolderTitleIdPrefix
        && !switchu::widgets::isWidgetTitleId(m_editHeldTitleId)) {
        titleId = m_editHeldTitleId;
    } else if (m_grid) {
        auto* current = m_grid->focusManager().current();
        if (current && current->tag() == "glossy_icon") {
            const int focused = m_grid->focusedGlobalIndex();
            if (focused >= 0 && focused < m_model.count()
                && m_model.at(focused).isApplication())
                titleId = static_cast<GlossyIcon*>(current)->titleId();
        }
        const int focused = m_grid->focusedGlobalIndex();
        for (int distance = 1; focused >= 0 && nearbyTitleIds.size() < 8
                               && distance < m_model.count(); ++distance) {
            for (int direction : {1, -1}) {
                const int index = focused + direction * distance;
                if (index < 0 || index >= m_model.count()) continue;
                const auto& entry = m_model.at(index);
                if (!entry.isApplication() || entry.titleId == 0) continue;
                nearbyTitleIds.push_back(entry.titleId);
                if (nearbyTitleIds.size() >= 8) break;
            }
        }
    }
    // Zero is meaningful: it clears artwork when focus moves to a folder,
    // an empty slot or any non-application widget.
    m_steamGridDbBackdrop->showTitle(titleId, forceReload);
    // After layout morph, skip neighbour hero/logo warm for a beat so HOME
    // stays responsive; focused title still updates above.
    if (m_deferBackdropPreloadSec > 0.f)
        nearbyTitleIds.clear();
    m_steamGridDbBackdrop->setPreloadTitles(std::move(nearbyTitleIds));
    syncSteamGridDbLogoArea();
    updateHeroAwareChrome();
}

void WiiUMenuApp::createGameOptions() {
    if (m_gameOptions) return;
    m_gameOptions = std::make_shared<GameOptionsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_gameOptions);
    m_gameOptions->setFont(&m_fontNormal);
    m_gameOptions->setSmallFont(&m_fontSmall);
    m_gameOptions->setTheme(&m_theme);
    m_gameOptions->setInstantCursorMotion(m_config.cursorMotionMode == 1);
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
    m_folderOptions->setInstantCursorMotion(m_config.cursorMotionMode == 1);
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

void WiiUMenuApp::createAutoThemeScreen() {
    if (m_autoThemeScreen) return;
    m_autoThemeScreen = std::make_shared<AutoThemeScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_autoThemeScreen);
    m_autoThemeScreen->setFont(&m_fontNormal);
    m_autoThemeScreen->setSmallFont(&m_fontSmall);
    m_autoThemeScreen->setTheme(&m_theme);
    m_autoThemeScreen->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_autoThemeScreen->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                         m_config.accessibilitySpeakPosition);
    m_autoThemeScreen->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_autoThemeScreen->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_autoThemeScreen->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_autoThemeScreen->onToggleSfx([this](bool on) {
        m_audio.playSfx(on ? Sfx::ThemeToggle : Sfx::ToggleOff);
    });
    m_autoThemeScreen->onSliderSfx([this](bool up) {
        m_audio.playSfx(up ? Sfx::SliderUp : Sfx::SliderDown);
    });
    m_autoThemeScreen->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_autoThemeScreen->onAccessibilityStructuredAnnouncement(
        [this](const std::string& context, const std::string& position,
               const std::string& summary, bool forceRepeat, bool forceContext) {
            m_accessibility.announceStructuredFocus(context, position, summary,
                                                    forceRepeat, forceContext);
        });
    m_autoThemeScreen->onChanged([this](const AutoThemeScreen::State& st) {
        m_config.autoThemeMode = st.mode == 1 ? "manual"
                               : (st.mode == 2 ? "geo" : "off");
        m_config.autoThemeDayPreset = st.dayPreset;
        m_config.autoThemeNightPreset = st.nightPreset;
        m_config.autoThemeDayStartHour = std::clamp(st.dayStartHour, 0, 23);
        m_config.autoThemeNightStartHour = std::clamp(st.nightStartHour, 0, 23);
        applyAutoThemeConfig();
        maybeFetchGeoLocation();
        evaluateAutoTheme(true);
        refreshAutoThemeSummary();
        pushGeoDisplayToScreen();
    });
    m_autoThemeScreen->onClosed([this]() {
        if (m_configSaveFuture.valid())
            m_configSaveFuture.wait();
        m_configSaveFuture = m_threadPool.submit([cfg = m_config]() { cfg.save(); });
        DebugLog::log("[config] save queued");
        // Return to the Theme Shop it was opened from.
        if (m_themeShop && m_themeShop->isActive()) {
            m_navigator.navigate(switchu::navigation::Route::ThemeShop);
            m_themeShop->rebuildCurrentTab();
            focusManager().setFocus(m_themeShop.get());
        } else {
            m_navigator.routeDidClose(switchu::navigation::Route::AutoTheme);
        }
    });
}

void WiiUMenuApp::openAutoThemeSettings() {
    createAutoThemeScreen();
    if (!m_autoThemeScreen) return;

    std::vector<AutoThemeScreen::PresetOption> presets;
    presets.reserve(m_allPresets.size());
    for (const auto& p : m_allPresets) {
        AutoThemeScreen::PresetOption opt;
        opt.id = p.id.empty() ? p.name : p.id;
        opt.name = p.name;
        presets.push_back(std::move(opt));
    }

    AutoThemeScreen::State st;
    st.mode = m_config.autoThemeMode == "manual" ? 1
            : (m_config.autoThemeMode == "geo" ? 2 : 0);
    st.dayPreset = m_config.autoThemeDayPreset;
    st.nightPreset = m_config.autoThemeNightPreset;
    st.dayStartHour = m_config.autoThemeDayStartHour;
    st.nightStartHour = m_config.autoThemeNightStartHour;
    st.geoResolved = m_config.autoThemeGeoResolved;
    st.geoCity = m_config.autoThemeGeoResolved
        ? (m_config.autoThemeGeoCity.empty()
               ? fmt::format("{:.2f}, {:.2f}", m_config.autoThemeGeoLat, m_config.autoThemeGeoLon)
               : m_config.autoThemeGeoCity)
        : std::string();

    m_autoThemeScreen->setTheme(&m_theme);
    m_autoThemeScreen->configure(st, std::move(presets));
    pushGeoDisplayToScreen(); // fill in computed sunrise/sunset

    m_navigator.navigate(switchu::navigation::Route::AutoTheme);
    m_autoThemeScreen->show();
    focusManager().setFocus(m_autoThemeScreen.get());
    m_audio.playSfx(Sfx::ModalShow);
}

void WiiUMenuApp::refreshAutoThemeSummary() {
    if (!m_themeShop) return;
    auto& i18n = nxui::I18n::instance();
    std::string summary;
    if (m_config.autoThemeMode == "manual") {
        summary = fmt::format("{} ({:02d}:00 / {:02d}:00)",
                              i18n.tr("autotheme.summary.manual", "Manual hours"),
                              std::clamp(m_config.autoThemeDayStartHour, 0, 23),
                              std::clamp(m_config.autoThemeNightStartHour, 0, 23));
    } else if (m_config.autoThemeMode == "geo") {
        summary = i18n.tr("autotheme.summary.geo", "Geolocation (IP)");
    } else {
        summary = i18n.tr("autotheme.summary.off", "Off");
    }
    m_themeShop->setAutoThemeSummary(summary);
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

void WiiUMenuApp::createProfileCarousel() {
    if (m_profileCarousel) return;
    m_profileCarousel = std::make_shared<ProfileCarouselScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_profileCarousel);
    m_profileCarousel->setFont(&m_fontNormal);
    m_profileCarousel->setSmallFont(&m_fontSmall);
    m_profileCarousel->setTheme(&m_theme);
    m_profileCarousel->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_profileCarousel->onLockSfx([this]() { m_audio.playSfx(Sfx::ConfirmPositive); });
    m_profileCarousel->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_profileCarousel->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_profileCarousel->onLockProfile([this](std::optional<AccountUid> uid) {
        setLockedProfile(uid);
    });
    m_profileCarousel->onOpenProfile([this](AccountUid uid) {
        m_audio.playSfx(Sfx::Activate);
        m_profileCarousel->hide();
#ifdef SWITCHU_MENU
        // The leave capture waits for the carousel to finish fading out.
        scheduleLeaveCapture([this, uid]() { m_launcher.launchUserPage(uid); });
#else
        (void)uid;
#endif
    });
    m_profileCarousel->onAddUser([this]() {
        m_audio.playSfx(Sfx::Activate);
        m_profileCarousel->hide();
#ifdef SWITCHU_MENU
        scheduleLeaveCapture([this]() { m_launcher.launchUserCreator(); });
#endif
    });
    m_profileCarousel->onClosed([this]() {
        if (m_navigator.route() != switchu::navigation::Route::ProfileSelect)
            return;
        m_navigator.resetToHome();
        if (m_profileLock)
            focusManager().setFocus(m_profileLock.get());
    });
}

void WiiUMenuApp::openProfileCarousel() {
    createProfileCarousel();
    if (!m_profileCarousel || m_profileCarousel->isActive())
        return;
    refreshProfileLockButton();
    m_navigator.navigate(switchu::navigation::Route::ProfileSelect);
    m_profileCarousel->show();
    focusManager().setFocus(m_profileCarousel.get());
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

        // Finish any in-flight uninstall before we decide "already installed".
        if (m_themeDeleteFuture.valid()) {
            m_themeDeleteFuture.wait();
            m_themeDeleteFuture = {};
        }

        // Only a real package (theme.json) counts. Empty/orphan folders left by a
        // failed async delete used to trigger a bogus "Replace Installed Theme".
        const bool packagePresent = pathExists(joinPath(destination, "theme.json"));
        if (!packagePresent) {
            if (pathExists(destination)) {
                const bool cleaned = removeDirectoryRecursive(destination);
                DebugLog::log("[themeshop] cleared orphan theme folder before install: %s (%s)",
                              safeLogPath(destination), cleaned ? "ok" : "failed");
            }
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
    m_themeShop->setInstantCursorMotion(m_config.cursorMotionMode == 1);
    m_themeShop->setThreadPool(&m_threadPool);
    m_themeShop->setRenderContext(&app().gpu(), &app().renderer());
    m_themeShop->setMusicState(m_audio.isPlaying(), m_audio.volume(), m_audio.sfxVolume());
    m_themeShop->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_themeShop->setActionHintStyleState(m_config.actionHintStyle);
    m_themeShop->setSortLargeTilesState(m_config.sortLargeTiles);
    m_themeShop->setBatterySaverState(m_config.batterySaver);
    m_themeShop->setLockScreenState(m_config.lockScreenEnabled);
    m_themeShop->setPowerButtonAction(m_config.powerButtonAction);
    m_themeShop->setLockScreenClockFontState(m_config.lockScreenClockFont);
    m_themeShop->setLockClockFontContext(
        SD_ASSETS,
        m_loadedRegularFontPath.empty()
            ? std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf"
            : m_loadedRegularFontPath);
    refreshLockClockFontEntries();
    m_themeShop->setFrameTapLiteEngaged(m_frameTapStatus.viewerConnected());
    m_themeShop->setIconContrastAdaptState(m_config.iconContrastAdapt);
    m_themeShop->setLockScreenTransitionState(m_config.lockScreenTransition);
    m_themeShop->setLockScreenZoomDirState(m_config.lockScreenZoomDir);
    m_themeShop->setLockScreenWipeDirState(m_config.lockScreenWipeDir);
    m_themeShop->setThemeLockClockFontOverrideState(m_config.themeLockClockFontOverride);
    m_themeShop->setThemeIconPackOverrideState(m_config.themeIconPackOverride);
    m_themeShop->setThemeMusicOverrideState(m_config.themeMusicOverride);
    m_themeShop->setThemeMusicAddToPlaylistState(m_config.themeMusicAddToPlaylist);
    m_themeShop->setThemeMusicPlayOnApplyState(m_config.themeMusicPlayOnApply);
    m_themeShop->setThemeMatchAccent(m_activeColors.accentH,
                                     m_activeColors.accentS,
                                     m_activeColors.accentL);
    {
        std::unordered_map<std::string, ThemeShopScreen::IconPackTintState> tints;
        for (const auto& [packId, tint] : m_config.iconPackTints) {
            ThemeShopScreen::IconPackTintState state;
            state.enabled = tint.enabled;
            state.flatten = tint.flatten;
            state.grayscale = tint.grayscale;
            state.matchTheme = tint.matchTheme;
            state.hue = tint.hue;
            state.sat = tint.sat;
            state.light = tint.light;
            tints[packId] = state;
        }
        m_themeShop->setIconPackTintState(tints);
    }
    m_themeShop->setInputSource(&app().input());
    refreshAutoThemeSummary();
    m_themeShop->setCursorMotionModeState(m_config.cursorMotionMode);
    m_themeShop->setPageTransitionModeState(m_config.pageTransitionMode);
    m_themeShop->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_themeShop->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                   m_config.accessibilitySpeakPosition);
    m_themeShop->onSearchRequest([this](const std::string& current) {
        requestTextEntry(nxui::I18n::instance().tr("hint.search", "Search"),
            nxui::I18n::instance().tr("themeshop.search.guide", "Search themes"),
            current, 64, false, [this](const std::string& query) {
                if (m_themeShop) m_themeShop->setSearchQuery(query);
            });
    });

    m_themeShop->onMusicEnabledChange([this](bool enabled) {
        m_config.musicEnabled = enabled;
        if (enabled)
            m_audio.play();
        else
            m_audio.pause();
        persistMusicPlaybackState(false);
        m_config.save();
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
        cols = std::clamp(cols, 1, 8);
        if (m_config.gridColumns == cols)
            return;
        m_config.gridColumns = cols;
        reflowHomeGrid();
    });
    m_themeShop->onGridRowsChange([this](int rows) {
        rows = std::clamp(rows, 1, 5);
        if (m_config.gridRows == rows)
            return;
        m_config.gridRows = rows;
        reflowHomeGrid();
    });
    m_themeShop->onActionHintStyleChange([this](int style) {
        m_config.actionHintStyle = style == 0 ? "panel" : "capsules";
    });
    m_themeShop->onSortLargeTilesChange([this](bool enabled) {
        if (m_config.sortLargeTiles == enabled)
            return;
        m_config.sortLargeTiles = enabled;
        m_config.save();
        reflowHomeGrid();
    });
    m_themeShop->onBatterySaverChange([this](bool enabled) {
        applyBatterySaver(enabled);
    });
    m_themeShop->onLockScreenChange([this](bool enabled) {
        if (m_config.lockScreenEnabled == enabled)
            return;
        m_config.lockScreenEnabled = enabled;
        m_config.save();
        if (m_quickSettings)
            m_quickSettings->setLockScreenEnabled(enabled);
#ifdef SWITCHU_MENU
        switchu::menu::smi_cmd::setLockScreen(enabled, m_config.powerButtonAction);
#endif
    });
    m_themeShop->onPowerButtonActionChange([this](int action) {
        action = std::clamp(action, 0, 2);
        if (m_config.powerButtonAction == action)
            return;
        m_config.powerButtonAction = action;
        m_config.save();
#ifdef SWITCHU_MENU
        switchu::menu::smi_cmd::setLockScreen(m_config.lockScreenEnabled, action);
#endif
    });
    m_themeShop->onLockScreenClockFontChange([this](const std::string& id) {
        if (m_config.lockScreenClockFont == id)
            return;
        m_config.lockScreenClockFont = id;
        m_config.save();
        applyLockScreenClockFont();
    });
    m_themeShop->onAutoThemeOpen([this]() { openAutoThemeSettings(); });
    m_themeShop->onCursorMotionModeChange([this](int mode) {
        m_config.cursorMotionMode = std::clamp(mode, 0, 1);
        const bool instant = m_config.cursorMotionMode == 1;
        if (m_cursor) m_cursor->setInstantMotion(instant);
        if (m_settings) m_settings->setInstantCursorMotion(instant);
        if (m_themeShop) m_themeShop->setInstantCursorMotion(instant);
        if (m_gameOptions) m_gameOptions->setInstantCursorMotion(instant);
        if (m_folderOptions) m_folderOptions->setInstantCursorMotion(instant);
        if (m_quickSettings) m_quickSettings->setInstantCursorMotion(instant);
        if (m_dialog) m_dialog->cursor().setInstantMotion(instant);
        if (m_userSelect) m_userSelect->cursor().setInstantMotion(instant);
        updateCursor();
    });
    m_themeShop->onPageTransitionModeChange([this](int mode) {
        m_config.pageTransitionMode = std::clamp(mode, 0, 1);
        if (m_grid)
            m_grid->setSlideTransition(m_config.pageTransitionMode == 0);
        m_config.save();
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
        if (m_themeShop) m_themeShop->hide();
        m_navigator.resetToHome();
    });
        m_themeShop->onIconContrastAdaptChange([this](bool enabled) {
        if (m_config.iconContrastAdapt == enabled)
            return;
        m_config.iconContrastAdapt = enabled;
        m_config.save();
        syncColourIconContrast();
    });
    m_themeShop->onLockScreenTransitionChange([this](int mode) {
        mode = std::clamp(mode, 0, 3);
        if (m_config.lockScreenTransition == mode)
            return;
        m_config.lockScreenTransition = mode;
        m_config.save();
        if (m_lockScreen)
            m_lockScreen->setTransition(static_cast<LockScreen::Transition>(mode));
    });
    m_themeShop->onLockScreenZoomDirChange([this](int dir) {
        dir = std::clamp(dir, 0, 1);
        if (m_config.lockScreenZoomDir == dir)
            return;
        m_config.lockScreenZoomDir = dir;
        m_config.save();
        if (m_lockScreen)
            m_lockScreen->setZoomDir(dir);
    });
    m_themeShop->onLockScreenWipeDirChange([this](int dir) {
        dir = std::clamp(dir, 0, 3);
        if (m_config.lockScreenWipeDir == dir)
            return;
        m_config.lockScreenWipeDir = dir;
        m_config.save();
        if (m_lockScreen)
            m_lockScreen->setWipeDir(dir);
    });
    m_themeShop->onThemeLockClockFontOverrideChange([this](bool enabled) {
        if (m_config.themeLockClockFontOverride == enabled)
            return;
        m_config.themeLockClockFontOverride = enabled;
        m_config.save();
    });
    m_themeShop->onThemeIconPackOverrideChange([this](bool enabled) {
        if (m_config.themeIconPackOverride == enabled)
            return;
        m_config.themeIconPackOverride = enabled;
        m_config.save();
    });
    m_themeShop->onThemeMusicOverrideChange([this](bool enabled) {
        if (m_config.themeMusicOverride == enabled)
            return;
        m_config.themeMusicOverride = enabled;
        m_config.save();
    });
    m_themeShop->onThemeMusicAddToPlaylistChange([this](bool enabled) {
        if (m_config.themeMusicAddToPlaylist == enabled)
            return;
        m_config.themeMusicAddToPlaylist = enabled;
        m_config.save();
    });
    m_themeShop->onThemeMusicPlayOnApplyChange([this](bool enabled) {
        if (m_config.themeMusicPlayOnApply == enabled)
            return;
        m_config.themeMusicPlayOnApply = enabled;
        m_config.save();
    });
    m_themeShop->onIconPackTintChange([this](const std::string& packId,
                                             const ThemeShopScreen::IconPackTintState& tint) {
        AppConfig::IconPackTint cfg;
        cfg.enabled = tint.enabled;
        cfg.flatten = tint.flatten;
        cfg.grayscale = tint.grayscale;
        cfg.matchTheme = tint.matchTheme;
        cfg.hue = std::clamp(tint.hue, 0.f, 1.f);
        cfg.sat = std::clamp(tint.sat, 0.f, 1.f);
        cfg.light = std::clamp(tint.light, 0.f, 1.f);
        const AppConfig::IconPackTint previous = m_config.iconPackTintFor(packId);
        if (previous.enabled == cfg.enabled
            && previous.flatten == cfg.flatten
            && previous.grayscale == cfg.grayscale
            && previous.matchTheme == cfg.matchTheme
            && std::fabs(previous.hue - cfg.hue) < 0.0005f
            && std::fabs(previous.sat - cfg.sat) < 0.0005f
            && std::fabs(previous.light - cfg.light) < 0.0005f)
            return;
        m_config.setIconPackTint(packId, cfg);
        m_config.save();
        const std::string activeId = m_config.iconPack.empty()
            ? switchu::icons::kDefaultPackId : m_config.iconPack;
        const std::string id = packId.empty() ? switchu::icons::kDefaultPackId : packId;
        if (id == activeId)
            syncColourIconContrast();
    });
    m_themeShop->onIconPackApply([this](const std::string& packId) {
        applyIconPack(packId);
    });
    m_themeShop->onIconPackDownload([this](const std::string& packId) {
        const auto* entry = findCatalogIconEntry(packId);
        if (!entry)
            return;
        startIconPackTransfer(*entry, false);
    });
    m_themeShop->onIconPackDownloadApply([this](const std::string& packId) {
        const auto* entry = findCatalogIconEntry(packId);
        if (!entry)
            return;
        startIconPackTransfer(*entry, true);
    });
    m_themeShop->onIconPackDelete([this](const std::string& packId) {
        auto& i18n = nxui::I18n::instance();
        if (!m_dialog) return;
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            i18n.tr("themeshop.icons.remove", "Remove Icon Pack"),
            i18n.tr("themeshop.icons.remove_confirm",
                    "Delete this icon pack from the console?"),
            {{i18n.tr("button.cancel", "Cancel"), {}, false},
             {i18n.tr("themeshop.installed.remove_confirm_yes", "Remove"),
              [this, packId]() { removeIconPack(packId); }, true}});
        focusManager().setFocus(m_dialog.get());
    });
    m_themeShop->onIconCatalogUpdated([this]() {
        refreshThemeShopState();
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

        if (!m_dialog) {
            deletePreset(presetId);
            return;
        }
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            i18n.tr("themeshop.installed.remove", "Remove Theme"),
            i18n.tr("themeshop.installed.remove_confirm",
                    "Remove this theme from the console?"),
            {{i18n.tr("button.cancel", "Cancel"), {}, false},
             {i18n.tr("themeshop.installed.remove_confirm_yes", "Remove"),
              [this, presetId]() { deletePreset(presetId); }, true}});
        focusManager().setFocus(m_dialog.get());
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
    refreshLockClockFontEntries();
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
    // Theme Shop shows on-card / thin-sheet progress — skip the modal so we
    // don't steal focus or pressure the Community preview GPU budget.
    const bool inlineThemeShopProgress = m_themeShop && m_themeShop->isActive();
    if (m_progressDialog && !inlineThemeShopProgress) {
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

const ThemeCatalogClient::IconEntry* WiiUMenuApp::findCatalogIconEntry(
    const std::string& packId) const {
    if (!m_themeShop)
        return nullptr;
    const auto& icons = m_themeShop->catalogIconEntries();
    for (const auto& icon : icons) {
        if (icon.id == packId)
            return &icon;
    }
    return nullptr;
}

void WiiUMenuApp::startIconPackTransfer(const ThemeCatalogClient::IconEntry& entry,
                                        bool applyAfter) {
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
    if (!m_themeShop)
        return;

    auto& i18n = nxui::I18n::instance();
    auto shared = std::make_shared<ThemePackageTransferShared>();
    shared->themeId = entry.id;
    shared->installMode = applyAfter;
    shared->isIconPack = true;
    shared->destinationPath =
        switchu::icons::IconPackInstaller::destinationRootFor(entry.id);
    const std::string preparingLabel =
        i18n.tr("themeshop.icons.prepare_download", "Preparing icon pack download...");
    const std::string unknownTransferError =
        i18n.tr("themeshop.transfer.unknown_error", "Unknown transfer error");
    shared->state.begin(preparingLabel);
    shared->revision = 1;

    m_themePackageTransfer = shared;
    m_themePackageTransferUiRevision = 0;
    m_themePackageTransferHandledRevision = 0;

    m_themeShop->setPackageTransferState(shared->state, shared->themeId, true);
    const bool inlineThemeShopProgress = m_themeShop->isActive();
    if (m_progressDialog && !inlineThemeShopProgress) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->show(i18n.tr("themeshop.icons.dialog_title", "Downloading Icon Pack"),
                               preparingLabel,
                               -1.f);
        focusManager().setFocus(m_progressDialog.get());
    }

    const std::string catalogUrl = m_themeShop->communityCatalogUrl();
    DebugLog::log("[icon-install] transfer start: id=%s apply=%d",
                  entry.id.c_str(), applyAfter ? 1 : 0);

    m_themePackageTransferFuture = m_threadPool.submit(
        [shared, catalogUrl, entry, unknownTransferError]() {
            try {
                auto result = switchu::icons::IconPackInstaller::install(
                    catalogUrl,
                    entry,
                    [shared](std::string label, float progress) {
                        std::lock_guard<std::mutex> lk(shared->mutex);
                        shared->state.update(std::move(label), progress);
                        ++shared->revision;
                    });

                std::lock_guard<std::mutex> lk(shared->mutex);
                shared->destinationPath = result.destinationPath;
                shared->state.succeed(result.message);
                ++shared->revision;
                DebugLog::log("[icon-install] transfer success: %s", result.packId.c_str());
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lk(shared->mutex);
                shared->state.fail(ex.what());
                ++shared->revision;
                DebugLog::log("[icon-install] transfer failed: %s", ex.what());
            } catch (...) {
                std::lock_guard<std::mutex> lk(shared->mutex);
                shared->state.fail(unknownTransferError);
                ++shared->revision;
                DebugLog::log("[icon-install] transfer failed: unknown error");
            }
        });
}

void WiiUMenuApp::removeIconPack(const std::string& packId) {
    if (packId.empty())
        return;
    auto& i18n = nxui::I18n::instance();

    if (resolvedIconPackId() == packId) {
        // Fall back to Default before the glyphs disappear.
        applyIconPack(switchu::icons::kDefaultPackId);
    }

    if (!switchu::icons::IconPackInstaller::remove(packId)) {
        if (m_themeShop)
            m_themeShop->requestToast(
                i18n.tr("themeshop.icons.remove_failed", "Could not remove this icon pack."), 2.8f);
        return;
    }

    m_config.iconPackTints.erase(packId);
    m_config.save();
    if (m_themeShop)
        m_themeShop->invalidateIconPackPreviews();
    refreshThemeShopState();
    if (m_themeShop && m_themeShop->detailOpen())
        m_themeShop->closeDetailView();
    if (m_themeShop)
        m_themeShop->requestToast(
            i18n.tr("themeshop.icons.removed", "Icon pack removed."), 2.4f);
}

void WiiUMenuApp::extractThemeIconPack(const std::string& packId, const std::string& themeRoot) {
    if (packId.empty() || themeRoot.empty())
        return;

    const std::string themeFolder = themeFolderFromInstallPath(themeRoot);
    std::string targetPackId = packId;
    std::string themeName;
    std::string themeAuthor;
    {
        // Prefer theme.icons.packId when present (e.g. Atmosphere → atmos).
        nlohmann::json manifest;
        std::ifstream input(joinPath(themeRoot, "theme.json"));
        if (input) {
            try {
                input >> manifest;
            } catch (...) {
                manifest = nlohmann::json{};
            }
        }
        if (manifest.is_object()) {
            readJsonString(manifest, "name", themeName);
            readJsonString(manifest, "author", themeAuthor);
            auto readPackId = [&](const nlohmann::json& iconsNode) {
                if (!iconsNode.is_object())
                    return;
                for (const char* key : {"packId", "pack_id", "pack", "iconPack", "icon_pack"}) {
                    std::string value;
                    if (readJsonString(iconsNode, key, value)) {
                        targetPackId = std::move(value);
                        return;
                    }
                }
            };
            if (auto icons = manifest.find("icons"); icons != manifest.end())
                readPackId(*icons);
            if (auto theme = manifest.find("theme"); theme != manifest.end() && theme->is_object()) {
                if (auto icons = theme->find("icons"); icons != theme->end())
                    readPackId(*icons);
            }
        }
    }

    const std::string sourceDir = themeRoot + "/assets/icons";
    const std::string destDir =
        switchu::icons::IconPackInstaller::destinationRootFor(targetPackId);

    // Companion pack already on the console (builtin atmos, prior download):
    // drop the theme-local copy and any slug-named orphan clone.
    const std::string existing =
        switchu::icons::resolvePackIconsDirectory(SD_ASSETS, targetPackId);
    if (!existing.empty()) {
        std::error_code ec;
        if (switchu::icons::iconDirLooksUsable(sourceDir))
            std::filesystem::remove_all(sourceDir, ec);
        if (!themeFolder.empty() && themeFolder != targetPackId) {
            const std::string orphan =
                switchu::icons::IconPackInstaller::destinationRootFor(themeFolder);
            if (orphan != existing && pathExists(orphan)) {
                std::filesystem::remove_all(orphan, ec);
                DebugLog::log("[icon-pack] removed orphan theme clone pack=%s",
                              themeFolder.c_str());
            }
            if (m_config.iconPack == themeFolder) {
                m_config.iconPack = targetPackId;
                m_config.save();
            }
        }
        DebugLog::log("[icon-pack] theme icons skipped; pack %s already present at %s",
                      targetPackId.c_str(), existing.c_str());
        return;
    }

    if (!switchu::icons::iconDirLooksUsable(sourceDir))
        return;

    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    int copied = 0;
    for (const auto& entry : std::filesystem::directory_iterator(sourceDir, ec)) {
        if (ec || !entry.is_regular_file(ec))
            continue;
        const std::string name = entry.path().filename().string();
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".png") != 0)
            continue; // skip the theme's own *_disabled markers

        std::ifstream input(entry.path(), std::ios::binary);
        if (!input)
            continue;
        std::string bytes((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
        std::ofstream output(destDir + "/" + name, std::ios::binary | std::ios::trunc);
        if (!output)
            continue;
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (output)
            ++copied;
    }

    if (copied == 0) {
        DebugLog::log("[icon-pack] theme icons copy failed: %s", sourceDir.c_str());
        return;
    }

    writeIconPackManifest(destDir, targetPackId,
                          themeName.empty() ? targetPackId : themeName,
                          themeAuthor);

    // The pack now lives in the shared icon directory: drop the copy inside the
    // theme so it is not listed twice.
    std::filesystem::remove_all(sourceDir, ec);
    if (!themeFolder.empty() && themeFolder != targetPackId) {
        const std::string orphan =
            switchu::icons::IconPackInstaller::destinationRootFor(themeFolder);
        if (pathExists(orphan))
            std::filesystem::remove_all(orphan, ec);
    }
    DebugLog::log("[icon-pack] theme icons extracted: theme=%s pack=%s files=%d",
                  themeFolder.c_str(), targetPackId.c_str(), copied);
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
    bool isIconPack = false;
    std::uint64_t revision = 0;
    std::string destinationPath;
    {
        std::lock_guard<std::mutex> lk(shared->mutex);
        state = shared->state;
        themeId = shared->themeId;
        installMode = shared->installMode;
        isIconPack = shared->isIconPack;
        revision = shared->revision;
        destinationPath = shared->destinationPath;
    }

    if (revision != m_themePackageTransferUiRevision) {
        if (m_themeShop)
            m_themeShop->setPackageTransferState(state, themeId, installMode);
        const bool inlineThemeShopProgress = m_themeShop && m_themeShop->isActive();
        if (m_progressDialog && state.isRunning() && !inlineThemeShopProgress) {
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

    if (state.isReady() && isIconPack) {
        auto& i18n = nxui::I18n::instance();
        DebugLog::log("[icon-install] transfer ready: id=%s apply=%d destination=%s",
                      themeId.c_str(), installMode ? 1 : 0, safeLogPath(destinationPath));
        if (installMode)
            applyIconPack(themeId);
        else
            refreshThemeShopState();
        state.succeed(i18n.tr("themeshop.icons.installed", "Icon pack installed."));
        if (m_themeShop)
            m_themeShop->setPackageTransferState(state, themeId, installMode);
    }

    if (state.isReady() && !isIconPack) {
        DebugLog::log("[theme-apply] transfer ready: id=%s installMode=%d destination=%s",
                      themeId.c_str(),
                      installMode ? 1 : 0,
                      safeLogPath(destinationPath));

        // Older theme packages still bundle sidebar glyphs. Move them into the
        // shared icon directory so they show up as a normal downloaded pack
        // (and keep working after the theme is removed).
        extractThemeIconPack(themeId, destinationPath);
        if (m_themeShop)
            m_themeShop->invalidateIconPackPreviews();

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
        {
            const std::string folder = themeFolderFromPreset(preset);
            const std::string companionId = resolvedThemeIconPackId(preset, folder);
            const std::string iconsDir = resolveThemeAssetPath(preset, preset.icons.basePath);
            entry.hasIcons = switchu::icons::iconDirLooksUsable(iconsDir);
            if (!entry.hasIcons && !companionId.empty()) {
                const std::string packDir =
                    switchu::icons::resolvePackIconsDirectory(SD_ASSETS, companionId);
                entry.hasIcons = switchu::icons::iconDirLooksUsable(packDir);
            }
            if (!entry.hasIcons && !folder.empty() && folder != companionId) {
                const std::string folderPackDir =
                    switchu::icons::resolvePackIconsDirectory(SD_ASSETS, folder);
                entry.hasIcons = switchu::icons::iconDirLooksUsable(folderPackDir);
            }
        }
        // Cheap local dir checks — no I/O beyond a couple of is_directory calls.
        if (!preset.installPath.empty()) {
            const std::string soundBase = resolveThemeSoundBase(preset.installPath);
            const std::string root = soundBase.empty() ? preset.installPath : soundBase;
            entry.hasMusic = pathExists(root + "/music");
            entry.hasSounds = pathExists(root + "/sfx");
        }
        entries.push_back(std::move(entry));
    }

    return entries;
}


std::string WiiUMenuApp::resolvedIconPackId() const {
    const std::string configured = m_config.iconPack.empty()
        ? switchu::icons::kDefaultPackId : m_config.iconPack;
    // Normalize legacy theme:package:<id> ids to theme:<folder>.
    const std::string normalized = switchu::icons::isThemePackId(configured)
        ? switchu::icons::makeThemePackId(configured)
        : configured;
    const std::string dir = switchu::icons::resolvePackIconsDirectory(SD_ASSETS, normalized);
    if (dir.empty()) {
        // Log once per distinct id — resolvedIconPackId is hit every frame.
        static std::string s_lastFailed;
        if (s_lastFailed != normalized) {
            s_lastFailed = normalized;
            DebugLog::log("[icon-pack] resolve failed pack=%s (configured=%s); healing to default",
                          normalized.c_str(), configured.c_str());
        }
        return switchu::icons::kDefaultPackId;
    }
    return normalized;
}

std::string WiiUMenuApp::iconsDirectoryForPack(const std::string& packId) const {
    const std::string id = switchu::icons::isThemePackId(packId)
        ? switchu::icons::makeThemePackId(packId) : packId;
    if (!switchu::icons::isThemePackId(id))
        return {};
    return switchu::icons::resolvePackIconsDirectory(SD_ASSETS, id);
}

namespace {

// Packs that ship with SwitchU: always present, never removable from the shop.
bool isBuiltinIconPack(const std::string& packId) {
    return packId == switchu::icons::kDefaultPackId
        || packId == switchu::icons::kAtmosPackId;
}

} // namespace

std::vector<ThemeShopScreen::ThemeShopEntry> WiiUMenuApp::buildIconPackEntries() {
    std::vector<ThemeShopScreen::ThemeShopEntry> entries;
    const std::string activeId = resolvedIconPackId();

    // Companion pack ids claimed by installed themes (theme.icons.packId).
    std::unordered_map<std::string, std::string> companionByThemeFolder;
    std::unordered_set<std::string> companionPackIds;
    for (const auto& preset : m_allPresets) {
        if (preset.source != ThemePresetSource::InstalledPackage)
            continue;
        const std::string folder = themeFolderFromPreset(preset);
        if (folder.empty())
            continue;
        if (!preset.icons.packId.empty()) {
            companionByThemeFolder[folder] = preset.icons.packId;
            companionPackIds.insert(preset.icons.packId);
        }
    }

    // Heal slug-named clones when the theme now points at a companion pack.
    for (const auto& [folder, companionId] : companionByThemeFolder) {
        if (folder.empty() || folder == companionId)
            continue;
        const std::string companionDir =
            switchu::icons::resolvePackIconsDirectory(SD_ASSETS, companionId);
        if (companionDir.empty())
            continue;
        const std::string orphan =
            switchu::icons::IconPackInstaller::destinationRootFor(folder);
        if (!pathExists(orphan))
            continue;
        std::error_code ec;
        std::filesystem::remove_all(orphan, ec);
        DebugLog::log("[icon-pack] healed orphan clone pack=%s -> %s",
                      folder.c_str(), companionId.c_str());
        if (m_config.iconPack == folder) {
            m_config.iconPack = companionId;
            m_config.save();
        }
    }

    auto addPack = [&](const switchu::icons::IconPackInfo& pack) {
        // Hide theme-folder clones that duplicate a companion pack id.
        if (companionByThemeFolder.count(pack.id)
            && companionByThemeFolder[pack.id] != pack.id
            && companionPackIds.count(companionByThemeFolder[pack.id])) {
            const std::string& companion = companionByThemeFolder[pack.id];
            if (!switchu::icons::resolvePackIconsDirectory(SD_ASSETS, companion).empty())
                return;
        }

        const bool alreadyListed = std::any_of(
            entries.begin(), entries.end(),
            [&](const ThemeShopScreen::ThemeShopEntry& e) { return e.id == pack.id; });
        if (alreadyListed)
            return;

        ThemeShopScreen::ThemeShopEntry entry;
        entry.id = pack.id;
        entry.name = pack.name;
        entry.author = pack.author;
        entry.version = "1.0.0";
        entry.builtin = isBuiltinIconPack(pack.id);
        entry.source = pack.fromTheme ? "Theme"
            : (entry.builtin ? "Built-in" : "Installed");
        entry.coverPath = switchu::icons::ensurePackPreview(pack.directory, pack.id);
        entry.active = (pack.id == activeId);
        // Downloaded / extracted packs on SD can be removed; builtins and
        // in-theme glyphs stay tied to their theme.
        entry.installed = true;
        entry.removable = !pack.fromTheme && !entry.builtin;
        entry.colourPack = pack.colourPack;
        entries.push_back(std::move(entry));
    };

    for (const auto& pack : switchu::icons::discoverPacks(SD_ASSETS))
        addPack(pack);
    // Theme icon folders are discovered from disk the same way as SD packs.
    for (const auto& pack : switchu::icons::discoverThemePacks())
        addPack(pack);

    // Online catalog: list every catalog pack (installed ones stay installed —
    // Icon Shop shows them with an Installed badge, like Theme Shop Community).
    if (m_themeShop) {
        const std::string& catalogUrl = m_themeShop->communityCatalogUrl();
        const std::size_t slash = catalogUrl.find_last_of('/');
        const std::string catalogBase =
            slash == std::string::npos ? std::string() : catalogUrl.substr(0, slash + 1);
        auto resolveCoverUrl = [&](const std::string& cover) -> std::string {
            if (cover.empty())
                return {};
            const bool absolute = cover.rfind("https://", 0) == 0
                || cover.rfind("http://", 0) == 0;
            return absolute ? cover : catalogBase + cover;
        };
        for (const auto& icon : m_themeShop->catalogIconEntries()) {
            auto existing = std::find_if(
                entries.begin(), entries.end(),
                [&](const ThemeShopScreen::ThemeShopEntry& e) { return e.id == icon.id; });
            if (existing != entries.end()) {
                // Enrich the on-console row so Icon Shop can list it.
                existing->catalog = true;
                if (!icon.name.empty())
                    existing->name = icon.name;
                if (!icon.author.empty())
                    existing->author = icon.author;
                if (!icon.version.empty())
                    existing->version = icon.version;
                if (!icon.description.empty())
                    existing->description = icon.description;
                existing->colourPack = icon.colour;
                // Keep both: Installed shelf uses coverPath (live mosaic + tint);
                // Icon Shop uses coverUrl (vanilla catalog art).
                existing->coverUrl = resolveCoverUrl(icon.cover);
                continue;
            }
            // Catalog row whose theme companion is already installed as another id.
            if (!icon.theme.empty()) {
                std::string linkedPack;
                if (auto it = companionByThemeFolder.find(icon.theme);
                    it != companionByThemeFolder.end())
                    linkedPack = it->second;
                const bool themeCompanionInstalled = std::any_of(
                    entries.begin(), entries.end(),
                    [&](const ThemeShopScreen::ThemeShopEntry& e) {
                        return e.installed && (e.id == icon.theme
                            || (!linkedPack.empty() && e.id == linkedPack));
                    });
                if (themeCompanionInstalled)
                    continue;
            }

            ThemeShopScreen::ThemeShopEntry entry;
            entry.id = icon.id;
            entry.name = icon.name;
            entry.author = icon.author;
            entry.version = icon.version;
            entry.description = icon.description;
            entry.builtin = icon.builtin;
            entry.catalog = true;
            entry.installed = false;
            entry.removable = false;
            entry.active = false;
            entry.colourPack = icon.colour;
            entry.source = "Shop";
            entry.coverUrl = resolveCoverUrl(icon.cover);
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

std::string WiiUMenuApp::resolveActiveIconGlyphPath(const char* fileName) const {
    return switchu::icons::resolveGlyphPath(SD_ASSETS, resolvedIconPackId(), fileName);
}

void WiiUMenuApp::reloadSidebarIcons() {
    const std::string packId = resolvedIconPackId();
    // Theme packs resolve to their on-disk icons dir; SD packs leave this empty
    // and SidebarManager loads from icons/<packId>/ via packId.
    const std::string customIconsBase = iconsDirectoryForPack(packId);
    const std::string fallbackPackId = switchu::icons::isThemePackId(packId)
        ? switchu::icons::kDefaultPackId : packId;
    DebugLog::log("[icon-pack] reload pack=%s custom=%s fallback=%s",
                  packId.c_str(),
                  customIconsBase.empty() ? "<empty>" : customIconsBase.c_str(),
                  fallbackPackId.c_str());
    m_sidebar.invalidateAssetsCache();
    m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS, customIconsBase, fallbackPackId);
    // The layout-flip chrome button uses the same pack, so refresh it too.
    if (m_layoutFlip) {
        m_layoutFlip->setIconHslAdjust(m_sidebar.iconHslAdjust());
        m_layoutFlip->setColourIconContrast(m_sidebar.colourIconContrast());
        m_layoutFlip->reloadIcon(app().gpu(), app().renderer(),
                                 resolveActiveIconGlyphPath(switchu::icons::kChangeViewFile));
    }
}

void WiiUMenuApp::applyIconPack(const std::string& packId) {
    std::string id = packId.empty() ? switchu::icons::kDefaultPackId : packId;
    if (switchu::icons::isThemePackId(id))
        id = switchu::icons::makeThemePackId(id);
    else
        id = switchu::icons::canonicalizeIconPackId(id);
    const bool packChanged = m_config.iconPack != id;
    if (packChanged) {
        m_config.iconPack = id;
        m_config.save();
    }
    DebugLog::log("[icon-pack] apply pack=%s changed=%d", id.c_str(), packChanged ? 1 : 0);
    // Bake once with the pack's current HSL (avoid reload → sync → reload).
    applyActivePackHslToSidebar();
    reloadSidebarIcons();
    if (m_quickSettings)
        m_quickSettings->invalidateMusicControlIcons();
    syncColourIconContrast();
    refreshThemeShopState();
    if (m_themeShop)
        m_themeShop->requestToast(
            nxui::I18n::instance().tr("themeshop.icons.applied", "Icon pack applied."), 2.0f);
}

void WiiUMenuApp::refreshThemeShopState(bool rebuildTab) {
    if (!m_themeShop) return;

    ThemePreset* activePreset = findPresetPtr(m_activePresetName);
    std::string activeId = activePreset ? (activePreset->id.empty() ? activePreset->name : activePreset->id)
                                        : m_activePresetName;

    // Normalize legacy theme:package:* ids without clobbering a valid selection.
    const std::string activePackId = resolvedIconPackId();
    if (m_config.iconPack != activePackId && switchu::icons::isThemePackId(m_config.iconPack)
        && activePackId != switchu::icons::kDefaultPackId) {
        m_config.iconPack = activePackId;
        m_config.save();
    }

    m_themeShop->setTheme(&m_theme);
    m_themeShop->setMusicState(m_audio.isPlaying(), m_audio.volume(), m_audio.sfxVolume());
    m_themeShop->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_themeShop->setActionHintStyleState(m_config.actionHintStyle);
    m_themeShop->setSortLargeTilesState(m_config.sortLargeTiles);
    m_themeShop->setBatterySaverState(m_config.batterySaver);
    m_themeShop->setLockScreenState(m_config.lockScreenEnabled);
    m_themeShop->setPowerButtonAction(m_config.powerButtonAction);
    m_themeShop->setLockScreenClockFontState(m_config.lockScreenClockFont);
    m_themeShop->setLockClockFontContext(
        SD_ASSETS,
        m_loadedRegularFontPath.empty()
            ? std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf"
            : m_loadedRegularFontPath);
    refreshLockClockFontEntries();
    m_themeShop->setFrameTapLiteEngaged(m_frameTapStatus.viewerConnected());
    m_themeShop->setIconContrastAdaptState(m_config.iconContrastAdapt);
    m_themeShop->setLockScreenTransitionState(m_config.lockScreenTransition);
    m_themeShop->setLockScreenZoomDirState(m_config.lockScreenZoomDir);
    m_themeShop->setLockScreenWipeDirState(m_config.lockScreenWipeDir);
    m_themeShop->setThemeLockClockFontOverrideState(m_config.themeLockClockFontOverride);
    m_themeShop->setThemeIconPackOverrideState(m_config.themeIconPackOverride);
    m_themeShop->setThemeMusicOverrideState(m_config.themeMusicOverride);
    m_themeShop->setThemeMusicAddToPlaylistState(m_config.themeMusicAddToPlaylist);
    m_themeShop->setThemeMusicPlayOnApplyState(m_config.themeMusicPlayOnApply);
    m_themeShop->setCursorMotionModeState(m_config.cursorMotionMode);
    m_themeShop->setPageTransitionModeState(m_config.pageTransitionMode);
    m_themeShop->setThemeMatchAccent(m_activeColors.accentH,
                                     m_activeColors.accentS,
                                     m_activeColors.accentL);
    {
        std::unordered_map<std::string, ThemeShopScreen::IconPackTintState> tints;
        for (const auto& [packId, tint] : m_config.iconPackTints) {
            ThemeShopScreen::IconPackTintState state;
            state.enabled = tint.enabled;
            state.flatten = tint.flatten;
            state.grayscale = tint.grayscale;
            state.matchTheme = tint.matchTheme;
            state.hue = tint.hue;
            state.sat = tint.sat;
            state.light = tint.light;
            tints[packId] = state;
        }
        m_themeShop->setIconPackTintState(tints);
    }
    m_themeShop->setThemeShopState(buildThemeShopEntries(), activeId);
    m_themeShop->setIconPackState(buildIconPackEntries(), activePackId);
    if (rebuildTab)
        m_themeShop->rebuildCurrentTab();
}

void WiiUMenuApp::applyActivePackHslToSidebar() {
    const AppConfig::IconPackTint packTint = m_config.iconPackTintFor(resolvedIconPackId());
    switchu::icons::HslAdjust hsl;
    hsl.enabled = packTint.enabled;
    hsl.flatten = packTint.flatten;
    hsl.grayscale = packTint.grayscale;
    hsl.hue = std::clamp(packTint.hue, 0.f, 1.f);
    hsl.sat = std::clamp(packTint.sat, 0.f, 1.f);
    hsl.light = std::clamp(packTint.light, 0.f, 1.f);
    m_sidebar.setIconHslAdjust(hsl);
    if (m_layoutFlip)
        m_layoutFlip->setIconHslAdjust(hsl);
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

    // Themes that ship icons become the active Icons-library pack (unless opted out).
    if (m_config.themeIconPackOverride) {
        std::string folder = themeFolderFromPreset(*preset);
        if (folder.empty())
            folder = presetRef;
        const std::string companionId = resolvedThemeIconPackId(*preset, folder);
        // Prefer theme.icons.packId (e.g. atmos), then folder extract, then theme:*.
        std::string selectedId;
        std::string selectedDir;
        auto tryPack = [&](const std::string& id) {
            if (id.empty() || !selectedId.empty())
                return;
            const std::string dir =
                switchu::icons::resolvePackIconsDirectory(SD_ASSETS, id);
            if (dir.empty())
                return;
            selectedId = id;
            selectedDir = dir;
        };
        tryPack(companionId);
        if (folder != companionId)
            tryPack(folder);
        tryPack(switchu::icons::makeThemePackId(folder));
        if (!selectedId.empty() && m_config.iconPack != selectedId) {
            m_config.iconPack = selectedId;
            DebugLog::log("[icon-pack] theme apply selected pack=%s dir=%s",
                          selectedId.c_str(), selectedDir.c_str());
        }
    } else {
        DebugLog::log("[icon-pack] theme apply skipped (icon override off): preset=%s",
                      presetRef.c_str());
    }

    // Lock clock: optionally follow a theme that ships fonts.
    if (m_config.themeLockClockFontOverride) {
        std::string nextFontId;
        if (!preset->fonts.regularPath.empty() || !preset->fonts.smallPath.empty()) {
            nextFontId = "theme";
        } else {
            const auto packageFonts = switchu::lockfonts::themePackageEntries({*preset});
            if (!packageFonts.empty())
                nextFontId = packageFonts.front().id;
        }
        if (!nextFontId.empty() && m_config.lockScreenClockFont != nextFontId) {
            m_config.lockScreenClockFont = nextFontId;
            DebugLog::log("[lock] theme apply clock font=%s", nextFontId.c_str());
            if (m_themeShop)
                m_themeShop->setLockScreenClockFontState(nextFontId);
            applyLockScreenClockFont();
        }
    }

    m_config.save();

    // Rematch Match-theme packs to the new accent before baking sidebar glyphs
    // so we only reloadAssets once (old path: bake → rematch → bake again).
    rematchIconPackTintsToTheme(/*syncSidebar=*/false);
    applyActivePackHslToSidebar();

    // Cancel in-flight Theme Shop preview workers so Apply does not fight
    // cover uploads for GPU / waitIdle under Applet pressure (Frame Tap).
    if (m_themeShop)
        m_themeShop->pausePreviewWorkForSleep();

    DebugLog::log("[theme-apply] rebuildThemeFromColors start: preset=%s", presetRef.c_str());
    rebuildThemeFromColors();
    DebugLog::log("[theme-apply] rebuildThemeFromColors done: preset=%s", presetRef.c_str());

    // HSL already applied above - sync should be a no-op reload when unchanged.
    syncColourIconContrast();

    // Sound already loads off-thread; queue after visuals so Apply returns sooner.
    // Theme Shop Options: replace playlist (legacy), merge into it, or leave alone.
    if (applyBundledSound && !preset->soundPreset.empty()) {
        if (m_config.themeMusicOverride) {
            DebugLog::log("[theme-apply] changeSoundPreset queued: preset=%s sound=%s",
                          presetRef.c_str(),
                          preset->soundPreset.c_str());
            m_config.soundPreset = preset->soundPreset;
            changeSoundPreset(preset->soundPreset);
        } else if (m_config.themeMusicAddToPlaylist) {
            const std::string base = resolveThemeSoundBase(preset->installPath);
            const std::string musicDir = base.empty() ? std::string() : (base + "/music");
            std::string album = "theme";
            if (!preset->installPath.empty()) {
                const auto pos = preset->installPath.find_last_of("/\\");
                album = "theme-" + ((pos == std::string::npos)
                    ? preset->installPath
                    : preset->installPath.substr(pos + 1));
            } else if (!presetRef.empty()) {
                album = "theme-" + presetRef;
            }
            std::error_code musicDirEc;
            if (!musicDir.empty() && std::filesystem::is_directory(musicDir, musicDirEc)
                && !m_presetChangePending && !m_themeMusicMergePending) {
                DebugLog::log("[theme-apply] merge theme music: dir=%s album=%s play=%d",
                              safeLogPath(musicDir),
                              album.c_str(),
                              (m_config.themeMusicPlayOnApply && m_config.musicEnabled) ? 1 : 0);
                m_themeMusicMergePending = true;
                m_themeMusicMergePlay = m_config.themeMusicPlayOnApply && m_config.musicEnabled;
                m_themeMusicMergeFirstIndex = m_audio.trackCount();
                m_audioFuture = m_threadPool.submit([this, musicDir, album]() {
                    const size_t n = appendMp3TracksFromDir(m_audio, musicDir, album);
                    DebugLog::log("[audio] merged %zu theme track(s) from %s",
                                  n, safeLogPath(musicDir));
                });
            } else {
                DebugLog::log("[theme-apply] merge theme music skipped: dir=%s pending=%d",
                              safeLogPath(musicDir),
                              (m_presetChangePending || m_themeMusicMergePending) ? 1 : 0);
            }
        } else {
            DebugLog::log("[theme-apply] bundled sound skipped by Options (override off, add off): preset=%s",
                          presetRef.c_str());
        }
    } else {
        DebugLog::log("[theme-apply] bundled sound skipped: preset=%s apply=%d soundPreset=%s",
                      presetRef.c_str(),
                      applyBundledSound ? 1 : 0,
                      safeLogPath(preset->soundPreset));
    }

    // Light shop refresh: update Applied badges / tints without rebuildCurrentTab.
    DebugLog::log("[theme-apply] refreshThemeShopState start: preset=%s (light)", presetRef.c_str());
    refreshThemeShopState(/*rebuildTab=*/false);
    DebugLog::log("[theme-apply] refreshThemeShopState done: preset=%s", presetRef.c_str());
    m_themeRenderDebugFrames = 6;
    if (m_themeShop)
        m_themeShop->requestRenderDiagnostics(6);
    m_audio.playSfx(Sfx::ThemeToggle);
    DebugLog::log("[theme-apply] complete preset=%s", presetRef.c_str());
}

void WiiUMenuApp::applyAutoThemeConfig() {
    using switchu::services::AutoThemeMode;
    using switchu::services::AutoThemeSettings;

    AutoThemeSettings settings;
    if (m_config.autoThemeMode == "manual")
        settings.mode = AutoThemeMode::ManualHours;
    else if (m_config.autoThemeMode == "geo")
        settings.mode = AutoThemeMode::Geolocation;
    else
        settings.mode = AutoThemeMode::Off;

    settings.dayPreset = m_config.autoThemeDayPreset;
    settings.nightPreset = m_config.autoThemeNightPreset;
    settings.dayStartHour = m_config.autoThemeDayStartHour;
    settings.nightStartHour = m_config.autoThemeNightStartHour;

    m_autoTheme.configure(settings);

    // Seed the geolocation provider with the cached position, if any.
    if (m_config.autoThemeGeoResolved)
        m_autoTheme.setGeoLocation(switchu::services::GeoLocation{
            m_config.autoThemeGeoLat, m_config.autoThemeGeoLon});
    else
        m_autoTheme.setGeoLocation(std::nullopt);

    DebugLog::log("[auto-theme] configured: mode=%s day=%s night=%s dayStart=%d nightStart=%d",
                  m_config.autoThemeMode.c_str(),
                  safeLogPath(settings.dayPreset),
                  safeLogPath(settings.nightPreset),
                  settings.dayStartHour,
                  settings.nightStartHour);
}

void WiiUMenuApp::evaluateAutoTheme(bool force) {
    if (force)
        m_autoTheme.reset();

    const auto snapshot = m_clockService.refresh();
    auto desired = m_autoTheme.evaluate(snapshot);
    if (!desired)
        return;

    if (*desired == m_activePresetName)
        return;

    ThemePreset* preset = findPresetPtr(*desired);
    if (!preset) {
        DebugLog::log("[auto-theme] target preset not found: %s", desired->c_str());
        return;
    }

    DebugLog::log("[auto-theme] switching to preset=%s (hour=%u)",
                  desired->c_str(),
                  static_cast<unsigned>(snapshot.calendar.hour));
    // Auto switching keeps the current sound preset (applyBundledSound=false).
    activateThemePreset(preset, false);
}

void WiiUMenuApp::maybeFetchGeoLocation() {
    if (!m_autoTheme.needsGeoLocation() || m_geoFetchInFlight)
        return;

    m_geoFetchInFlight = true;
    m_geoFetchResult = std::make_shared<GeoFetchResult>();
    DebugLog::log("[auto-theme][geo] starting IP geolocation lookup");
    auto result = m_geoFetchResult;
    m_geoFetchFuture = m_threadPool.submit([result]() {
        try {
            // The Switch has no GPS; approximate the position from the public IP.
            const std::string body = themeshop::http::getText(
                "http://ip-api.com/json/?fields=status,message,lat,lon,city");
            const auto j = nlohmann::json::parse(body);
            if (j.value("status", std::string()) == "success") {
                result->lat = j.value("lat", 0.0);
                result->lon = j.value("lon", 0.0);
                result->city = j.value("city", std::string());
                result->ok = true;
            } else {
                DebugLog::log("[auto-theme][geo] api status not success: %s",
                              j.value("message", std::string("?")).c_str());
            }
        } catch (const std::exception& ex) {
            DebugLog::log("[auto-theme][geo] lookup failed: %s", ex.what());
        } catch (...) {
            DebugLog::log("[auto-theme][geo] lookup failed: unknown error");
        }
    });
}

void WiiUMenuApp::pollGeoLocationFetch() {
    if (!m_geoFetchInFlight || !m_geoFetchFuture.valid())
        return;
    if (m_geoFetchFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    m_geoFetchFuture.get();
    const GeoFetchResult r = m_geoFetchResult ? *m_geoFetchResult : GeoFetchResult{};
    m_geoFetchResult.reset();
    m_geoFetchInFlight = false;

    if (!r.ok) {
        DebugLog::log("[auto-theme][geo] no location resolved (offline or API error)");
        return;
    }

    DebugLog::log("[auto-theme][geo] resolved lat=%.4f lon=%.4f city=%s",
                  r.lat, r.lon, r.city.c_str());

    m_config.autoThemeGeoResolved = true;
    m_config.autoThemeGeoLat = r.lat;
    m_config.autoThemeGeoLon = r.lon;
    m_config.autoThemeGeoCity = r.city;
    m_autoTheme.setGeoLocation(switchu::services::GeoLocation{r.lat, r.lon});

    // Persist the resolved location so future launches skip the network request.
    if (m_configSaveFuture.valid())
        m_configSaveFuture.wait();
    m_configSaveFuture = m_threadPool.submit([cfg = m_config]() { cfg.save(); });

    evaluateAutoTheme(true);
    pushGeoDisplayToScreen();
}

void WiiUMenuApp::pushGeoDisplayToScreen() {
    if (!m_autoThemeScreen)
        return;

    const bool resolved = m_config.autoThemeGeoResolved;
    std::string city, sunrise, sunset;

    if (resolved) {
        city = m_config.autoThemeGeoCity.empty()
            ? fmt::format("{:.2f}, {:.2f}", m_config.autoThemeGeoLat, m_config.autoThemeGeoLon)
            : m_config.autoThemeGeoCity;

        auto& i18n = nxui::I18n::instance();
        const auto snap = m_clockService.refresh();
        const auto sun = m_autoTheme.geoSunTimes(snap);
        if (sun.valid) {
            if (sun.polarDay) {
                sunrise = sunset = i18n.tr("autotheme.geo_midnight_sun", "Midnight sun");
            } else if (sun.polarNight) {
                sunrise = sunset = i18n.tr("autotheme.geo_polar_night", "Polar night");
            } else {
                auto fmtHour = [this](double h) {
                    int hh = static_cast<int>(std::floor(h));
                    int mm = static_cast<int>(std::llround((h - hh) * 60.0));
                    if (mm >= 60) { mm -= 60; ++hh; }
                    hh = ((hh % 24) + 24) % 24;
                    if (m_config.clockUse12Hour) {
                        int h12 = hh % 12;
                        if (h12 == 0) h12 = 12;
                        return fmt::format("{}:{:02d} {}", h12, mm, hh >= 12 ? "PM" : "AM");
                    }
                    return fmt::format("{:02d}:{:02d}", hh, mm);
                };
                sunrise = fmtHour(sun.sunriseLocalHours);
                sunset = fmtHour(sun.sunsetLocalHours);
            }
        }
    }

    m_autoThemeScreen->updateGeoDisplay(resolved, city, sunrise, sunset);
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

    const std::string defaultGameCardPath = std::string(SD_ASSETS) + "/icons/default/gamecard.png";
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
            applyLockScreenClockFont();
            if (m_themeShop) {
                m_themeShop->setLockClockFontContext(
                    SD_ASSETS,
                    m_loadedRegularFontPath.empty()
                        ? std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf"
                        : m_loadedRegularFontPath);
            }
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
        // Theme config regenerates shapes — re-assert lite suppress if FT/Battery
        // Saver currently holds the light path.
        if (m_frameTapLightActive)
            applyFrameTapLiteBackground(true);

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

    if (needsGpuResourceReload)
        ren.reclaimReleasedTextureSlotsAfterIdle();

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

void WiiUMenuApp::applyHomeChrome() {
    const nxui::Theme& chrome = m_homeChrome;

    if (m_clock) {
        m_clock->setBaseColor(chrome.panelBase);
        m_clock->setBorderColor(chrome.panelBorder);
        m_clock->setHighlightColor(chrome.panelHighlight);
        m_clock->setTextColor(chrome.textPrimary);
        m_clock->setSecondaryTextColor(chrome.textSecondary);
    }

    if (m_battery) {
        m_battery->setBaseColor(chrome.panelBase);
        m_battery->setBorderColor(chrome.panelBorder);
        m_battery->setHighlightColor(chrome.panelHighlight);
        m_battery->setTextColor(chrome.textPrimary);
    }

    if (m_titlePill) {
        m_titlePill->setBaseColor(chrome.panelBase.withAlpha(
            chrome.mode == nxui::ThemeMode::Dark ? 0.90f : 0.86f));
        m_titlePill->setBorderColor(chrome.panelBorder.withAlpha(0.84f));
        m_titlePill->setHighlightColor(chrome.panelHighlight.withAlpha(0.42f));
        m_titlePill->setTextColor(chrome.textPrimary);
    }

    if (m_pageIndicator) {
        m_pageIndicator->setBaseColor(chrome.panelBase);
        m_pageIndicator->setBorderColor(chrome.panelBorder);
        m_pageIndicator->setHighlightColor(chrome.panelHighlight);
        m_pageIndicator->setTheme(&m_homeChrome);
    }
    if (m_folderHeader) {
        m_folderHeader->setBaseColor(chrome.panelBase);
        m_folderHeader->setBorderColor(chrome.panelBorder);
        m_folderHeader->setHighlightColor(chrome.panelHighlight);
    }
    if (m_folderHeaderLabel)
        m_folderHeaderLabel->setTextColor(chrome.textPrimary);
    if (m_profileLock)
        m_profileLock->setTheme(&m_homeChrome);
    if (m_layoutFlip)
        m_layoutFlip->setTheme(&m_homeChrome);

    m_sidebar.applyTheme(m_homeChrome);
    syncColourIconContrast();
}

void WiiUMenuApp::updateHeroAwareChrome(bool force) {
    HeroChromeAdapt desired = HeroChromeAdapt::Off;
    // Full-opacity heroes are single-row only; dimmed grid heroes leave chrome alone.
    const bool fullHeroContext =
        presentationLayoutMode() == AppLayoutMode::DynamicLine;

    if (fullHeroContext && m_steamGridDbBackdrop
        && m_steamGridDbBackdrop->hasDisplayedHero()) {
        const float luma = m_steamGridDbBackdrop->displayedHeroLuminance();
        if (luma >= 0.f) {
            // Hysteresis avoids flicker when focus skims near-threshold heroes.
            const float darkEnter = 0.42f;
            const float darkExit = 0.50f;
            const float lightEnter = 0.58f;
            const float lightExit = 0.50f;
            if (m_theme.mode == nxui::ThemeMode::Light) {
                const float limit = (m_heroChromeAdapt == HeroChromeAdapt::ForceDark)
                    ? darkExit : darkEnter;
                if (luma < limit)
                    desired = HeroChromeAdapt::ForceDark;
            } else if (m_theme.mode == nxui::ThemeMode::Dark) {
                const float limit = (m_heroChromeAdapt == HeroChromeAdapt::ForceLight)
                    ? lightExit : lightEnter;
                if (luma > limit)
                    desired = HeroChromeAdapt::ForceLight;
            }
        }
    }

    // Colour-icon contrast must track every hero change even when chrome mode
    // stays put (otherwise adapt never releases after the first hero).
    if (!force && desired == m_heroChromeAdapt) {
        syncColourIconContrast();
        return;
    }

    m_heroChromeAdapt = desired;
    if (desired == HeroChromeAdapt::ForceDark) {
        m_homeChrome = nxui::Theme::dark();
    } else if (desired == HeroChromeAdapt::ForceLight) {
        m_homeChrome = nxui::Theme::light();
    } else {
        m_homeChrome = m_theme;
    }
    // Keep the user's cursor brand while chrome contrast flips.
    m_homeChrome.cursorNormal = m_theme.cursorNormal;
    m_homeChrome.cursorGlow = m_theme.cursorGlow;
    m_homeChrome.cursorCornerRadius = m_theme.cursorCornerRadius;
    m_homeChrome.cursorBorderWidth = m_theme.cursorBorderWidth;
    applyHomeChrome();
}

void WiiUMenuApp::syncHeroBackdropPresentation() {
    if (!m_steamGridDbBackdrop)
        return;
    m_steamGridDbBackdrop->setLayoutMode(presentationLayoutMode());
    updateHeroAwareChrome(true);
}

void WiiUMenuApp::applyTheme() {
    DebugLog::log("[theme-apply] widget recolor start");
    m_homeChrome = m_theme;
    m_heroChromeAdapt = HeroChromeAdapt::Off;
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
        icon->setThemeMode(m_theme.mode);
    }
    DebugLog::log("[theme-apply] widget recolor grid icons done");

    m_cursor->setColor(m_theme.cursorNormal);
    m_cursor->setCornerRadius(m_theme.cursorCornerRadius);
    m_cursor->setBorderWidth(m_theme.cursorBorderWidth);
    DebugLog::log("[theme-apply] widget recolor cursors done");

    applyHomeChrome();
    if (m_profileCarousel)
        m_profileCarousel->setTheme(&m_theme);
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
    if (m_contextMenu)
        m_contextMenu->setTheme(&m_theme);
    DebugLog::log("[theme-apply] widget recolor overlays done");

    if (m_settings)
        m_settings->setTheme(&m_theme);
    if (m_themeShop)
        m_themeShop->setTheme(&m_theme);
    if (m_gameOptions)
        m_gameOptions->setTheme(&m_theme);
    if (m_steamGridDbPicker)
        m_steamGridDbPicker->setTheme(&m_theme);
    if (m_folderOptions)
        m_folderOptions->setTheme(&m_theme);
    if (m_controllerTest)
        m_controllerTest->setTheme(&m_theme);
    if (m_quickSettings)
        m_quickSettings->setTheme(&m_theme);
    if (m_textEntry)
        m_textEntry->setTheme(&m_theme);

    // Re-evaluate hero contrast after a theme change (may re-apply chrome).
    updateHeroAwareChrome(true);
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

    DebugLog::log("[theme-apply] sidebar.reloadAssets start: pack=%s",
                  m_config.iconPack.empty() ? switchu::icons::kDefaultPackId
                                            : m_config.iconPack.c_str());
    reloadSidebarIcons();
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

    if (!soundPreset.empty() && m_config.soundPreset == soundPreset) {
        m_config.soundPreset = "wiiu";
        changeSoundPreset(m_config.soundPreset);
    }

    {
        std::string folder = idToDelete;
        if (!installPath.empty()) {
            const auto pos = installPath.find_last_of("/\\");
            folder = (pos == std::string::npos) ? installPath : installPath.substr(pos + 1);
        }
        // package:slug ids store the folder under installPath; strip prefix for pack ids.
        if (folder.rfind("package:", 0) == 0)
            folder = folder.substr(8);
        const std::string themePackId = switchu::icons::makeThemePackId(folder);
        const std::string legacyPackId = switchu::icons::makeThemePackId(idToDelete);
        std::string companionPackId;
        if (const ThemePreset* dying = findPresetPtr(idToDelete))
            companionPackId = dying->icons.packId;
        // Snapshot before preset list changes: folder-named extract is orphaned
        // when the theme goes away (unless it is a builtin like atmos).
        if (!folder.empty() && !isBuiltinIconPack(folder)) {
            const std::string orphan =
                switchu::icons::IconPackInstaller::destinationRootFor(folder);
            if (pathExists(orphan)) {
                switchu::icons::IconPackInstaller::remove(folder);
                DebugLog::log("[icon-pack] removed theme orphan pack=%s", folder.c_str());
            }
        }
        if ((!themePackId.empty() && m_config.iconPack == themePackId)
            || (!legacyPackId.empty() && m_config.iconPack == legacyPackId)
            || (!folder.empty() && m_config.iconPack == folder)
            || (switchu::icons::isThemePackId(m_config.iconPack)
                && switchu::icons::themeFolderFromPackId(m_config.iconPack) == folder)) {
            m_config.iconPack = switchu::icons::kDefaultPackId;
            m_config.save();
            DebugLog::log("[icon-pack] theme removed; pack reset to default");
        }
        (void)companionPackId; // builtins referenced via packId are kept
    }

    // Leave the package BEFORE deleting files — otherwise open fonts/textures can
    // lock the folder, remove_all fails, and Community still thinks it's installed.
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
            m_forceThemeResourceReload = true;
        }
        rebuildThemeFromColors();
    }

    if (source == ThemePresetSource::InstalledPackage && !installPath.empty()) {
        if (m_themeDeleteFuture.valid()) {
            m_themeDeleteFuture.wait();
            m_themeDeleteFuture = {};
        }
        // Sync remove so Installed → Community reinstall does not race an orphan folder.
        const bool removed = removeDirectoryRecursive(installPath);
        DebugLog::log("[theme-delete] removal %s: %s",
                      removed ? "complete" : "failed", safeLogPath(installPath));
        if (!removed || pathExists(installPath)) {
            // One more pass after resources have switched away.
            const bool retry = removeDirectoryRecursive(installPath);
            DebugLog::log("[theme-delete] retry %s: %s",
                          retry ? "complete" : "failed", safeLogPath(installPath));
        }
    }

    refreshThemeShopState();
    // Removing from the product sheet used to leave detail open on the next
    // theme (selection falls through after the list rebuild). Return to shelf.
    if (m_themeShop && m_themeShop->detailOpen())
        m_themeShop->closeDetailView();
    m_audio.playSfx(Sfx::ConfirmPositive);
}

void WiiUMenuApp::startSoftwareDeletion(uint64_t titleId, const std::string& title,
                                        bool closeGameOptionsOnSuccess) {
    if (m_softwareDeleteFuture.valid()
        && m_softwareDeleteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    auto& i18n = nxui::I18n::instance();
    m_softwareDeleteTitleId = titleId;
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
        // Drop NACP/icon cache so a reinstall with a new icon is not stuck
        // on the previous control_cache entry.
        if (m_softwareDeleteTitleId != 0)
            switchu::control_cache::remove(m_softwareDeleteTitleId);
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

    m_softwareDeleteTitleId = 0;
    m_softwareDeleteTitle.clear();
    m_softwareDeleteClosesGameOptions = false;
}

void WiiUMenuApp::rematchIconPackTintsToTheme(bool syncSidebar) {
    float accentH = m_activeColors.accentH;
    float accentS = m_activeColors.accentS;
    float accentL = m_activeColors.accentL;
    if (accentS < 0.05f && accentL < 0.08f
        && (m_activeColors.cursorS >= 0.05f || m_activeColors.cursorL >= 0.08f)) {
        accentH = m_activeColors.cursorH;
        accentS = std::max(0.f, m_activeColors.cursorS);
        accentL = std::max(0.f, m_activeColors.cursorL);
    }
    if (m_themeShop)
        m_themeShop->setThemeMatchAccent(accentH, accentS, accentL);

    // Rematch every pack that already has Match on. Do NOT move/clear Match
    // when a theme switches the active icon pack (e.g. iiSU) - Atmos keeps Auto.
    bool any = false;
    for (auto& [packId, tint] : m_config.iconPackTints) {
        if (!tint.matchTheme)
            continue;
        float meanH = 0.f, meanS = 0.45f, meanL = 0.55f;
        const std::string sample =
            switchu::icons::resolveGlyphPath(SD_ASSETS, packId, "album.png");
        if (!sample.empty())
            switchu::icons::probeIconMeanHsl(sample, meanH, meanS, meanL);
        const auto pivots = switchu::icons::computeMatchThemePivots(
            meanH, meanS, meanL, accentH, accentS, accentL);
        tint.enabled = true;
        tint.matchTheme = true;
        if (tint.grayscale) {
            // Absolute accent H/S for grayscale colorize; keep Light pivot.
            tint.hue = accentH - std::floor(accentH);
            tint.sat = accentS < 0.08f ? 0.45f : accentS;
            tint.light = pivots.light;
        } else {
            tint.hue = pivots.hue;
            tint.sat = pivots.sat;
            tint.light = pivots.light;
        }
        any = true;
    }
    if (!any)
        return;

    m_config.save();
    if (syncSidebar)
        syncColourIconContrast();
    // Push rematched tints into Theme Shop before invalidating so hslKeys match.
    if (m_themeShop) {
        std::unordered_map<std::string, ThemeShopScreen::IconPackTintState> tints;
        for (const auto& [packId, tint] : m_config.iconPackTints) {
            ThemeShopScreen::IconPackTintState state;
            state.enabled = tint.enabled;
            state.flatten = tint.flatten;
            state.grayscale = tint.grayscale;
            state.matchTheme = tint.matchTheme;
            state.hue = tint.hue;
            state.sat = tint.sat;
            state.light = tint.light;
            tints[packId] = state;
        }
        m_themeShop->setIconPackTintState(tints);
        m_themeShop->setThemeMatchAccent(accentH, accentS, accentL);
        m_themeShop->invalidateIconPackPreviews();
    }
}

void WiiUMenuApp::syncColourIconContrast() {
    // Single-row + hero: colour icons follow HOME chrome (same light/dark flip
    // as mono glyphs). No hero / grid: authored colour, optionally HSL-shifted
    // when custom tint is on. Theme > Options arms the feature.
    const bool fullHero =
        presentationLayoutMode() == AppLayoutMode::DynamicLine
        && m_steamGridDbBackdrop
        && m_steamGridDbBackdrop->hasDisplayedHero();
    const bool adaptColour = m_config.iconContrastAdapt && fullHero;

    const std::string activePackId = resolvedIconPackId();
    const AppConfig::IconPackTint packTint = m_config.iconPackTintFor(activePackId);
    switchu::icons::HslAdjust hsl;
    hsl.enabled = packTint.enabled;
    hsl.flatten = packTint.flatten;
    hsl.grayscale = packTint.grayscale;
    hsl.hue = std::clamp(packTint.hue, 0.f, 1.f);
    hsl.sat = std::clamp(packTint.sat, 0.f, 1.f);
    hsl.light = std::clamp(packTint.light, 0.f, 1.f);

    const auto& prev = m_sidebar.iconHslAdjust();
    const bool hslChanged =
        prev.enabled != hsl.enabled
        || prev.flatten != hsl.flatten
        || prev.grayscale != hsl.grayscale
        || std::fabs(prev.hue - hsl.hue) > 0.0005f
        || std::fabs(prev.sat - hsl.sat) > 0.0005f
        || std::fabs(prev.light - hsl.light) > 0.0005f;

    m_sidebar.setIconHslAdjust(hsl);
    if (m_layoutFlip)
        m_layoutFlip->setIconHslAdjust(hsl);
    m_sidebar.setColourIconContrast(adaptColour);
    if (m_layoutFlip)
        m_layoutFlip->setColourIconContrast(adaptColour);

    if (hslChanged) {
        // Rebake colour glyphs (and Change View) with the new HSL adjust.
        // Do not log the unchanged path — syncColourIconContrast runs every
        // frame from updateHeroAwareChrome; SD log spam soft-locks the console.
        reloadSidebarIcons();
        m_sidebar.setColourIconContrast(adaptColour);
        if (m_layoutFlip)
            m_layoutFlip->setColourIconContrast(adaptColour);
    }
}
