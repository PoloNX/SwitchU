#include "WiiUMenuApp.hpp"
#include <switchu/sd_commit.hpp>
#include "widgets/GlossyIcon.hpp"
#include "widgets/FolderPalette.hpp"
#include "themeshop/ThemeHttp.hpp"
#include <nxui/core/Animation.hpp>
#include <nxui/core/I18n.hpp>
#include "DebugLog.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include <switch.h>
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <system_error>
#include <tuple>
#include <ctime>
#include <random>
#include <optional>

namespace {

#ifdef SWITCHU_MENU
std::optional<std::uint64_t> queryApplicationPlaytimeSeconds(std::uint64_t titleId) {
    if (titleId == 0) return std::nullopt;
    PdmApplicationPlayStatistics statistics{};
    s32 total = 0;
    const u64 applicationId = titleId;
    const Result result = appletQueryApplicationPlayStatistics(
        &statistics, &applicationId, 1, &total);
    if (R_FAILED(result) || total <= 0 || statistics.application_id != titleId)
        return std::nullopt;
    constexpr std::uint64_t kNanosecondsPerSecond = 1000000000ULL;
    return statistics.playtime / kNanosecondsPerSecond;
}
#endif

static constexpr const char* kLayoutPath = "sdmc:/config/SwitchU/layout.json";
static constexpr int kMinHomePages = 8;
static constexpr const char* kBuiltInSoundPreset = "wiiu";

static constexpr float kGridRectX = 0.f;
static constexpr float kGridRectY = 90.f;
static constexpr float kGridRectW = 1280.f;
static constexpr float kGridRectH = 540.f;

static constexpr float kGridBaseCellW = 150.f;
static constexpr float kGridBaseCellH = 150.f;
static constexpr float kGridBasePadX  = 20.f;
static constexpr float kGridBasePadY  = 16.f;

GridModel compactDynamicLineEntries(const GridModel& source) {
    GridModel compacted;
    const int displaySlots = source.count();
    for (const auto& entry : source.entries()) {
        if (entry.kind == GridEntryKind::Empty ||
            entry.kind == GridEntryKind::WidgetContinuation)
            continue;
        compacted.addEntry(entry);
    }
    // Keep addable capacity, but only after the final visible item.
    while (compacted.count() < displaySlots)
        compacted.addEntry({});
    return compacted;
}

bool isPackageSoundPreset(const std::string& preset) {
    return preset.rfind("package:", 0) == 0;
}

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool directoryExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

std::string resolveAudioOverridePath(const std::string& preferredBase,
                                    const std::string& fallbackBase,
                                    const char* relativePath) {
    if (!preferredBase.empty()) {
        const std::string preferredPath = preferredBase + "/" + relativePath;
        if (pathExists(preferredPath))
            return preferredPath;
    }
    return fallbackBase + "/" + relativePath;
}

std::string installedThemePathFromPackagePreset(const std::string& preset) {
    if (!isPackageSoundPreset(preset))
        return {};

    const std::string slug = preset.substr(std::strlen("package:"));
    if (slug.empty())
        return {};

    const std::string installPath = std::string("sdmc:/config/SwitchU/themes/") + slug;
    return directoryExists(installPath) ? installPath : std::string();
}

std::string resolveThemeSoundBase(const std::string& installPath) {
    if (installPath.empty())
        return {};

    const std::string directSfx = installPath + "/sfx";
    const std::string directMusic = installPath + "/music";
    const std::string soundsRoot = installPath + "/sounds";

    const bool hasDirect = directoryExists(directSfx) || directoryExists(directMusic);
    if (hasDirect)
        return installPath;
    if (directoryExists(soundsRoot))
        return soundsRoot;
    return {};
}

// Keep enough side/top clearance so large grids do not overlap HUD/side buttons.
static constexpr float kGridSafeSideMargin = 220.f;

// Contextual action capsules, bottom-right.
static constexpr float kHintIconScale = 0.72f;
static constexpr float kHintTextScale = 0.62f;
static constexpr float kHintCapH      = 28.f;
static constexpr float kHintCapPadX   = 12.f;
static constexpr float kHintIconGap   = 6.f;
static constexpr float kHintCapGap    = 8.f;
static constexpr float kHintRowGap    = 7.f;
static constexpr float kHintEdgeX     = 18.f;
static constexpr float kHintEdgeY     = 16.f;
// Stop short of the page dots, centred and reaching x = 721 at eight pages.
static constexpr float kHintRowMaxW   = 522.f;
static constexpr int   kHintMaxItems  = 8;
static constexpr float kHintWidthDur  = 0.22f;
static constexpr float kHintSwapDur   = 0.18f;

// Page arrows flanking the grid.
static constexpr float kPageArrowInset = 152.f;
static constexpr float kPageArrowW     = 54.f;
static constexpr float kPageArrowH     = 72.f;
static constexpr float kPageArrowFade  = 0.20f;
static constexpr float kPageArrowKick  = 0.32f;
static constexpr float kAddPageHoldDur = 1.0f;

static constexpr float kGridSafeTopBottomMargin = 20.f;

std::string titleIdToHex(uint64_t v) {
    char buf[17] = {};
    std::snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)v);
    return std::string(buf);
}

bool hexToTitleId(const std::string& s, uint64_t& out) {
    if (s.empty()) {
        out = 0;
        return false;
    }
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 16);
    if (end == s.c_str() || *end != '\0') {
        out = 0;
        return false;
    }
    out = (uint64_t)v;
    return true;
}

int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool hexToAccountUid(const std::string& s, AccountUid& out) {
    if (s.size() != 32)
        return false;

    AccountUid uid{};
    for (int part = 0; part < 2; ++part) {
        uint64_t value = 0;
        for (int i = 0; i < 16; ++i) {
            int nibble = hexNibble(s[(size_t)(part * 16 + i)]);
            if (nibble < 0)
                return false;
            value = (value << 4) | (uint64_t)nibble;
        }
        uid.uid[part] = value;
    }

    if (!accountUidIsValid(&uid))
        return false;
    out = uid;
    return true;
}

const char* safeTag(const nxui::Widget* widget) {
    if (!widget || widget->tag().empty())
        return "<none>";
    return widget->tag().c_str();
}

std::string utf8Codepoint(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string buttonGlyph(nxui::Button button) {
    switch (button) {
        case nxui::Button::A: return utf8Codepoint(0xE0E0);
        case nxui::Button::B: return utf8Codepoint(0xE0E1);
        case nxui::Button::X: return utf8Codepoint(0xE0E2);
        case nxui::Button::Y: return utf8Codepoint(0xE0E3);
        case nxui::Button::L: return utf8Codepoint(0xE0E4);
        case nxui::Button::R: return utf8Codepoint(0xE0E5);
        case nxui::Button::ZL: return utf8Codepoint(0xE0E6);
        case nxui::Button::ZR: return utf8Codepoint(0xE0E7);
        case nxui::Button::Plus: return utf8Codepoint(0xE0F1);
        case nxui::Button::Minus: return utf8Codepoint(0xE0F2);
        default: return {};
    }
}

std::string dpadGlyph() {
    return utf8Codepoint(0xE0EA);
}

bool appEntriesRefreshEquivalent(const AppEntry& a, const AppEntry& b) {
    return a.id == b.id &&
           a.title == b.title &&
           a.titleId == b.titleId &&
           a.viewFlags == b.viewFlags &&
           a.userRequired == b.userRequired &&
           a.startupUserKnown == b.startupUserKnown &&
           a.startupUserAccount == b.startupUserAccount &&
           a.startupUserAccountOption == b.startupUserAccountOption &&
           a.kind == b.kind &&
           a.folderId == b.folderId &&
           a.folderPreviewCount == b.folderPreviewCount &&
           a.folderColorIndex == b.folderColorIndex &&
           a.widgetId == b.widgetId &&
           a.widgetType == b.widgetType &&
           a.widgetColumns == b.widgetColumns &&
           a.widgetRows == b.widgetRows &&
           a.widgetAssetRef == b.widgetAssetRef;
}

bool gridModelsRefreshEquivalent(const GridModel& a, const GridModel& b) {
    if (a.count() != b.count())
        return false;
    for (int i = 0; i < a.count(); ++i) {
        if (!appEntriesRefreshEquivalent(a.at(i), b.at(i)))
            return false;
    }
    return true;
}

}


WiiUMenuApp::WiiUMenuApp() {}
WiiUMenuApp::~WiiUMenuApp() {
    rootBox().clearChildren();
}

void WiiUMenuApp::setTutorialStartupFade(bool enabled) {
    m_tutorialStartupFade = enabled;
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::setStartupStatus(const switchu::smi::SystemStatus& status, bool fastReturn) {
    m_launcher.setStartupStatus(status.suspended_app_id, status.app_running);
    m_startupFailure = status.last_failure;
    m_fastReturnRequested = fastReturn || status.suspended_app_id != 0;
}
#endif

bool WiiUMenuApp::onCreate() {
    const std::uint64_t initStartTick = armGetSystemTick();
    auto initElapsedMs = [initStartTick]() -> unsigned long {
        return static_cast<unsigned long>(
            armTicksToNs(armGetSystemTick() - initStartTick) / 1'000'000ULL);
    };
    DebugLog::log("[init] onCreate enter");
    m_iconStreamer.setThreadPool(&m_threadPool);
    m_config.load();
    m_appLayoutMode = m_config.appLayoutMode;
    loadMenuLayout();
    if (!m_folderStore.load())
        DebugLog::log("[folders] store unavailable; continuing with an empty folder list");
    if (!m_widgetStore.load())
        DebugLog::log("[widgets] store unavailable; continuing with an empty widget list");
    normalizeWidgetPlacements();
    if (m_widgetStore.recentActivity().titleId != 0) {
        refreshRecentActivityDuration();
        if (!m_widgetStore.save())
            DebugLog::log("[widgets] recent activity duration could not be saved");
    }
    m_appLoader.setPendingTransform([this](std::vector<PendingApp>& apps) {
        applyMenuLayoutToPending(apps);
    });

    nxui::I18n::instance().initialize(std::string(SD_ASSETS) + "/i18n", "en-US");
    applyUiLanguage();
    const bool fastReturn = m_fastReturnRequested || m_launcher.suspendedTitleId() != 0;
    m_fastReturnRequested = fastReturn;
    m_fastReturnStartupTick = fastReturn ? initStartTick : 0;
    m_appLoader.setPrefetchIcons(!fastReturn);

    const std::string accessibilityVoice = nxui::I18n::instance().activeLanguageTag();
    const int accessibilityRate = m_config.accessibilitySpeechRate;
    m_accessibility.configure(m_config.accessibilityEnabled, accessibilityVoice);
    m_accessibility.setSpeakHints(m_config.accessibilitySpeakHints);
    m_accessibility.setSpeakContextEveryFocus(m_config.accessibilitySpeakContextEveryFocus);
    m_accessibility.setSpeechRate(m_config.accessibilitySpeechRate);
    if (fastReturn) {
        m_accessibilityFuture = m_threadPool.submit([this, accessibilityVoice, accessibilityRate]() {
            m_accessibility.initializeConfigured(SD_ASSETS, accessibilityVoice,
                                                  accessibilityRate);
        });
        DebugLog::log("[init] accessibility deferred for fast return at %lums",
                      initElapsedMs());
    } else {
        m_accessibilityReady = m_accessibility.initializeConfigured(
            SD_ASSETS, accessibilityVoice, accessibilityRate);
    }

    // Opening the audio session here froze the console when a game was still
    // suspended. Hold it back until the menu has rendered and settled.
    m_audioInitPending = true;
    DebugLog::log("[init] Audio initialisation deferred past the first frames");

    DebugLog::log("[init] Bluetooth audio manager deferred until its Settings tab is opened");
    DebugLog::log("[init] Theme Shop HTTP runtime deferred until first request");

    DebugLog::log("[init] Config loaded (theme=%s, musicVol=%.2f, sfxVol=%.2f)",
                  m_config.themePreset.c_str(), m_config.musicVolume, m_config.sfxVolume);

    m_launcher.init({
        .playSfxModalHide = [this]() { m_audio.playSfx(Sfx::ModalHide); },
        .requestExit = [this]() { app().requestExit(); },
        .beforePowerAction = [this]() { return quiesceWritersForPowerAction(); },
    });


    DebugLog::log("[init] loadResources...");
    loadResources();
    DebugLog::log("[init] loadResources done at %lums", initElapsedMs());
    DebugLog::log("[init] buildGrid...");
    buildGrid();
    DebugLog::log("[init] buildGrid done at %lums", initElapsedMs());

#ifdef SWITCHU_MENU
    if (m_startupFailure.result != 0 && m_dialog) {
        auto commandName = [](uint32_t command) -> const char* {
            using Message = switchu::smi::SystemMessage;
            switch (static_cast<Message>(command)) {
                case Message::LaunchAlbum: return "Album";
                case Message::LaunchMiiEditor: return "MiiEditor";
                case Message::LaunchControllers: return "Controllers";
                case Message::LaunchNetConnect: return "NetConnect";
                case Message::LaunchUserPage: return "UserPage";
                case Message::LaunchControllerRemapping: return "ControllerRemapping";
                case Message::LaunchUserCreator: return "UserCreator";
                default: return "SystemAction";
            }
        };
        char details[240]{};
        std::snprintf(details, sizeof(details),
                      "%s failed.\nCommand: %u · Request: %lu\nResult: 0x%X · Module: %u · Description: %u",
                      commandName(m_startupFailure.command),
                      m_startupFailure.command,
                      static_cast<unsigned long>(m_startupFailure.request_id),
                      m_startupFailure.result,
                      R_MODULE(m_startupFailure.result),
                      R_DESCRIPTION(m_startupFailure.result));
        m_dialog->show(nxui::I18n::instance().tr("error.operation_failed", "Operation failed"),
                       details,
                       {{nxui::I18n::instance().tr("button.ok", "OK"), {}, true}});
        focusManager().setFocus(m_dialog.get());
    }
#endif

#ifdef SWITCHU_DEBUG_UI
    m_debugOverlay = std::make_unique<DebugImGuiOverlay>();
    if (!m_debugOverlay->initialize(app().gpu(), app().renderer())) {
        DebugLog::log("[debug] ImGui overlay init failed");
        m_debugOverlay.reset();
    } else {
        DebugLog::log("[debug] ImGui overlay ready");
    }
#endif

#ifdef SWITCHU_MENU
    m_sysMsg.setCallback([this](SysAction a) { handleSystemAction(a); });
    DebugLog::log("[init] async notifications via AppletStorage only");
    switchu::menu::smi_cmd::menuReady();
#endif

    DebugLog::log("[init] DONE total=%lums fastReturn=%d",
                  initElapsedMs(), fastReturn ? 1 : 0);
    return true;
}

bool WiiUMenuApp::quiesceWritersForPowerAction() {
    // Power actions do not destroy the activity first, so explicitly wait for
    // every SD writer owned by the menu. Keep the futures valid where normal
    // UI completion code still consumes their results.
    if (m_configSaveFuture.valid())
        m_configSaveFuture.wait();
    if (m_themeDeleteFuture.valid())
        m_themeDeleteFuture.wait();
    if (m_themePackageTransferFuture.valid())
        m_themePackageTransferFuture.wait();
    if (m_softwareDeleteFuture.valid())
        m_softwareDeleteFuture.wait();
    if (m_layoutDirty)
        saveMenuLayout();

    const bool committed = switchu::commitSdCard("power action");
    DebugLog::log("[power] writers quiesced; SD commit=%s",
                  committed ? "ok" : "failed");
    return committed;
}

void WiiUMenuApp::onDestroy() {
    // All background jobs may reference services or members owned by this
    // activity. Drain them while the entire object graph is still alive, and
    // only then shut down HTTP, Bluetooth, accessibility and audio.
    m_iconStreamer.cancelPending();
    if (m_recentWidgetAssetDecode)
        m_recentWidgetAssetDecode->cancelled.store(true);
    if (m_gameArtworkDecode)
        m_gameArtworkDecode->cancelled.store(true);
    m_threadPool.shutdown();

    if (m_accessibilityFuture.valid()) {
        try {
            m_accessibilityFuture.get();
        } catch (const std::exception& ex) {
            DebugLog::log("[shutdown] accessibility task failed: %s", ex.what());
        } catch (...) {
            DebugLog::log("[shutdown] accessibility task failed: unknown exception");
        }
    }

    if (m_audioFuture.valid()) {
        try {
            m_audioFuture.get();
        } catch (const std::exception& ex) {
            DebugLog::log("[shutdown] audio task failed: %s", ex.what());
        } catch (...) {
            DebugLog::log("[shutdown] audio task failed: unknown exception");
        }
    }
    if (m_themePackageTransferFuture.valid()) {
        try {
            m_themePackageTransferFuture.get();
        } catch (const std::exception& ex) {
            DebugLog::log("[shutdown] theme transfer failed: %s", ex.what());
        } catch (...) {
            DebugLog::log("[shutdown] theme transfer failed: unknown exception");
        }
    }

#ifdef SWITCHU_DEBUG_UI
    if (m_debugOverlay) {
        m_debugOverlay->shutdown(app().gpu());
        m_debugOverlay.reset();
    }
#endif

    stopEditGhost();

    m_steamGridDb.cancelAndWait();
    if (m_steamGridDbBrowseFuture.valid()) m_steamGridDbBrowseFuture.wait();
    if (m_steamGridDbApplyFuture.valid()) m_steamGridDbApplyFuture.wait();
    if (m_steamGridDbPicker) m_steamGridDbPicker->wait();
    themeshop::http::shutdown();

    bluetooth::Finalize();

#ifdef SWITCHU_MENU
    switchu::menu::smi_cmd::menuClosing();
#endif
    if (m_layoutDirty)
        saveMenuLayout();
    m_accessibility.shutdown();
    m_audio.shutdown();
}

void WiiUMenuApp::loadResources() {
    DebugLog::log("[init] loadResources: regular font");
    std::string fontPath = std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf";
    if (m_fontNormal.load(app().gpu(), app().renderer(), fontPath, 24))
        m_loadedRegularFontPath = fontPath;
    DebugLog::log("[init] loadResources: small font");
    if (m_fontSmall.load(app().gpu(), app().renderer(), fontPath, 18))
        m_loadedSmallFontPath = fontPath;
    DebugLog::log("[init] loadResources: icon font");
    m_fontIcons.load(app().gpu(), app().renderer(), std::string(SD_ASSETS) + "/fonts/switch_icons.ttf", 24);

    if (m_fastReturnRequested) {
        m_deferredStaticTextures = true;
        DebugLog::log("[init] loadResources: static textures deferred past first frame");
    } else {
        loadStaticTextures();
    }

    DebugLog::log("[init] loadResources: application catalog");
    m_appLoader.load(m_model, m_iconStreamer);
    DebugLog::log("[init] loadResources: done");
}

void WiiUMenuApp::loadStaticTextures() {
    DebugLog::log("[init] loadResources: static textures");
    std::string gameCardPath = std::string(SD_ASSETS) + "/icons/gamecard.png";
    if (m_gameCardTex.loadFromFile(app().gpu(), app().renderer(), gameCardPath))
        m_loadedGameCardPath = gameCardPath;

    m_arrowTexLeft.loadFromFile(app().gpu(), app().renderer(),
                                std::string(SD_ASSETS) + "/icons/page_arrow_left.png");
    m_arrowTexRight.loadFromFile(app().gpu(), app().renderer(),
                                 std::string(SD_ASSETS) + "/icons/page_arrow_right.png");
    m_batteryConsoleTex.loadFromFile(app().gpu(), app().renderer(),
                                     std::string(SD_ASSETS) + "/icons/widget_battery_switch.png");
    m_batteryJoyconLeftTex.loadFromFile(app().gpu(), app().renderer(),
                                        std::string(SD_ASSETS) + "/icons/widget_battery_joycon_left.png");
    m_batteryJoyconRightTex.loadFromFile(app().gpu(), app().renderer(),
                                         std::string(SD_ASSETS) + "/icons/widget_battery_joycon_right.png");
}

void WiiUMenuApp::buildUserAvatarBar(bool loadImmediately) {
    m_userAvatarButtons.clear();

    if (!m_userAvatarBar) {
        m_userAvatarBar = std::make_shared<nxui::Box>(nxui::Axis::ROW);
        m_userAvatarBar->setMarginTop(17.f);
        m_userAvatarBar->setGap(10.f);
        m_userAvatarBar->setShrink(0.f);
        m_userAvatarBar->setSize(0.f, 56.f);
        m_userAvatarBar->setTag("userAvatarBar");
        m_userAvatarBar->setWireframeEnabled(false);
    } else {
        m_userAvatarBar->clearChildren();
        m_userAvatarBar->setSize(0.f, 56.f);
    }

    m_pendingProfileUids.clear();
    m_pendingProfileIndex = 0;
    if (!loadImmediately && m_fastReturnRequested) {
        auto state = std::make_shared<DeferredProfileList>();
        m_deferredProfileList = state;
        m_deferredProfileListFuture = m_threadPool.submit([state]() {
            AccountUid uids[8] = {};
            s32 count = 0;
            state->result = accountListAllUsers(uids, 8, &count);
            if (R_SUCCEEDED(state->result) && count > 0)
                state->uids.assign(uids, uids + count);
        });
        DebugLog::log("[profiles] account enumeration deferred off fast-return path");
        return;
    }

    AccountUid uids[8] = {};
    s32 count = 0;
    Result rc = accountListAllUsers(uids, 8, &count);
    DebugLog::log("[profiles] accountListAllUsers rc=0x%X count=%d", rc, count);
    if (R_SUCCEEDED(rc) && count > 0)
        m_pendingProfileUids.assign(uids, uids + count);

    if (loadImmediately) {
        while (m_pendingProfileIndex < m_pendingProfileUids.size())
            loadNextUserAvatar();
        appendAddUserButton();
    } else if (!m_pendingProfileUids.empty()) {
        m_deferredProfileFrames = 1;
        DebugLog::log("[profiles] %d avatars deferred until after first frame", count);
    } else {
        appendAddUserButton();
    }
}

void WiiUMenuApp::appendAddUserButton() {
    if (!m_userAvatarBar || m_pendingProfileUids.size() >= 8)
        return;
    if (!m_userAvatarButtons.empty() && m_userAvatarButtons.back()->addUserMode())
        return;

    auto add = std::make_shared<UserAvatarButton>();
    add->setSize(56.f, 56.f);
    add->setMinWidth(56.f);
    add->setMinHeight(56.f);
    add->setShrink(0.f);
    add->setCornerRadius(28.f);
    add->setChromeEnabled(true);
    add->setTheme(&m_theme);
    add->setAddUserMode(true);
    add->setFocusable(true);
    add->setOnActivate([this]() {
        m_audio.playSfx(Sfx::Activate);
#ifdef SWITCHU_MENU
        m_launcher.launchUserCreator();
#endif
    });
    m_userAvatarButtons.push_back(add);
    m_userAvatarBar->addChild(add);

    const float countF = static_cast<float>(m_userAvatarButtons.size());
    m_userAvatarBar->setSize(countF * 56.f + (countF - 1.f) * 10.f, 56.f);
    wireUserAvatarNavigation();
    if (m_topHud)
        m_topHud->layout();
    DebugLog::log("[profiles] add-user tile appended count=%d",
                  static_cast<int>(m_pendingProfileUids.size()));
}

void WiiUMenuApp::wireUserAvatarNavigation() {
    const bool dynamicLine = m_appLayoutMode == AppLayoutMode::DynamicLine;
    auto returnToRow = [this]() {
        if (!m_grid || m_appLayoutMode != AppLayoutMode::DynamicLine
            || m_navigator.route() != switchu::navigation::Route::Home)
            return;
        if (auto* target = m_grid->focusManager().current())
            focusManager().setFocus(target);
    };
    for (std::size_t i = 0; i < m_userAvatarButtons.size(); ++i) {
        auto* current = m_userAvatarButtons[i].get();
        nxui::Widget* left = current;
        if (i > 0)
            left = m_userAvatarButtons[i - 1].get();
        else if (dynamicLine && !m_sidebar.leftButtons().empty())
            left = m_sidebar.leftButtons().back().get();
        nxui::Widget* right = current;
        if (i + 1 < m_userAvatarButtons.size())
            right = m_userAvatarButtons[i + 1].get();
        else if (dynamicLine && !m_sidebar.rightButtons().empty())
            right = m_sidebar.rightButtons().front().get();
        current->setCustomNavigation(nxui::FocusDirection::LEFT, left);
        current->setCustomNavigation(nxui::FocusDirection::RIGHT, right);
        current->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
        current->removeAction(static_cast<uint64_t>(nxui::Button::DDown));
        current->removeAction(static_cast<uint64_t>(nxui::Button::LStickD));
        current->removeAction(static_cast<uint64_t>(nxui::Button::RStickD));
        if (dynamicLine)
            current->addDirectionAction(nxui::FocusDirection::DOWN, returnToRow);
    }
    if (dynamicLine && !m_userAvatarButtons.empty()) {
        if (m_grid)
            m_grid->setDynamicLineUpTarget(
                m_userAvatarButtons[m_userAvatarButtons.size() / 2].get());
        m_sidebar.setDynamicLineUpTarget(
            m_userAvatarButtons[m_userAvatarButtons.size() / 2].get());
        m_sidebar.setDynamicLineProfileTargets(m_userAvatarButtons.front().get(),
                                               m_userAvatarButtons.back().get());
    } else {
        m_sidebar.setDynamicLineProfileTargets(nullptr, nullptr);
    }
}

void WiiUMenuApp::loadNextUserAvatar() {
    if (!m_userAvatarBar || m_pendingProfileIndex >= m_pendingProfileUids.size())
        return;

    const AccountUid uid = m_pendingProfileUids[m_pendingProfileIndex++];
    AccountProfile profile{};
    Result rc = accountGetProfile(&profile, uid);
    if (R_FAILED(rc))
        return;

    auto avatar = std::make_shared<UserAvatarButton>();
    avatar->setSize(56.f, 56.f);
    avatar->setMinWidth(56.f);
    avatar->setMinHeight(56.f);
    avatar->setShrink(0.f);
    avatar->setCornerRadius(28.f);
    avatar->setChromeEnabled(true);
    avatar->setTheme(&m_theme);
    avatar->setUid(uid);
    avatar->setFocusable(true);

    AccountProfileBase base{};
    AccountUserData userData{};
    if (R_SUCCEEDED(accountProfileGet(&profile, &userData, &base)))
        avatar->setNickname(base.nickname);

    u32 imgSize = 0;
    if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imgSize)) && imgSize > 0) {
        std::vector<uint8_t> imgBuf(imgSize);
        u32 realSize = 0;
        if (R_SUCCEEDED(accountProfileLoadImage(&profile, imgBuf.data(), imgSize, &realSize))
                && realSize > 0) {
            avatar->loadAvatar(app().gpu(), app().renderer(), imgBuf.data(), realSize);
        }
    }

    avatar->setOnActivate([this, uid]() {
        m_audio.playSfx(Sfx::Activate);
#ifdef SWITCHU_MENU
        m_launcher.launchUserPage(uid);
#endif
    });

    accountProfileClose(&profile);
    m_userAvatarButtons.push_back(avatar);
    m_userAvatarBar->addChild(avatar);

    if (!m_userAvatarButtons.empty()) {
        const float countF = static_cast<float>(m_userAvatarButtons.size());
        m_userAvatarBar->setSize(countF * 56.f + (countF - 1.f) * 10.f, 56.f);
        wireUserAvatarNavigation();
    }

    if (m_topHud)
        m_topHud->layout();

    if (m_pendingProfileIndex >= m_pendingProfileUids.size())
        appendAddUserButton();
}

WiiUMenuApp::GridLayoutMetrics WiiUMenuApp::computeGridLayoutMetrics() const {
    const int cols = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);
    return computeGridLayoutMetrics(cols, rows);
}

WiiUMenuApp::GridLayoutMetrics WiiUMenuApp::computeGridLayoutMetrics(int cols,
                                                                      int rows) const {
    cols = std::clamp(cols, 3, 8);
    rows = std::clamp(rows, 2, 5);

    const float baseGridW = cols * kGridBaseCellW + (cols - 1) * kGridBasePadX;
    const float baseGridH = rows * kGridBaseCellH + (rows - 1) * kGridBasePadY;

    const float safeW = std::max(1.f, kGridRectW - (kGridSafeSideMargin * 2.f));
    const float safeH = std::max(1.f, kGridRectH - (kGridSafeTopBottomMargin * 2.f));

    const float scaleW = safeW / baseGridW;
    const float scaleH = safeH / baseGridH;
    const float scale = std::min(1.f, std::min(scaleW, scaleH));

    GridLayoutMetrics m;
    m.cellW = std::max(88.f, kGridBaseCellW * scale);
    m.cellH = std::max(88.f, kGridBaseCellH * scale);
    m.padX = std::max(8.f, kGridBasePadX * scale);
    m.padY = std::max(8.f, kGridBasePadY * scale);
    return m;
}

std::pair<int, int> WiiUMenuApp::folderGridDimensions(std::uint32_t folderId) const {
    const auto* folder = m_folderStore.find(folderId);
    const int size = folder ? std::clamp(folder->sizeIndex, 0, 2) : 1;
    switch (size) {
        case 0: return {4, 2};
        case 2: return {6, 4};
        default: return {5, 3};
    }
}

void WiiUMenuApp::reflowHomeGrid() {
    if (!m_grid)
        return;

    // Folder-aware models include synthetic entries which must never be sent
    // to the icon loader as application title IDs. Rebuild them through the
    // same composition path used at startup when grid dimensions change.
    if (!m_allApps.empty() && m_openFolderId == 0) {
        std::uint64_t focused = 0;
        if (auto* current = m_grid->focusManager().current();
            current && current->tag() == "glossy_icon")
            focused = static_cast<GlossyIcon*>(current)->titleId();
        applyDisplayModel(buildRootFolderModel(), focused, false);
        if (m_layoutDirty) saveMenuLayout();
        return;
    }

    const int oldFocusedIndex = m_grid->focusedGlobalIndex();
    const int oldPage = m_grid->currentPage();
    uint64_t focusedTitleId = 0;
    if (oldFocusedIndex >= 0 && oldFocusedIndex < m_model.count())
        focusedTitleId = m_model.at(oldFocusedIndex).titleId;

    std::unordered_map<uint64_t, AppEntry> byId;
    std::vector<uint64_t> appOrder;
    byId.reserve((size_t)std::max(0, m_model.count()));
    appOrder.reserve((size_t)std::max(0, m_model.count()));
    for (const auto& entry : m_model.entries()) {
        if (entry.titleId == 0 || byId.count(entry.titleId))
            continue;
        appOrder.push_back(entry.titleId);
        byId.emplace(entry.titleId, entry);
    }

    std::vector<uint64_t> slots = m_layoutSlots;
    if (slots.empty())
        slots = appOrder;

    std::unordered_set<uint64_t> placed;
    placed.reserve(byId.size());
    for (auto& slotTid : slots) {
        if (slotTid == 0)
            continue;
        if (!byId.count(slotTid) || placed.count(slotTid)) {
            slotTid = 0;
            continue;
        }
        placed.insert(slotTid);
    }

    for (uint64_t tid : appOrder) {
        if (placed.count(tid))
            continue;

        auto emptyIt = std::find(slots.begin(), slots.end(), 0);
        if (emptyIt != slots.end())
            *emptyIt = tid;
        else
            slots.push_back(tid);
        placed.insert(tid);
    }

    const int cols = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);
    const int perPage = std::max(1, cols * rows);
    int minSlots = std::max(perPage * kMinHomePages, (int)slots.size());
    int roundedSlots = ((minSlots + perPage - 1) / perPage) * perPage;
    if ((int)slots.size() < roundedSlots)
        slots.resize(roundedSlots, 0);

    if (slots != m_layoutSlots) {
        m_layoutSlots = slots;
        m_layoutDirty = true;
    }

    GridModel rebuiltModel;
    for (uint64_t tid : slots) {
        if (tid == 0) {
            rebuiltModel.addEntry(AppEntry{});
            continue;
        }

        auto it = byId.find(tid);
        if (it != byId.end())
            rebuiltModel.addEntry(it->second);
        else
            rebuiltModel.addEntry(AppEntry{});
    }

    std::vector<std::shared_ptr<GlossyIcon>> icons;
    icons.reserve((size_t)std::max(0, rebuiltModel.count()));
    const auto& oldIcons = m_grid->allIcons();
    for (int i = 0; i < rebuiltModel.count(); ++i) {
        if (i < (int)oldIcons.size() && oldIcons[i] &&
            i < m_model.count() &&
            m_model.at(i).titleId == rebuiltModel.at(i).titleId) {
            icons.push_back(oldIcons[i]);
        } else {
            auto icon = makeIcon(rebuiltModel.at(i));
            icon->setBaseColor(m_theme.iconDefault);
            icons.push_back(std::move(icon));
        }
    }

    m_model = std::move(rebuiltModel);
    m_iconStreamer.setIconDataLoader(AppListLoader::loadIconData);
    std::vector<uint64_t> reflowedTitleIds;
    reflowedTitleIds.reserve((size_t)std::max(0, m_model.count()));
    for (int i = 0; i < m_model.count(); ++i)
        reflowedTitleIds.push_back(m_model.at(i).titleId);
    m_iconStreamer.reconcileTitleIds(reflowedTitleIds);

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();
    m_grid->setup(std::move(icons), cols, rows,
                  gridMetrics.cellW, gridMetrics.cellH,
                  gridMetrics.padX, gridMetrics.padY);

    int targetIndex = -1;
    if (focusedTitleId != 0)
        targetIndex = findTitleIndex(focusedTitleId);
    if (targetIndex < 0 && oldFocusedIndex >= 0 && m_model.count() > 0)
        targetIndex = std::clamp(oldFocusedIndex, 0, m_model.count() - 1);

    if (targetIndex >= 0)
        m_grid->focusGlobalIndex(targetIndex);
    else
        m_grid->setPage(oldPage);

    for (auto* icon : m_grid->pageIcons()) {
        if (icon)
            icon->forceVisible();
    }

    m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());

    const bool overlayActive =
        (m_dialog && m_dialog->isActive()) ||
        (m_themeShop && m_themeShop->isActive()) ||
        (m_settings && m_settings->isActive()) ||
        (m_userSelect && m_userSelect->isActive());
    if (!overlayActive) {
        if (auto* cur = m_grid->focusManager().current())
            focusManager().setFocus(cur);
        updateCursor();
    }

    DebugLog::log("[grid] reflowed layout cols=%d rows=%d apps=%d page=%d",
                  cols, rows, m_model.count(), m_grid->currentPage());
}

void WiiUMenuApp::loadMenuLayout() {
    m_layoutSlots.clear();
    m_gameSizes.clear();

    std::ifstream f(kLayoutPath);
    if (!f.is_open())
        return;

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return;
    }

    if (const auto sizes = j.find("gameSizes");
        sizes != j.end() && sizes->is_array()) {
        for (const auto& item : *sizes) {
            if (!item.is_object()) continue;
            std::uint64_t titleId = 0;
            const std::string encoded = item.value("titleId", std::string());
            if (!hexToTitleId(encoded, titleId) || titleId == 0) continue;
            const int columns = item.value("columns", 1);
            const int rows = item.value("rows", 1);
            if ((columns == 1 && rows == 1) ||
                (columns == 2 && (rows == 1 || rows == 2)))
                m_gameSizes[titleId] = {columns, rows};
        }
    }

    auto it = j.find("slots");
    if (it == j.end() || !it->is_array())
        return;

    for (const auto& v : *it) {
        uint64_t tid = 0;
        if (v.is_string()) {
            std::string s = v.get<std::string>();
            if (!hexToTitleId(s, tid))
                tid = 0;
        } else if (v.is_number_unsigned()) {
            tid = v.get<uint64_t>();
        } else if (v.is_number_integer()) {
            auto raw = v.get<int64_t>();
            tid = raw > 0 ? (uint64_t)raw : 0;
        }
        m_layoutSlots.push_back(tid);
    }
}

void WiiUMenuApp::saveMenuLayout() {
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory("sdmc:/config/SwitchU", ec);

    nlohmann::json j;
    j["version"] = 1;
    j["slots"] = nlohmann::json::array();
    for (uint64_t tid : m_layoutSlots) {
        if (tid == 0)
            j["slots"].push_back("0");
        else
            j["slots"].push_back(titleIdToHex(tid));
    }
    j["gameSizes"] = nlohmann::json::array();
    for (const auto& [titleId, size] : m_gameSizes) {
        if (size == switchu::widgets::WidgetSize{1, 1}) continue;
        j["gameSizes"].push_back({
            {"titleId", titleIdToHex(titleId)},
            {"columns", size.columns},
            {"rows", size.rows},
        });
    }

    std::ofstream f(kLayoutPath, std::ios::trunc);
    if (!f.is_open())
        return;
    f << j.dump(2);
    m_layoutDirty = false;
}

switchu::widgets::WidgetSize WiiUMenuApp::gameGridSize(
    std::uint64_t titleId, AppLayoutMode mode) const {
    if (mode == AppLayoutMode::DynamicLine) return {1, 1};
    const auto found = m_gameSizes.find(titleId);
    if (found == m_gameSizes.end()) return {1, 1};
    const auto size = found->second;
    if ((size.columns == 2 && (size.rows == 1 || size.rows == 2)) ||
        (size.columns == 1 && size.rows == 1))
        return size;
    return {1, 1};
}

void WiiUMenuApp::applyMenuLayoutToPending(std::vector<PendingApp>& apps) {
    composeRootPending(apps);
}

void WiiUMenuApp::composeRootPending(std::vector<PendingApp>& apps) {
    const int cols = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);
    const int perPage = std::max(1, cols * rows);

    m_allApps.clear();
    m_allApps.reserve(apps.size());
    for (const auto& pending : apps) {
        if (pending.titleId == 0)
            continue;
        AppEntry entry;
        entry.id = pending.id;
        entry.title = pending.title;
        entry.englishTitle = pending.englishTitle;
        entry.titleId = pending.titleId;
        entry.viewFlags = pending.viewFlags;
        entry.userRequired = pending.userRequired;
        entry.startupUserKnown = pending.startupUserKnown;
        entry.startupUserAccount = pending.startupUserAccount;
        entry.startupUserAccountOption = pending.startupUserAccountOption;
        entry.kind = GridEntryKind::Application;
        const auto gameSize = gameGridSize(entry.titleId, m_appLayoutMode);
        entry.widgetColumns = gameSize.columns;
        entry.widgetRows = gameSize.rows;
        m_allApps.push_back(std::move(entry));
    }
    normalizeWidgetPlacements();

    std::unordered_map<uint64_t, PendingApp> byId;
    byId.reserve(apps.size() + m_folderStore.all().size() + m_widgetStore.all().size());
    std::vector<uint64_t> itemOrder;
    itemOrder.reserve(apps.size() + m_folderStore.all().size() + m_widgetStore.all().size());
    for (auto& app : apps) {
        if (app.titleId != 0 && m_folderStore.folderForTitle(app.titleId) == 0) {
            const auto gameSize = gameGridSize(app.titleId, m_appLayoutMode);
            app.widgetColumns = gameSize.columns;
            app.widgetRows = gameSize.rows;
            itemOrder.push_back(app.titleId);
            byId.emplace(app.titleId, std::move(app));
        }
    }
    for (const auto& folder : m_folderStore.all()) {
        PendingApp item;
        item.id = "folder:" + std::to_string(folder.id);
        item.title = folder.name;
        item.titleId = folderTitleId(folder.id);
        item.kind = GridEntryKind::Folder;
        item.folderId = folder.id;
        item.folderPreviewCount = static_cast<int>(folder.titleCount());
        item.folderColorIndex = folder.colorIndex;
        itemOrder.push_back(item.titleId);
        byId.emplace(item.titleId, std::move(item));
    }
    for (const auto& widget : m_widgetStore.all()) {
        const auto supported = switchu::widgets::supportedSizes(
            widget.type, m_appLayoutMode);
        if (supported.empty())
            continue;
        PendingApp item;
        item.id = "widget:" + std::to_string(widget.id);
        item.title = widgetTypeLabel(widget.type);
        item.titleId = switchu::widgets::widgetTitleId(widget.id);
        item.kind = GridEntryKind::Widget;
        item.widgetId = widget.id;
        item.widgetType = widget.type;
        const auto effectiveSize = switchu::widgets::validatedSize(
            widget.type, widget.size, m_appLayoutMode);
        item.widgetColumns = effectiveSize.columns;
        item.widgetRows = effectiveSize.rows;
        item.widgetAssetRef = widget.assetRef;
        itemOrder.push_back(item.titleId);
        byId.emplace(item.titleId, std::move(item));
    }

    std::vector<uint64_t> slots = m_layoutSlots;
    if (slots.empty()) {
        slots = itemOrder;
    }

    std::unordered_set<uint64_t> placed;
    placed.reserve(byId.size());
    std::unordered_set<std::uint64_t> hiddenWidgetIds;
    for (const auto& widget : m_widgetStore.all()) {
        if (switchu::widgets::supportedSizes(widget.type, m_appLayoutMode).empty())
            hiddenWidgetIds.insert(switchu::widgets::widgetTitleId(widget.id));
    }
    for (auto& slotTid : slots) {
        if (slotTid == 0)
            continue;
        if (hiddenWidgetIds.count(slotTid)) {
            placed.insert(slotTid);
            continue;
        }
        auto it = byId.find(slotTid);
        if (it == byId.end() || placed.count(slotTid)) {
            slotTid = 0;
            continue;
        }
        placed.insert(slotTid);
    }

    for (uint64_t itemId : itemOrder) {
        if (itemId == 0 || placed.count(itemId))
            continue;

        auto emptyIt = std::find(slots.begin(), slots.end(), 0);
        if (emptyIt != slots.end())
            *emptyIt = itemId;
        else
            slots.push_back(itemId);

        placed.insert(itemId);
    }

    int minSlots = std::max(perPage * kMinHomePages, (int)slots.size());
    int roundedSlots = ((minSlots + perPage - 1) / perPage) * perPage;
    if ((int)slots.size() < roundedSlots)
        slots.resize(roundedSlots, 0);

    std::vector<int> coveredBy(slots.size(), -1);
    if (m_appLayoutMode == AppLayoutMode::Grid) {
        for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
            auto found = byId.find(slots[static_cast<std::size_t>(index)]);
            if (found == byId.end() ||
                (found->second.kind != GridEntryKind::Widget &&
                 found->second.kind != GridEntryKind::Application))
                continue;
            auto& item = found->second;
            int spanColumns = std::max(1, item.widgetColumns);
            int spanRows = std::max(1, item.widgetRows);
            if (spanColumns == 1 && spanRows == 1)
                continue;
            const int pageOffset = index % perPage;
            const int column = pageOffset % cols;
            const int row = pageOffset / cols;
            bool fits = column + spanColumns <= cols && row + spanRows <= rows;
            for (int dy = 0; fits && dy < spanRows; ++dy) {
                for (int dx = 0; dx < spanColumns; ++dx) {
                    const int cell = index + dy * cols + dx;
                    if (cell >= static_cast<int>(slots.size()) ||
                        (cell != index && slots[static_cast<std::size_t>(cell)] != 0) ||
                        coveredBy[static_cast<std::size_t>(cell)] >= 0) {
                        fits = false;
                        break;
                    }
                }
            }
            if (!fits) {
                // Placement normalization normally relocates a large item.
                // Keep a safe 1x1 fallback for malformed legacy layouts.
                item.widgetColumns = 1;
                item.widgetRows = 1;
                coveredBy[static_cast<std::size_t>(index)] = index;
                continue;
            }
            for (int dy = 0; dy < spanRows; ++dy)
                for (int dx = 0; dx < spanColumns; ++dx)
                    coveredBy[static_cast<std::size_t>(index + dy * cols + dx)] = index;
        }
    }

    std::vector<PendingApp> ordered;
    ordered.reserve(slots.size());
    for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
        if (coveredBy[static_cast<std::size_t>(index)] >= 0 &&
            coveredBy[static_cast<std::size_t>(index)] != index) {
            PendingApp continuation;
            continuation.kind = GridEntryKind::WidgetContinuation;
            ordered.push_back(std::move(continuation));
            continue;
        }
        const std::uint64_t tid = slots[static_cast<std::size_t>(index)];
        if (hiddenWidgetIds.count(tid))
            continue;
        if (tid == 0) {
            PendingApp empty;
            empty.kind = GridEntryKind::Empty;
            ordered.push_back(std::move(empty));
            continue;
        }
        auto it = byId.find(tid);
        if (it != byId.end()) {
            ordered.push_back(std::move(it->second));
        } else {
            PendingApp empty;
            empty.kind = GridEntryKind::Empty;
            ordered.push_back(std::move(empty));
        }
    }

    if (slots != m_layoutSlots) {
        m_layoutSlots = std::move(slots);
        m_layoutDirty = true;
    }

    apps = std::move(ordered);
}

GridModel WiiUMenuApp::buildRootFolderModel() {
    normalizeWidgetPlacements();
    GridModel model;
    std::unordered_map<std::uint64_t, AppEntry> entries;
    for (const auto& app : m_allApps) {
        if (m_folderStore.folderForTitle(app.titleId) == 0) {
            AppEntry effective = app;
            const auto size = gameGridSize(app.titleId, m_appLayoutMode);
            effective.widgetColumns = size.columns;
            effective.widgetRows = size.rows;
            entries.emplace(effective.titleId, std::move(effective));
        }
    }
    for (const auto& folder : m_folderStore.all()) {
        AppEntry entry;
        entry.id = "folder:" + std::to_string(folder.id);
        entry.title = folder.name;
        entry.titleId = folderTitleId(folder.id);
        entry.kind = GridEntryKind::Folder;
        entry.folderId = folder.id;
        entry.folderPreviewCount = static_cast<int>(folder.titleCount());
        entry.folderColorIndex = folder.colorIndex;
        entries.emplace(entry.titleId, std::move(entry));
    }
    for (const auto& widget : m_widgetStore.all()) {
        if (switchu::widgets::supportedSizes(widget.type, m_appLayoutMode).empty())
            continue;
        AppEntry entry;
        entry.id = "widget:" + std::to_string(widget.id);
        entry.title = widgetTypeLabel(widget.type);
        entry.titleId = switchu::widgets::widgetTitleId(widget.id);
        entry.kind = GridEntryKind::Widget;
        entry.widgetId = widget.id;
        entry.widgetType = widget.type;
        const auto size = switchu::widgets::validatedSize(
            widget.type, widget.size, m_appLayoutMode);
        entry.widgetColumns = size.columns;
        entry.widgetRows = size.rows;
        entry.widgetAssetRef = widget.assetRef;
        entries.emplace(entry.titleId, std::move(entry));
    }

    const int perPage = std::max(1, std::clamp(m_config.gridColumns, 3, 8) *
                                     std::clamp(m_config.gridRows, 2, 5));
    if (m_layoutSlots.empty()) {
        for (const auto& app : m_allApps)
            if (entries.count(app.titleId)) m_layoutSlots.push_back(app.titleId);
        for (const auto& folder : m_folderStore.all())
            m_layoutSlots.push_back(folderTitleId(folder.id));
        for (const auto& widget : m_widgetStore.all())
            m_layoutSlots.push_back(switchu::widgets::widgetTitleId(widget.id));
    }
    for (const auto& pair : entries) {
        if (std::find(m_layoutSlots.begin(), m_layoutSlots.end(), pair.first) == m_layoutSlots.end()) {
            auto empty = std::find(m_layoutSlots.begin(), m_layoutSlots.end(), 0);
            if (empty == m_layoutSlots.end()) m_layoutSlots.push_back(pair.first);
            else *empty = pair.first;
            m_layoutDirty = true;
        }
    }
    const int minimum = perPage * kMinHomePages;
    const int rounded = ((std::max(minimum, static_cast<int>(m_layoutSlots.size())) + perPage - 1) / perPage) * perPage;
    if (static_cast<int>(m_layoutSlots.size()) < rounded) {
        m_layoutSlots.resize(rounded, 0);
        m_layoutDirty = true;
    }

    const int columns = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);
    std::vector<int> coveredBy(m_layoutSlots.size(), -1);
    if (m_appLayoutMode == AppLayoutMode::Grid) {
        for (int index = 0; index < static_cast<int>(m_layoutSlots.size()); ++index) {
            auto found = entries.find(m_layoutSlots[static_cast<std::size_t>(index)]);
            if (found == entries.end() ||
                (!found->second.isWidget() && !found->second.isApplication()))
                continue;
            auto& entry = found->second;
            const int spanColumns = std::max(1, entry.widgetColumns);
            const int spanRows = std::max(1, entry.widgetRows);
            if (spanColumns == 1 && spanRows == 1) continue;
            const int local = index % perPage;
            const int column = local % columns;
            const int row = local / columns;
            bool fits = column + spanColumns <= columns && row + spanRows <= rows;
            for (int dy = 0; fits && dy < spanRows; ++dy) {
                for (int dx = 0; dx < spanColumns; ++dx) {
                    const int cell = index + dy * columns + dx;
                    if (cell >= static_cast<int>(m_layoutSlots.size()) ||
                        (cell != index && m_layoutSlots[static_cast<std::size_t>(cell)] != 0) ||
                        coveredBy[static_cast<std::size_t>(cell)] >= 0) {
                        fits = false;
                        break;
                    }
                }
            }
            if (!fits) {
                entry.widgetColumns = 1;
                entry.widgetRows = 1;
                coveredBy[static_cast<std::size_t>(index)] = index;
                continue;
            }
            for (int dy = 0; dy < spanRows; ++dy)
                for (int dx = 0; dx < spanColumns; ++dx)
                    coveredBy[static_cast<std::size_t>(index + dy * columns + dx)] = index;
        }
    }

    for (int index = 0; index < static_cast<int>(m_layoutSlots.size()); ++index) {
        const auto storedTitleId = m_layoutSlots[static_cast<std::size_t>(index)];
        if (switchu::widgets::isWidgetTitleId(storedTitleId)) {
            const auto* widget = m_widgetStore.find(
                switchu::widgets::widgetIdFromTitleId(storedTitleId));
            if (widget && switchu::widgets::supportedSizes(
                    widget->type, m_appLayoutMode).empty())
                continue;
        }
        if (coveredBy[static_cast<std::size_t>(index)] >= 0 &&
            coveredBy[static_cast<std::size_t>(index)] != index) {
            AppEntry continuation;
            continuation.kind = GridEntryKind::WidgetContinuation;
            model.addEntry(std::move(continuation));
            continue;
        }
        const auto titleId = m_layoutSlots[static_cast<std::size_t>(index)];
        auto found = entries.find(titleId);
        if (found != entries.end()) {
            model.addEntry(found->second);
        } else {
            model.addEntry({});
        }
    }
    if (m_appLayoutMode == AppLayoutMode::DynamicLine)
        return compactDynamicLineEntries(model);
    return model;
}

GridModel WiiUMenuApp::buildOpenFolderModel(std::uint32_t folderId) const {
    GridModel model;
    const auto* folder = m_folderStore.find(folderId);
    if (!folder)
        return model;
    const auto [folderCols, folderRows] = folderGridDimensions(folderId);
    const int perPage = std::max(1, folderCols * folderRows);
    const int occupied = (static_cast<int>(folder->titleIds.size()) + perPage - 1) /
                         perPage;
    const int pages = std::clamp(std::max(folder->pageCount, occupied),
                                 1, switchu::folders::kMaxFolderPages);
    std::vector<std::uint64_t> slots = folder->titleIds;
    slots.resize(static_cast<std::size_t>(pages * perPage), 0);
    std::vector<int> coveredBy(slots.size(), -1);
    std::unordered_map<int, AppEntry> anchors;

    for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
        const std::uint64_t titleId = slots[static_cast<std::size_t>(index)];
        if (titleId == 0) continue;
        auto found = std::find_if(m_allApps.begin(), m_allApps.end(),
            [titleId](const AppEntry& app) { return app.titleId == titleId; });
        if (found == m_allApps.end()) {
            DebugLog::log("[folders] missing title ignored folder=%u tid=%016lX",
                          folderId, static_cast<unsigned long>(titleId));
            continue;
        }

        AppEntry entry = *found;
        const auto size = gameGridSize(titleId, m_appLayoutMode);
        entry.widgetColumns = std::max(1, size.columns);
        entry.widgetRows = std::max(1, size.rows);
        const int local = index % perPage;
        const int column = local % folderCols;
        const int row = local / folderCols;
        bool fits = column + entry.widgetColumns <= folderCols &&
                    row + entry.widgetRows <= folderRows;
        for (int dy = 0; fits && dy < entry.widgetRows; ++dy) {
            for (int dx = 0; dx < entry.widgetColumns; ++dx) {
                const int cell = index + dy * folderCols + dx;
                if (cell >= static_cast<int>(slots.size()) ||
                    (cell != index && slots[static_cast<std::size_t>(cell)] != 0) ||
                    coveredBy[static_cast<std::size_t>(cell)] >= 0) {
                    fits = false;
                    break;
                }
            }
        }
        if (!fits) {
            entry.widgetColumns = 1;
            entry.widgetRows = 1;
        }
        anchors.emplace(index, entry);
        for (int dy = 0; dy < entry.widgetRows; ++dy)
            for (int dx = 0; dx < entry.widgetColumns; ++dx)
                coveredBy[static_cast<std::size_t>(index + dy * folderCols + dx)] = index;
    }

    for (int index = 0; index < static_cast<int>(slots.size()); ++index) {
        if (coveredBy[static_cast<std::size_t>(index)] >= 0 &&
            coveredBy[static_cast<std::size_t>(index)] != index) {
            AppEntry continuation;
            continuation.kind = GridEntryKind::WidgetContinuation;
            model.addEntry(std::move(continuation));
            continue;
        }
        const auto anchor = anchors.find(index);
        if (anchor != anchors.end())
            model.addEntry(anchor->second);
        else
            model.addEntry({});
    }
    if (m_appLayoutMode == AppLayoutMode::DynamicLine)
        return compactDynamicLineEntries(model);
    return model;
}

void WiiUMenuApp::applyDisplayModel(GridModel model, std::uint64_t focusId, bool animate) {
    if (!m_grid)
        return;
    const auto isImageAssetWidget = [](switchu::widgets::WidgetType type) {
        return type == switchu::widgets::WidgetType::ImagePin ||
               type == switchu::widgets::WidgetType::RandomScreenshot;
    };
    // Image widgets own GPU textures, sometimes several animation frames.
    // Preserve their icon object across a reflow/move: destroying it directly
    // after the previous frame was submitted can release image memory still in
    // use by Deko3D and crash the menu.
    const auto previousIcons = m_grid->allIcons();
    for (int i = 0; i < m_model.count() && i < static_cast<int>(previousIcons.size()); ++i) {
        const auto& entry = m_model.at(i);
        if (entry.isWidget() && isImageAssetWidget(entry.widgetType) &&
            entry.titleId != 0 && previousIcons[static_cast<std::size_t>(i)])
            m_retainedImagePins[entry.titleId] = {
                entry.widgetAssetRef,
                previousIcons[static_cast<std::size_t>(i)]->widgetImageAssetPath(),
                previousIcons[static_cast<std::size_t>(i)]};
    }
    m_model = std::move(model);
    // Retention only bridges a reflow/move of the current model. Keeping pins
    // from a closed folder or another page tree would keep every GIF frame
    // alive indefinitely.
    for (auto it = m_retainedImagePins.begin(); it != m_retainedImagePins.end();) {
        bool stillPresent = false;
        for (int i = 0; i < m_model.count(); ++i) {
            const auto& entry = m_model.at(i);
            if (entry.titleId == it->first && entry.isWidget() &&
                isImageAssetWidget(entry.widgetType)) {
                stillPresent = true;
                break;
            }
        }
        if (!stillPresent)
            it = m_retainedImagePins.erase(it);
        else
            ++it;
    }
    std::vector<std::shared_ptr<GlossyIcon>> icons;
    std::vector<std::uint64_t> titleIds;
    icons.reserve(m_model.count());
    titleIds.reserve(m_model.count());
    for (int i = 0; i < m_model.count(); ++i) {
        const auto& entry = m_model.at(i);
        std::shared_ptr<GlossyIcon> icon;
        if (entry.isWidget() && isImageAssetWidget(entry.widgetType)) {
            const auto found = m_retainedImagePins.find(entry.titleId);
            const std::string resolvedPath =
                entry.widgetType == switchu::widgets::WidgetType::RandomScreenshot
                    ? randomScreenshotPath(entry.widgetId)
                    : resolveWidgetAssetRef(entry.widgetAssetRef);
            if (found != m_retainedImagePins.end() &&
                found->second.assetRef == entry.widgetAssetRef &&
                found->second.assetPath == resolvedPath) {
                icon = found->second.icon;
                if (icon)
                    icon->setGridSpan(entry.widgetColumns, entry.widgetRows);
            }
        }
        if (!icon)
            icon = makeIcon(entry);
        icon->setBaseColor(m_theme.iconDefault);
        icon->setBorderColor(m_theme.panelBorder);
        icon->setHighlightColor(m_theme.panelHighlight);
        icon->setCornerRadius(m_theme.iconCornerRadius);
        icon->setLoadingColor(m_theme.cursorNormal);
        icons.push_back(std::move(icon));
        titleIds.push_back(entry.isApplication() ? entry.titleId : 0);
    }
    m_iconStreamer.reconcileTitleIds(titleIds);
    int columns = std::clamp(m_config.gridColumns, 3, 8);
    int rows = std::clamp(m_config.gridRows, 2, 5);
    if (m_openFolderId != 0)
        std::tie(columns, rows) = folderGridDimensions(m_openFolderId);
    auto metrics = computeGridLayoutMetrics(columns, rows);
    if (m_openFolderId != 0) {
        // Reserve a real title band above and a title-pill band below. Scaling
        // the cells, rather than merely shrinking the grid rect, prevents the
        // centered first/last rows from escaping those bands.
        metrics.cellW *= 0.92f;
        metrics.cellH *= 0.92f;
        metrics.padX *= 0.92f;
        metrics.padY *= 0.92f;
    }
    // Inside a folder there is no sidebar to the left or right of the grid, so
    // the edge columns are free to flip the page instead.
    const bool inFolder = (m_openFolderId != 0);
    m_grid->setEdgePaging(inFolder);
    m_grid->setSlideTransition(inFolder);
    m_grid->setLayoutMode(m_appLayoutMode);
    m_grid->setup(std::move(icons), columns, rows, metrics.cellW, metrics.cellH,
                  metrics.padX, metrics.padY);
    wireFocusCallback();
    m_grid->onEdgePage([this](int dir) { flipPageFromEdge(dir); });
    m_grid->onPageSwitched([this]() {
        if (m_editMode && m_editTargetIndex >= 0) {
            const int perPage = std::max(1, m_grid->iconsPerPage());
            const int local = m_editTargetIndex % perPage;
            m_editTargetIndex = m_grid->currentPage() * perPage + local;
            if (m_editTargetIndex >= m_model.count())
                m_editTargetIndex = std::max(0, m_model.count() - 1);
            if (m_editGhostIcon)
                m_editGhostTargetRect = m_grid->gridSpanRect(
                    m_editTargetIndex,
                    m_editGhostIcon->gridSpanColumns(),
                    m_editGhostIcon->gridSpanRows());
        }
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(), m_grid->allIcons());
        if (auto* target = m_grid->focusManager().current())
            focusManager().setFocus(target);
        updateCursor();
    });
    if (!focusTitle(focusId)) {
        if (auto* first = m_grid->focusManager().current())
            focusManager().setFocus(first);
    }
    m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(), m_grid->allIcons());
    m_widgetAssetPage = -1;
    if (animate) m_grid->startAppearAnimation();
    else for (auto& icon : m_grid->allIcons()) icon->forceVisible();
    // Safety net for any rebuild while moving: never leave an out-of-range
    // placement index (that pins the ghost at the origin).
    if (m_editMode && (m_editTargetIndex < 0 || m_editTargetIndex >= m_model.count()))
        syncEditPlacementAfterModelChange(m_openFolderId != 0);
    updateCursor();
}

std::string WiiUMenuApp::promptFolderName(const std::string& initial,
                                          const std::string& guide) {
#ifdef SWITCHU_MENU
    SwkbdConfig keyboard{};
    char text[97]{};
    Result rc = swkbdCreate(&keyboard, 0);
    if (R_FAILED(rc)) {
        DebugLog::log("[folders] keyboard create failed rc=0x%X", rc);
        auto& i18n = nxui::I18n::instance();
        m_dialog->show(i18n.tr("folder.error_title", "Folder error"),
                       i18n.tr("folder.keyboard_error", "The keyboard could not be opened."),
                       {{i18n.tr("button.ok", "OK"), {}, true}});
        focusManager().setFocus(m_dialog.get());
        return {};
    }
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetGuideText(&keyboard, guide.c_str());
    swkbdConfigSetStringLenMax(&keyboard, 48);
    swkbdConfigSetInitialText(&keyboard, initial.c_str());
    rc = swkbdShow(&keyboard, text, sizeof(text));
    swkbdClose(&keyboard);
    if (R_FAILED(rc)) {
        DebugLog::log("[folders] keyboard cancelled/failed rc=0x%X", rc);
        return {};
    }
    return text;
#else
    (void)guide;
    return initial.empty() ? "Folder" : initial;
#endif
}

bool WiiUMenuApp::saveFoldersOrReport(const char* operation) {
    if (m_folderStore.save())
        return true;
    DebugLog::log("[folders] operation failed op=%s", operation ? operation : "unknown");
    m_folderStore.load();
    auto& i18n = nxui::I18n::instance();
    m_dialog->show(i18n.tr("folder.error_title", "Folder error"),
                   i18n.tr("folder.save_error", "The folder change could not be saved."),
                   {{i18n.tr("button.ok", "OK"), {}, true}});
    focusManager().setFocus(m_dialog.get());
    return false;
}

void WiiUMenuApp::createFolder(int targetSlot) {
    auto& i18n = nxui::I18n::instance();
    const std::string name = promptFolderName(
        "", i18n.tr("folder.name_guide", "Enter a folder name"));
    if (name.empty())
        return;
    DebugLog::log("[folders] create requested slot=%d name=%s", targetSlot, name.c_str());
    const std::uint32_t id = m_folderStore.create(name);
    if (id == 0 || !saveFoldersOrReport("create")) return;
    if (targetSlot >= 0 && targetSlot < static_cast<int>(m_layoutSlots.size()) &&
        m_layoutSlots[static_cast<std::size_t>(targetSlot)] == 0) {
        m_layoutSlots[static_cast<std::size_t>(targetSlot)] = folderTitleId(id);
        m_layoutDirty = true;
        saveMenuLayout();
    }
    m_audio.playSfx(Sfx::ConfirmPositive);
    applyDisplayModel(buildRootFolderModel(), folderTitleId(id), true);
}

std::string WiiUMenuApp::widgetTypeLabel(switchu::widgets::WidgetType type) const {
    auto& i18n = nxui::I18n::instance();
    switch (type) {
        case switchu::widgets::WidgetType::Clock:
            return i18n.tr("widget.clock", "Clock");
        case switchu::widgets::WidgetType::RecentlyPlayed:
            return i18n.tr("widget.recently_played", "Recently played");
        case switchu::widgets::WidgetType::RecentPlaytime:
            return i18n.tr("widget.recent_playtime", "Recent playtime");
        case switchu::widgets::WidgetType::RandomScreenshot:
            return i18n.tr("widget.random_screenshot", "Random screenshot");
        case switchu::widgets::WidgetType::ImagePin:
            return i18n.tr("widget.image_pin", "Image pin");
        case switchu::widgets::WidgetType::Batteries:
            return i18n.tr("widget.batteries", "Batteries");
    }
    return i18n.tr("widget.title", "Widget");
}

std::string WiiUMenuApp::widgetDurationLabel(std::uint64_t seconds) const {
    auto& i18n = nxui::I18n::instance();
    if (seconds == 0)
        return i18n.tr("widget.no_playtime", "No recent playtime");
    const std::uint64_t hours = seconds / 3600;
    const std::uint64_t minutes = (seconds % 3600) / 60;
    if (hours > 0)
        return std::to_string(hours) + " h " + std::to_string(minutes) + " min";
    return std::to_string(std::max<std::uint64_t>(1, minutes)) + " min";
}

void WiiUMenuApp::refreshRecentActivityDuration() {
    m_widgetStore.updateRecentDuration(
        static_cast<std::int64_t>(std::time(nullptr)));
#ifdef SWITCHU_MENU
    if (const auto total = queryApplicationPlaytimeSeconds(
            m_widgetStore.recentActivity().titleId))
        m_widgetStore.setTotalSeconds(*total);
#endif
}

void WiiUMenuApp::ensureRecentWidgetAssets(std::uint64_t titleId) {
    if (titleId == 0 || m_recentWidgetAssetTitleId == titleId) return;
    if (m_recentWidgetAssetDecode)
        m_recentWidgetAssetDecode->cancelled.store(true);
    m_recentWidgetAssetTitleId = titleId;
    const std::string heroPath = SteamGridDbManager::heroPath(titleId);
    const std::string logoPath = SteamGridDbManager::logoPath(titleId);
    m_recentWidgetAssetReady.reset();
    m_recentWidgetAssetUploadStage = 0;

    auto state = std::make_shared<RecentWidgetAssetDecodeState>();
    state->titleId = titleId;
    m_recentWidgetAssetDecode = state;
    m_recentWidgetAssetFuture = m_threadPool.submit(
        [state, heroPath, logoPath, titleId]() {
            const auto started = std::chrono::steady_clock::now();
            std::error_code error;
            if (std::filesystem::is_regular_file(heroPath, error) &&
                !state->cancelled.load()) {
                state->hero = steamgriddb::artwork::decode(heroPath, 640, 360, true);
            }
            error.clear();
            if (std::filesystem::is_regular_file(logoPath, error) &&
                !state->cancelled.load()) {
                state->logo = steamgriddb::artwork::decode(logoPath, 384, 192, false);
            }
            if (!state->cancelled.load()) {
                const auto iconData = AppListLoader::loadIconData(titleId);
                if (!state->cancelled.load())
                    state->icon = IconStreamer::decodeIconData(iconData);
            }
            state->elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        });
    DebugLog::log("[widget-recent-assets] queued title=0x%016lX",
                  static_cast<unsigned long>(titleId));
}

void WiiUMenuApp::syncRecentWidgetTextures() {
    if (!m_grid) return;
    const bool matches = m_recentWidgetLoadedTitleId != 0 &&
                         m_recentWidgetLoadedTitleId == m_recentWidgetAssetTitleId;
    for (const auto& icon : m_grid->allIcons()) {
        if (!icon || icon->entryKind() != GridEntryKind::Widget)
            continue;
        if (icon->widgetType() != switchu::widgets::WidgetType::RecentlyPlayed &&
            icon->widgetType() != switchu::widgets::WidgetType::RecentPlaytime)
            continue;
        if (icon->widgetGameTitleId() != m_recentWidgetAssetTitleId)
            continue;
        icon->setWidgetGameTextures(
            m_recentWidgetAssetTitleId,
            matches ? m_recentWidgetHero.get() : nullptr,
            matches ? m_recentWidgetLogo.get() : nullptr,
            matches ? m_recentWidgetIcon.get() : nullptr);
    }
}

void WiiUMenuApp::pollRecentWidgetAssets() {
    if (!m_recentWidgetAssetReady && m_recentWidgetAssetFuture.valid() &&
        m_recentWidgetAssetFuture.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        try {
            m_recentWidgetAssetFuture.get();
            auto decoded = std::move(m_recentWidgetAssetDecode);
            if (decoded && !decoded->cancelled.load() &&
                decoded->titleId == m_recentWidgetAssetTitleId) {
                DebugLog::log(
                    "[widget-recent-assets] decoded title=0x%016lX hero=%zu logo=%zu icon=%zu in %ldms",
                    static_cast<unsigned long>(decoded->titleId),
                    decoded->hero.rgba.size(), decoded->logo.rgba.size(),
                    decoded->icon.rgba.size(), static_cast<long>(decoded->elapsedMs));
                m_recentWidgetAssetReady = std::move(decoded);
                m_recentWidgetAssetUploadStage = 0;
            }
        } catch (const std::exception& ex) {
            DebugLog::log("[widget-recent-assets] decode failed: %s", ex.what());
            m_recentWidgetAssetDecode.reset();
        } catch (...) {
            DebugLog::log("[widget-recent-assets] decode failed: unknown exception");
            m_recentWidgetAssetDecode.reset();
        }
    }

    if (!m_recentWidgetAssetReady)
        return;

    auto& decoded = *m_recentWidgetAssetReady;
    if (m_recentWidgetAssetUploadStage == 0) {
        auto texture = std::make_unique<nxui::Texture>();
        if (!decoded.hero.rgba.empty() && texture->loadFromPixels(
                app().gpu(), app().renderer(), decoded.hero.rgba.data(),
                decoded.hero.width, decoded.hero.height))
            m_recentWidgetHero = std::move(texture);
        else
            m_recentWidgetHero.reset();
        decoded.hero.rgba.clear();
    } else if (m_recentWidgetAssetUploadStage == 1) {
        auto texture = std::make_unique<nxui::Texture>();
        if (!decoded.logo.rgba.empty() && texture->loadFromPixels(
                app().gpu(), app().renderer(), decoded.logo.rgba.data(),
                decoded.logo.width, decoded.logo.height))
            m_recentWidgetLogo = std::move(texture);
        else
            m_recentWidgetLogo.reset();
        decoded.logo.rgba.clear();
    } else if (m_recentWidgetAssetUploadStage == 2) {
        auto texture = std::make_unique<nxui::Texture>();
        if (!decoded.icon.rgba.empty() && texture->loadFromPixels(
                app().gpu(), app().renderer(), decoded.icon.rgba.data(),
                decoded.icon.w, decoded.icon.h))
            m_recentWidgetIcon = std::move(texture);
        else
            m_recentWidgetIcon.reset();
        decoded.icon.rgba.clear();
    }

    m_recentWidgetLoadedTitleId = decoded.titleId;
    ++m_recentWidgetAssetUploadStage;
    syncRecentWidgetTextures();
    if (m_recentWidgetAssetUploadStage >= 3) {
        DebugLog::log("[widget-recent-assets] upload complete title=0x%016lX",
                      static_cast<unsigned long>(decoded.titleId));
        m_recentWidgetAssetReady.reset();
        m_recentWidgetAssetUploadStage = 0;
    }
}

void WiiUMenuApp::ensureGameArtwork(std::uint64_t titleId) {
    if (titleId == 0 || m_gameArtwork.count(titleId)) return;
    if ((m_gameArtworkDecode && m_gameArtworkDecode->titleId == titleId) ||
        (m_gameArtworkReady && m_gameArtworkReady->titleId == titleId) ||
        std::find(m_gameArtworkDecodeQueue.begin(), m_gameArtworkDecodeQueue.end(),
                  titleId) != m_gameArtworkDecodeQueue.end())
        return;
    m_gameArtworkDecodeQueue.push_back(titleId);
    startNextGameArtworkDecode();
}

void WiiUMenuApp::startNextGameArtworkDecode() {
    if (m_gameArtworkFuture.valid() || m_gameArtworkDecode || m_gameArtworkReady ||
        m_gameArtworkDecodeQueue.empty())
        return;
    const std::uint64_t titleId = m_gameArtworkDecodeQueue.front();
    m_gameArtworkDecodeQueue.erase(m_gameArtworkDecodeQueue.begin());
    auto state = std::make_shared<GameArtworkDecodeState>();
    state->titleId = titleId;
    m_gameArtworkDecode = state;
    const std::string heroPath = SteamGridDbManager::heroPath(titleId);
    const std::string logoPath = SteamGridDbManager::logoPath(titleId);
    m_gameArtworkFuture = m_threadPool.submit([state, heroPath, logoPath]() {
        const auto started = std::chrono::steady_clock::now();
        std::error_code error;
        if (std::filesystem::is_regular_file(heroPath, error) &&
            !state->cancelled.load()) {
            state->hero = steamgriddb::artwork::decode(heroPath, 640, 360, true);
        }
        error.clear();
        if (std::filesystem::is_regular_file(logoPath, error) &&
            !state->cancelled.load()) {
            state->logo = steamgriddb::artwork::decode(logoPath, 384, 192, false);
        }
        state->elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    });
    DebugLog::log("[game-artwork] queued title=0x%016lX remaining=%zu",
                  static_cast<unsigned long>(titleId),
                  m_gameArtworkDecodeQueue.size());
}

void WiiUMenuApp::syncGameArtworkTextures(std::uint64_t titleId) {
    if (!m_grid) return;
    const auto artwork = m_gameArtwork.find(titleId);
    if (artwork == m_gameArtwork.end()) return;
    for (const auto& icon : m_grid->allIcons()) {
        if (icon && icon->entryKind() == GridEntryKind::Application &&
            icon->titleId() == titleId && icon->gridSpanColumns() > 1 &&
            icon->gridSpanRows() == 1) {
            icon->setWideGameTextures(artwork->second.hero.get(),
                                      artwork->second.logo.get());
        }
    }
}

void WiiUMenuApp::pollGameArtworkAssets() {
    if (!m_gameArtworkReady && m_gameArtworkFuture.valid() &&
        m_gameArtworkFuture.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        try {
            m_gameArtworkFuture.get();
            auto decoded = std::move(m_gameArtworkDecode);
            if (decoded && !decoded->cancelled.load()) {
                DebugLog::log(
                    "[game-artwork] decoded title=0x%016lX hero=%zu logo=%zu in %ldms",
                    static_cast<unsigned long>(decoded->titleId),
                    decoded->hero.rgba.size(), decoded->logo.rgba.size(),
                    static_cast<long>(decoded->elapsedMs));
                m_gameArtworkReady = std::move(decoded);
                m_gameArtworkUploadTextures = {};
                m_gameArtworkUploadStage = 0;
            }
        } catch (const std::exception& ex) {
            DebugLog::log("[game-artwork] decode failed: %s", ex.what());
            m_gameArtworkDecode.reset();
        } catch (...) {
            DebugLog::log("[game-artwork] decode failed: unknown exception");
            m_gameArtworkDecode.reset();
        }
    }

    // Let recent-widget artwork finish first. This keeps the combined handoff
    // capped to one large texture upload on these frames.
    if (!m_gameArtworkReady) {
        startNextGameArtworkDecode();
        return;
    }
    if (m_recentWidgetAssetReady)
        return;

    auto& decoded = *m_gameArtworkReady;
    constexpr std::uint64_t kTextImageReserve = 4u * 1024u * 1024u;
    if (m_gameArtworkUploadStage == 0) {
        auto texture = std::make_unique<nxui::Texture>();
        if (!decoded.hero.rgba.empty() &&
            app().gpu().imageMemoryAvailable() >
                kTextImageReserve + decoded.hero.rgba.size() &&
            texture->loadFromPixels(
                app().gpu(), app().renderer(), decoded.hero.rgba.data(),
                decoded.hero.width, decoded.hero.height))
            m_gameArtworkUploadTextures.hero = std::move(texture);
        decoded.hero.rgba.clear();
    } else {
        auto texture = std::make_unique<nxui::Texture>();
        if (!decoded.logo.rgba.empty() &&
            app().gpu().imageMemoryAvailable() >
                kTextImageReserve + decoded.logo.rgba.size() &&
            texture->loadFromPixels(
                app().gpu(), app().renderer(), decoded.logo.rgba.data(),
                decoded.logo.width, decoded.logo.height))
            m_gameArtworkUploadTextures.logo = std::move(texture);
        decoded.logo.rgba.clear();
    }
    ++m_gameArtworkUploadStage;
    if (m_gameArtworkUploadStage >= 2) {
        const std::uint64_t titleId = decoded.titleId;
        m_gameArtwork[titleId] = std::move(m_gameArtworkUploadTextures);
        m_gameArtworkReady.reset();
        m_gameArtworkUploadStage = 0;
        syncGameArtworkTextures(titleId);
        DebugLog::log("[game-artwork] upload complete title=0x%016lX",
                      static_cast<unsigned long>(titleId));
        startNextGameArtworkDecode();
    }
}

bool WiiUMenuApp::saveWidgetsOrReport(const char* operation) {
    if (m_widgetStore.save()) return true;
    DebugLog::log("[widgets] operation failed op=%s",
                  operation ? operation : "unknown");
    m_widgetStore.load();
    auto& i18n = nxui::I18n::instance();
    m_dialogReturnFocus = m_contextMenuReturnFocus;
    m_dialog->show(i18n.tr("widget.error_title", "Widget error"),
                   i18n.tr("widget.save_error", "The widget change could not be saved."),
                   {{i18n.tr("button.ok", "OK"), {}, true}});
    focusManager().setFocus(m_dialog.get());
    return false;
}

bool WiiUMenuApp::canPlaceWidget(int targetSlot,
                                 switchu::widgets::WidgetSize size,
                                 std::uint32_t ignoringWidgetId) const {
    return canPlaceGridItem(targetSlot, size,
        ignoringWidgetId == 0 ? 0 :
            switchu::widgets::widgetTitleId(ignoringWidgetId));
}

bool WiiUMenuApp::canPlaceGridItem(int targetSlot,
                                   switchu::widgets::WidgetSize size,
                                   std::uint64_t ignoringTitleId,
                                   std::uint64_t alsoIgnoringTitleId) const {
    if (targetSlot < 0 || targetSlot >= static_cast<int>(m_layoutSlots.size()))
        return false;
    size = m_appLayoutMode == AppLayoutMode::DynamicLine
        ? switchu::widgets::WidgetSize{1, 1} : size;
    const int columns = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);
    const int perPage = columns * rows;
    const int local = targetSlot % perPage;
    const int targetColumn = local % columns;
    const int targetRow = local / columns;
    if (targetColumn + size.columns > columns || targetRow + size.rows > rows)
        return false;

    std::vector<bool> occupied(m_layoutSlots.size(), false);
    for (int index = 0; index < static_cast<int>(m_layoutSlots.size()); ++index) {
        const std::uint64_t value = m_layoutSlots[static_cast<std::size_t>(index)];
        if (value == 0) continue;
        if (value == ignoringTitleId || value == alsoIgnoringTitleId) continue;
        const std::uint32_t widgetId = switchu::widgets::widgetIdFromTitleId(value);
        const auto* widget = widgetId != 0 ? m_widgetStore.find(widgetId) : nullptr;
        if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
            occupied[static_cast<std::size_t>(index)] = true;
            continue;
        }
        const auto widgetSize = widget
            ? switchu::widgets::validatedSize(
                widget->type, widget->size, AppLayoutMode::Grid)
            : gameGridSize(value, AppLayoutMode::Grid);
        const int widgetLocal = index % perPage;
        const int widgetColumn = widgetLocal % columns;
        const int widgetRow = widgetLocal / columns;
        if (widgetColumn + widgetSize.columns > columns ||
            widgetRow + widgetSize.rows > rows) {
            occupied[static_cast<std::size_t>(index)] = true;
            continue;
        }
        for (int dy = 0; dy < widgetSize.rows; ++dy) {
            for (int dx = 0; dx < widgetSize.columns; ++dx) {
                const int cell = index + dy * columns + dx;
                if (cell < static_cast<int>(occupied.size()))
                    occupied[static_cast<std::size_t>(cell)] = true;
            }
        }
    }

    for (int dy = 0; dy < size.rows; ++dy) {
        for (int dx = 0; dx < size.columns; ++dx) {
            const int cell = targetSlot + dy * columns + dx;
            if (cell >= static_cast<int>(occupied.size()) ||
                occupied[static_cast<std::size_t>(cell)])
                return false;
        }
    }
    return true;
}

void WiiUMenuApp::normalizeWidgetPlacements() {
    if (m_layoutSlots.empty()) return;
    const int columns = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);
    const int perPage = std::max(1, columns * rows);

    struct Placement {
        std::uint64_t titleId = 0;
        int anchor = -1;
        switchu::widgets::WidgetSize size;
    };
    std::vector<Placement> placements;
    for (int index = 0; index < static_cast<int>(m_layoutSlots.size()); ++index) {
        const std::uint64_t titleId = m_layoutSlots[static_cast<std::size_t>(index)];
        const auto* widget = m_widgetStore.find(
            switchu::widgets::widgetIdFromTitleId(titleId));
        switchu::widgets::WidgetSize size{1, 1};
        if (widget) {
            const auto sizes = switchu::widgets::supportedSizes(
                widget->type, AppLayoutMode::Grid);
            if (sizes.empty()) continue;
            size = switchu::widgets::validatedSize(
                widget->type, widget->size, AppLayoutMode::Grid);
        } else {
            const bool isGame = std::any_of(m_allApps.begin(), m_allApps.end(),
                [titleId](const AppEntry& app) { return app.titleId == titleId; });
            if (!isGame) continue;
            size = gameGridSize(titleId, AppLayoutMode::Grid);
        }
        if (size.columns > 1 || size.rows > 1)
            placements.push_back({titleId, index, size});
    }
    if (placements.empty()) return;

    std::vector<bool> reserved(m_layoutSlots.size(), false);
    for (int index = 0; index < static_cast<int>(m_layoutSlots.size()); ++index) {
        const std::uint64_t titleId = m_layoutSlots[static_cast<std::size_t>(index)];
        reserved[static_cast<std::size_t>(index)] = titleId != 0;
        if (const auto* widget = m_widgetStore.find(
                switchu::widgets::widgetIdFromTitleId(titleId));
            widget && switchu::widgets::supportedSizes(
                widget->type, AppLayoutMode::Grid).empty())
            reserved[static_cast<std::size_t>(index)] = false;
    }
    std::vector<bool> occupied(m_layoutSlots.size(), false);

    auto fits = [&](int anchor, switchu::widgets::WidgetSize size) {
        if (anchor < 0 || anchor >= static_cast<int>(m_layoutSlots.size()))
            return false;
        const int local = anchor % perPage;
        const int column = local % columns;
        const int row = local / columns;
        if (column + size.columns > columns || row + size.rows > rows)
            return false;
        for (int dy = 0; dy < size.rows; ++dy) {
            for (int dx = 0; dx < size.columns; ++dx) {
                const int cell = anchor + dy * columns + dx;
                if (cell >= static_cast<int>(reserved.size()) ||
                    reserved[static_cast<std::size_t>(cell)] ||
                    occupied[static_cast<std::size_t>(cell)])
                    return false;
            }
        }
        return true;
    };
    auto occupy = [&](int anchor, switchu::widgets::WidgetSize size) {
        for (int dy = 0; dy < size.rows; ++dy)
            for (int dx = 0; dx < size.columns; ++dx)
                occupied[static_cast<std::size_t>(anchor + dy * columns + dx)] = true;
    };

    for (const auto& placement : placements) {
        reserved[static_cast<std::size_t>(placement.anchor)] = false;
        int target = fits(placement.anchor, placement.size) ? placement.anchor : -1;
        if (target < 0) {
            for (int candidate = 0;
                 candidate < static_cast<int>(m_layoutSlots.size()); ++candidate) {
                if (fits(candidate, placement.size)) {
                    target = candidate;
                    break;
                }
            }
        }
        if (target < 0) {
            const std::size_t oldSize = m_layoutSlots.size();
            m_layoutSlots.resize(oldSize + static_cast<std::size_t>(perPage), 0);
            reserved.resize(m_layoutSlots.size(), false);
            occupied.resize(m_layoutSlots.size(), false);
            for (int candidate = static_cast<int>(oldSize);
                 candidate < static_cast<int>(m_layoutSlots.size()); ++candidate) {
                if (fits(candidate, placement.size)) {
                    target = candidate;
                    break;
                }
            }
        }
        if (target < 0) {
            reserved[static_cast<std::size_t>(placement.anchor)] = true;
            continue;
        }
        if (target != placement.anchor) {
            m_layoutSlots[static_cast<std::size_t>(placement.anchor)] = 0;
            m_layoutSlots[static_cast<std::size_t>(target)] = placement.titleId;
            m_layoutDirty = true;
        }
        reserved[static_cast<std::size_t>(target)] = true;
        occupy(target, placement.size);
    }
}

std::string WiiUMenuApp::resolveWidgetAssetRef(const std::string& assetRef) const {
    if (assetRef.empty() || assetRef.find("..") != std::string::npos ||
        assetRef.find('\\') != std::string::npos)
        return {};
    if (assetRef.rfind("widget:", 0) == 0) {
        const std::string relative = assetRef.substr(7);
        return relative.empty() ? std::string()
            : std::string(switchu::widgets::WidgetStore::kAssetRoot) + "/" + relative;
    }
    if (assetRef.rfind("theme:", 0) == 0) {
        const std::string relative = assetRef.substr(6);
        if (relative.empty() || m_effectivePreset.installPath.empty()) return {};
        return m_effectivePreset.installPath + "/" + relative;
    }
    return {};
}

void WiiUMenuApp::syncWidgetPageAssets() {
    if (!m_grid || m_grid->allIcons().empty()) return;

    const auto& icons = m_grid->allIcons();
    m_widgetAssetCurrentScratch.assign(icons.size(), 0);
    m_widgetAssetKeepScratch.assign(icons.size(), 0);
    auto& current = m_widgetAssetCurrentScratch;
    auto& keep = m_widgetAssetKeepScratch;
    const bool dynamicLine = m_appLayoutMode == AppLayoutMode::DynamicLine;
    const int page = dynamicLine
        ? std::max(0, m_grid->focusedGlobalIndex())
        : m_grid->currentPage();
    const bool pageChanged = page != m_widgetAssetPage;
    const bool sliding = m_grid->isTransitioning();
    const bool transitionEnded = m_widgetAssetsWereSliding && !sliding;
    m_widgetAssetPage = page;
    m_widgetAssetsWereSliding = sliding;

    if (dynamicLine) {
        // The carousel renderer keeps roughly four neighbours on each side in
        // view. One extra item avoids a decode exactly as it enters the clip.
        const int begin = std::max(0, page - 5);
        const int end = std::min(static_cast<int>(icons.size()), page + 6);
        for (int i = begin; i < end; ++i) {
            keep[static_cast<std::size_t>(i)] = true;
            current[static_cast<std::size_t>(i)] = true;
        }
    } else {
        const int perPage = std::max(1, m_grid->iconsPerPage());
        const int totalPages = std::max(1, m_grid->totalPages());
        const auto markPage = [&](int wantedPage, bool visibleNow) {
            if (wantedPage < 0 || wantedPage >= totalPages) return;
            const int begin = wantedPage * perPage;
            const int end = std::min(begin + perPage,
                                     static_cast<int>(icons.size()));
            for (int i = begin; i < end; ++i) {
                keep[static_cast<std::size_t>(i)] = true;
                if (visibleNow)
                    current[static_cast<std::size_t>(i)] = true;
            }
        };
        markPage(page, true);
        // Adjacent pages are warmed progressively while idle. Normal page
        // changes therefore never decode a GIF in the middle of the slide.
        markPage(page - 1, false);
        markPage(page + 1, false);
    }

    // Each wide game card owns a 640x360 hero and a 384x192 logo. Loading
    // those textures while constructing the complete grid made their lifetime
    // global and exhausted the 32 MiB Deko image budget. Keep only the page
    // being rendered (plus the outgoing page during a slide) resident.
    std::unordered_set<std::uint64_t> currentGameArtwork;
    std::unordered_set<std::uint64_t> retainedGameArtwork;
    for (std::size_t i = 0; i < icons.size(); ++i) {
        const auto* icon = icons[i].get();
        if (!icon || icon->entryKind() != GridEntryKind::Application ||
            icon->gridSpanColumns() <= 1 || icon->gridSpanRows() != 1)
            continue;
        if (current[i]) {
            currentGameArtwork.insert(icon->titleId());
            retainedGameArtwork.insert(icon->titleId());
        } else if (sliding && keep[i]) {
            retainedGameArtwork.insert(icon->titleId());
        }
        if (icon == m_editSourceIcon)
            retainedGameArtwork.insert(icon->titleId());
    }

    m_gameArtworkDecodeQueue.erase(
        std::remove_if(m_gameArtworkDecodeQueue.begin(), m_gameArtworkDecodeQueue.end(),
            [&currentGameArtwork](std::uint64_t titleId) {
                return !currentGameArtwork.count(titleId);
            }),
        m_gameArtworkDecodeQueue.end());
    if (m_gameArtworkDecode &&
        !currentGameArtwork.count(m_gameArtworkDecode->titleId))
        m_gameArtworkDecode->cancelled.store(true);
    if (m_gameArtworkReady &&
        !currentGameArtwork.count(m_gameArtworkReady->titleId)) {
        m_gameArtworkReady.reset();
        m_gameArtworkUploadTextures = {};
        m_gameArtworkUploadStage = 0;
    }

    std::vector<std::uint64_t> releaseGameArtwork;
    for (const auto& entry : m_gameArtwork) {
        if (!retainedGameArtwork.count(entry.first))
            releaseGameArtwork.push_back(entry.first);
    }
    if (!releaseGameArtwork.empty()) {
        for (const auto& icon : icons) {
            if (icon && std::find(releaseGameArtwork.begin(),
                                  releaseGameArtwork.end(), icon->titleId()) !=
                            releaseGameArtwork.end())
                icon->setWideGameTextures(nullptr, nullptr);
        }
        app().gpu().waitIdle();
        for (const std::uint64_t titleId : releaseGameArtwork)
            m_gameArtwork.erase(titleId);
#ifdef NXUI_BACKEND_DEKO3D
        app().renderer().reclaimReleasedTextureSlotsAfterIdle();
#endif
        DebugLog::log("[game-artwork] released=%zu page=%d gpu=%llu/%llu",
                      releaseGameArtwork.size(), page,
                      static_cast<unsigned long long>(app().gpu().imageMemoryUsed()),
                      static_cast<unsigned long long>(app().gpu().imageMemoryBudget()));
    }
    for (std::size_t i = 0; i < icons.size(); ++i) {
        auto* icon = icons[i].get();
        if (!icon || !current[i] ||
            icon->entryKind() != GridEntryKind::Application ||
            icon->gridSpanColumns() <= 1 || icon->gridSpanRows() != 1)
            continue;
        const auto artwork = m_gameArtwork.find(icon->titleId());
        if (artwork != m_gameArtwork.end())
            icon->setWideGameTextures(artwork->second.hero.get(),
                                      artwork->second.logo.get());
        else
            ensureGameArtwork(icon->titleId());
    }

    if (pageChanged || transitionEnded) {
        for (std::size_t i = 0; i < icons.size(); ++i) {
            if (current[i] && icons[i])
                icons[i]->allowWidgetImageAssetRetry();
        }
    }

    // Decoding is performed by the worker pool. Keep the Deko side deliberately
    // small: at most two finished frames are uploaded per UI frame.
    int animationUploads = 0;
    for (const auto& icon : icons) {
        if (!icon || !icon->isWidgetImageAssetLoading()) continue;
        if (animationUploads >= 2) break;
        if (icon->pollWidgetImageAssetLoad(app().gpu(), app().renderer()))
            ++animationUploads;
    }

    bool currentNeedsMemory = false;
    for (std::size_t i = 0; i < icons.size(); ++i) {
        const auto* icon = icons[i].get();
        if (current[i] && icon && icon->hasWidgetImageAsset() &&
            !icon->isWidgetImageAssetLoaded() &&
            !icon->widgetImageAssetLoadAttempted()) {
            currentNeedsMemory = true;
            break;
        }
    }

    // Do not release the outgoing page until its last transition frame has
    // completed. Destruction of Deko image memory also requires the queue to
    // be idle because the preceding frame may still reference it.
    std::vector<GlossyIcon*> release;
    constexpr std::uint64_t kCurrentPageReserve = 12u * 1024u * 1024u;
    const bool reclaimPrefetchForCurrent = !sliding && currentNeedsMemory &&
        app().gpu().imageMemoryAvailable() < kCurrentPageReserve;
    if (!sliding) {
        for (std::size_t i = 0; i < icons.size(); ++i) {
            auto* icon = icons[i].get();
            const bool retainedForSmoothPaging = keep[i] &&
                !(reclaimPrefetchForCurrent && !current[i]);
            if (!icon || retainedForSmoothPaging || icon == m_editSourceIcon ||
                !icon->hasWidgetImageAsset() ||
                (!icon->isWidgetImageAssetLoaded() &&
                 !icon->isWidgetImageAssetLoading()))
                continue;
            release.push_back(icon);
        }
    }
    if (!release.empty()) {
        const bool releasesGpuMemory = std::any_of(
            release.begin(), release.end(),
            [](const GlossyIcon* icon) {
                return icon && icon->isWidgetImageAssetLoaded();
            });
        if (releasesGpuMemory)
            app().gpu().waitIdle();
        bool released = false;
        for (auto* icon : release)
            released = icon->unloadWidgetImageAsset() || released;
#ifdef NXUI_BACKEND_DEKO3D
        if (released)
            app().renderer().reclaimReleasedTextureSlotsAfterIdle();
#endif
        DebugLog::log("[widget-assets] released=%zu page=%d gpu=%llu/%llu",
                      release.size(), page,
                      static_cast<unsigned long long>(app().gpu().imageMemoryUsed()),
                      static_cast<unsigned long long>(app().gpu().imageMemoryBudget()));
    }

    // Visible assets have priority and are all attempted before rendering.
    // A failure is remembered until the page changes, preventing an expensive
    // GIF decode loop when the fixed GPU budget is genuinely exhausted.
    for (std::size_t i = 0; i < icons.size(); ++i) {
        auto* icon = icons[i].get();
        if (!current[i] || !icon || !icon->hasWidgetImageAsset() ||
            icon->isWidgetImageAssetLoaded() ||
            icon->widgetImageAssetLoadAttempted())
            continue;
        icon->startWidgetImageAssetLoad(
            m_threadPool, app().gpu(), app().renderer());
    }

    if (sliding) return;

    // Decode at most one off-screen asset per frame. Keep enough space for
    // text and for a reasonably-sized current-page animation; prefetching is
    // opportunistic and must never starve the UI itself.
    constexpr std::uint64_t kPrefetchReserve = 8u * 1024u * 1024u;
    if (app().gpu().imageMemoryAvailable() <= kPrefetchReserve) return;
    for (std::size_t i = 0; i < icons.size(); ++i) {
        auto* icon = icons[i].get();
        if (!keep[i] || current[i] || !icon ||
            !icon->hasWidgetImageAsset() || icon->isWidgetImageAssetLoaded() ||
            icon->widgetImageAssetLoadAttempted())
            continue;
        icon->startWidgetImageAssetLoad(
            m_threadPool, app().gpu(), app().renderer());
        break;
    }
}

std::vector<std::pair<std::string, std::string>>
WiiUMenuApp::listWidgetAssets(bool screenshotsOnly) const {
    std::vector<std::pair<std::string, std::string>> result;
    std::unordered_set<std::string> seen;
    auto supported = [](std::string extension) {
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg"
            || extension == ".webp" || extension == ".gif";
    };
    auto scan = [&](const std::string& root, const std::string& referencePrefix,
                    const std::string& relativeBase) {
        std::error_code ec;
        if (root.empty() || !std::filesystem::is_directory(root, ec)) return;
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && iterator != end && result.size() < 64; iterator.increment(ec)) {
            if (!iterator->is_regular_file(ec) || !supported(iterator->path().extension().string()))
                continue;
            std::string relative = iterator->path().string().substr(root.size());
            while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
            if (relative.empty()) continue;
            std::string stored = referencePrefix + relativeBase + relative;
            if (!seen.insert(stored).second) continue;
            result.emplace_back(iterator->path().filename().string(), std::move(stored));
        }
    };

    const std::string widgetRoot = switchu::widgets::WidgetStore::kAssetRoot;
    if (screenshotsOnly) {
        scan(widgetRoot + "/screenshots", "widget:", "screenshots/");
    } else {
        scan(widgetRoot, "widget:", "");
    }
    if (!m_effectivePreset.installPath.empty()) {
        const std::string& themeRoot = m_effectivePreset.installPath;
        if (screenshotsOnly) {
            scan(themeRoot + "/widgets/screenshots", "theme:", "widgets/screenshots/");
            scan(themeRoot + "/screenshots", "theme:", "screenshots/");
        } else {
            scan(themeRoot + "/widgets", "theme:", "widgets/");
            scan(themeRoot + "/assets/widgets", "theme:", "assets/widgets/");
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    return result;
}

std::string WiiUMenuApp::randomScreenshotPath(std::uint32_t widgetId) const {
    const auto assets = listWidgetAssets(true);
    if (assets.empty()) return {};
    const std::uint64_t bucket = static_cast<std::uint64_t>(std::time(nullptr)) / 60u;
    const std::size_t index = static_cast<std::size_t>(
        (bucket * 11400714819323198485ull + widgetId) % assets.size());
    return resolveWidgetAssetRef(assets[index].second);
}

void WiiUMenuApp::showAddContextMenu(int targetSlot, const nxui::Rect& anchor) {
    if (!m_contextMenu || m_openFolderId != 0) return;
    auto& i18n = nxui::I18n::instance();
    if (!m_contextMenu->isActive())
        m_contextMenuReturnFocus = focusManager().current();
    m_contextMenu->show(anchor, i18n.tr("add.title", "Add"), {
        {i18n.tr("folder.create", "Create new folder"),
         [this, targetSlot]() {
             m_contextMenu->hide();
             createFolder(targetSlot);
         }},
        {i18n.tr("widget.create", "Create new widget"),
         [this, targetSlot, anchor]() { showWidgetTypeMenu(targetSlot, anchor); }},
    });
    m_audio.playSfx(Sfx::ModalShow);
    focusManager().setFocus(m_contextMenu.get());
}

void WiiUMenuApp::showWidgetTypeMenu(int targetSlot, const nxui::Rect& anchor) {
    auto& i18n = nxui::I18n::instance();
    std::vector<ContextMenu::Item> items;
    for (auto type : {switchu::widgets::WidgetType::Clock,
                      switchu::widgets::WidgetType::RecentlyPlayed,
                      switchu::widgets::WidgetType::RecentPlaytime,
                      switchu::widgets::WidgetType::ImagePin,
                      switchu::widgets::WidgetType::Batteries}) {
        if (switchu::widgets::supportedSizes(type, m_appLayoutMode).empty())
            continue;
        items.push_back({widgetTypeLabel(type),
            [this, targetSlot, anchor, type]() {
                showWidgetSizeMenu(targetSlot, anchor, type);
            }});
    }
    m_contextMenu->show(anchor, i18n.tr("widget.choose_type", "Choose widget"),
                        std::move(items), 0,
                        [this, targetSlot, anchor]() {
                            showAddContextMenu(targetSlot, anchor);
                        });
    focusManager().setFocus(m_contextMenu.get());
}

void WiiUMenuApp::showWidgetSizeMenu(int targetSlot, const nxui::Rect& anchor,
                                     switchu::widgets::WidgetType type) {
    auto& i18n = nxui::I18n::instance();
    std::vector<ContextMenu::Item> items;
    for (const auto size : switchu::widgets::supportedSizes(type, m_appLayoutMode)) {
        const bool available = canPlaceWidget(targetSlot, size);
        const std::string label = std::to_string(size.columns) + "×"
            + std::to_string(size.rows)
            + (available ? std::string() : " — " + i18n.tr("widget.no_space", "No space"));
        items.push_back({label, [this, targetSlot, anchor, type, size]() {
            if (type == switchu::widgets::WidgetType::ImagePin)
                showWidgetAssetMenu(targetSlot, anchor, type, size);
            else
                createWidget(targetSlot, type, size);
        }, available});
    }
    m_contextMenu->show(anchor, i18n.tr("widget.choose_size", "Choose size"),
                        std::move(items), 0,
                        [this, targetSlot, anchor]() {
                            showWidgetTypeMenu(targetSlot, anchor);
                        });
    focusManager().setFocus(m_contextMenu.get());
}

void WiiUMenuApp::showWidgetAssetMenu(int targetSlot, const nxui::Rect& anchor,
                                      switchu::widgets::WidgetType type,
                                      switchu::widgets::WidgetSize size) {
    auto assets = listWidgetAssets(false);
    auto& i18n = nxui::I18n::instance();
    if (assets.empty()) {
        nxui::Widget* returnFocus = m_contextMenuReturnFocus;
        m_contextMenu->hide();
        m_dialogReturnFocus = returnFocus;
        m_dialog->show(i18n.tr("widget.no_assets_title", "No widget images"),
            i18n.tr("widget.no_assets_desc",
                "Add PNG, JPG, WebP or GIF files to sdmc:/config/SwitchU/widgets/assets/ or to the active theme's widgets folder."),
            {{i18n.tr("button.ok", "OK"), {}, true}});
        focusManager().setFocus(m_dialog.get());
        return;
    }
    std::vector<ContextMenu::Item> items;
    items.reserve(assets.size());
    for (auto& [label, reference] : assets) {
        items.push_back({label, [this, targetSlot, type, size, reference]() {
            createWidget(targetSlot, type, size, reference);
        }});
    }
    m_contextMenu->show(anchor, i18n.tr("widget.choose_image", "Choose image"),
                        std::move(items), 0,
                        [this, targetSlot, anchor, type]() {
                            showWidgetSizeMenu(targetSlot, anchor, type);
                        });
    focusManager().setFocus(m_contextMenu.get());
}

void WiiUMenuApp::createWidget(int targetSlot, switchu::widgets::WidgetType type,
                               switchu::widgets::WidgetSize size,
                               const std::string& assetRef) {
    size = switchu::widgets::validatedSize(type, size, m_appLayoutMode);
    if (size.columns <= 0 || size.rows <= 0) return;
    if (!canPlaceWidget(targetSlot, size)) return;
    const std::uint32_t id = m_widgetStore.create(type, size, assetRef);
    if (id == 0 || !saveWidgetsOrReport("create")) return;
    m_layoutSlots[static_cast<std::size_t>(targetSlot)] =
        switchu::widgets::widgetTitleId(id);
    m_layoutDirty = true;
    saveMenuLayout();
    if (m_contextMenu) m_contextMenu->hide();
    m_audio.playSfx(Sfx::ConfirmPositive);
    applyDisplayModel(buildRootFolderModel(), switchu::widgets::widgetTitleId(id), true);
}

void WiiUMenuApp::showWidgetOptionsMenu(std::uint32_t widgetId, int slot,
                                        const nxui::Rect& anchor) {
    const auto* widget = m_widgetStore.find(widgetId);
    if (!widget || !m_contextMenu) return;
    m_contextMenuReturnFocus = focusManager().current();
    auto& i18n = nxui::I18n::instance();
    std::vector<ContextMenu::Item> items;
    for (const auto size : switchu::widgets::supportedSizes(widget->type, m_appLayoutMode)) {
        const bool available = canPlaceWidget(slot, size, widgetId);
        const std::string label = i18n.tr("widget.resize", "Resize") + " "
            + std::to_string(size.columns) + "×" + std::to_string(size.rows);
        items.push_back({label, [this, widgetId, size]() {
            if (!m_widgetStore.setSize(widgetId, size)) {
                m_contextMenu->hide();
                return;
            }
            if (!saveWidgetsOrReport("resize")) return;
            m_contextMenu->hide();
            applyDisplayModel(buildRootFolderModel(),
                              switchu::widgets::widgetTitleId(widgetId), false);
        }, available});
    }
    items.push_back({i18n.tr("widget.delete", "Delete widget"),
        [this, widgetId]() {
            if (!m_widgetStore.remove(widgetId) || !saveWidgetsOrReport("delete")) return;
            const auto pseudoId = switchu::widgets::widgetTitleId(widgetId);
            m_retainedImagePins.erase(pseudoId);
            std::replace(m_layoutSlots.begin(), m_layoutSlots.end(), pseudoId,
                         std::uint64_t{0});
            m_layoutDirty = true;
            saveMenuLayout();
            m_contextMenu->hide();
            applyDisplayModel(buildRootFolderModel(), 0, false);
            m_audio.playSfx(Sfx::ConfirmPositive);
        }});
    m_contextMenu->show(anchor, widgetTypeLabel(widget->type), std::move(items));
    m_audio.playSfx(Sfx::ModalShow);
    focusManager().setFocus(m_contextMenu.get());
}

void WiiUMenuApp::renameFolder(std::uint32_t folderId) {
    const auto* folder = m_folderStore.find(folderId);
    if (!folder) return;
    const std::string oldName = folder->name;
    const std::string name = promptFolderName(oldName,
        nxui::I18n::instance().tr("folder.rename_guide", "Rename folder"));
    if (name.empty() || name == oldName) return;
    m_folderStore.rename(folderId, name);
    if (!saveFoldersOrReport("rename")) return;
    if (m_openFolderId == folderId && m_folderHeaderLabel)
        m_folderHeaderLabel->setText(name);
    else
        applyDisplayModel(buildRootFolderModel(), folderTitleId(folderId), false);
}

void WiiUMenuApp::requestOpenFolder(std::uint32_t folderId, std::uint64_t focusTitleId) {
    if (!m_folderStore.find(folderId) || m_folderCaptureRequested) return;
    m_requestedFolderId = folderId;
    m_folderOpenFocusTitleId = focusTitleId;
    m_folderCaptureRequested = true;
    m_folderCaptureReady = false;
    if (m_cursor) m_cursor->setVisible(false);
}

void WiiUMenuApp::flipPageFromEdge(int dir) {
    if (!m_grid || m_grid->isTransitioning())
        return;
    const int target = m_grid->currentPage() + dir;
    if (target < 0 || target >= m_grid->totalPages())
        return;

    const int cols = std::max(1, m_grid->columns());
    const int perPage = std::max(1, m_grid->iconsPerPage());
    const int global = m_grid->focusedGlobalIndex();
    const int row = global >= 0 ? (global % perPage) / cols : 0;

    m_grid->startPageTransition(target);
    kickPageArrow(dir);

    // Carry on along the same row, entering from the opposite edge.
    const int col = (dir > 0) ? 0 : cols - 1;
    if (m_grid->focusGlobalIndex(target * perPage + row * cols + col)) {
        if (auto* focused = m_grid->focusManager().current())
            focusManager().setFocus(focused);
    }
    m_audio.playSfx(Sfx::PageChange);
}

void WiiUMenuApp::syncPageIndicator() {
    if (!m_pageIndicator || !m_grid)
        return;
    if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
        m_pageIndicator->setVisible(false);
        return;
    }
    const int total = m_grid->totalPages();
    m_pageIndicator->setVisible(total > 1); // a lone page would draw an empty pill
    m_pageIndicator->setPageCount(total);
    m_pageIndicator->setCurrentPage(m_grid->currentPage());
}

void WiiUMenuApp::toggleAppLayoutMode() {
    setAppLayoutMode(m_appLayoutMode == AppLayoutMode::Grid ? AppLayoutMode::DynamicLine : AppLayoutMode::Grid);
}

void WiiUMenuApp::configureDynamicLineNavigation() {
    const bool dynamicLine = m_appLayoutMode == AppLayoutMode::DynamicLine;
    m_sidebar.setDynamicLineLayout(dynamicLine);

    if (m_grid) {
        std::vector<nxui::Widget*> leftTargets;
        std::vector<nxui::Widget*> rightTargets;
        leftTargets.reserve(m_sidebar.leftButtons().size());
        rightTargets.reserve(m_sidebar.rightButtons().size());
        for (const auto& button : m_sidebar.leftButtons())
            leftTargets.push_back(button.get());
        for (const auto& button : m_sidebar.rightButtons())
            rightTargets.push_back(button.get());
        m_grid->setGridSideTargets(std::move(leftTargets), std::move(rightTargets));
        nxui::Widget* profileTarget = dynamicLine && !m_userAvatarButtons.empty()
            ? m_userAvatarButtons[m_userAvatarButtons.size() / 2].get() : nullptr;
        m_grid->setDynamicLineUpTarget(profileTarget);
        m_grid->setDynamicLineDownTarget(nullptr);
    }

    if (!dynamicLine) {
        m_sidebar.setDynamicLineDownAction({});
        wireUserAvatarNavigation();
        return;
    }

    wireUserAvatarNavigation();

    // Resolve the app when DOWN is pressed. A persistent raw pointer here can
    // outlive icons rebuilt by a move or catalogue refresh.
    m_sidebar.setDynamicLineDownAction([this]() {
        if (!m_grid || m_appLayoutMode != AppLayoutMode::DynamicLine ||
            m_navigator.route() != switchu::navigation::Route::Home)
            return;
        auto* target = m_grid->focusManager().current();
        if (target && isCurrentFocusableWidget(target))
            focusManager().setFocus(target);
    });
}

void WiiUMenuApp::setAppLayoutMode(AppLayoutMode mode) {
    if (m_appLayoutMode == mode && m_grid && m_grid->layoutMode() == mode)
        return;
    m_appLayoutMode = mode;
    m_config.appLayoutMode = mode;
    if (m_configSaveFuture.valid())
        m_configSaveFuture.wait();
    m_configSaveFuture = m_threadPool.submit([config = m_config]() {
        config.save();
    });

    const bool rebuildRoot = m_grid && m_openFolderId == 0;
    if (m_grid && !rebuildRoot) {
        m_grid->setLayoutMode(m_appLayoutMode);
    }
    if (m_steamGridDbBackdrop)
        m_steamGridDbBackdrop->setLayoutMode(m_appLayoutMode);

    if (rebuildRoot) {
        std::uint64_t focused = 0;
        if (auto* current = m_grid->focusManager().current();
            current && current->tag() == "glossy_icon")
            focused = static_cast<GlossyIcon*>(current)->titleId();
        applyDisplayModel(buildRootFolderModel(), focused, false);
    }
    configureDynamicLineNavigation();

    m_audio.playSfx(Sfx::ThemeToggle);

    auto& i18n = nxui::I18n::instance();
    const std::string announcement = (m_appLayoutMode == AppLayoutMode::DynamicLine)
        ? i18n.tr("accessibility.layout.dynamic_line", "Dynamic line mode")
        : i18n.tr("accessibility.layout.grid", "Grid mode");
    m_accessibility.announce(announcement, true, true);

    syncPageIndicator();
    updateCursor();
}

void WiiUMenuApp::openCapturedFolder() {
    const auto* folder = m_folderStore.find(m_requestedFolderId);
    if (!folder) return;
    m_openFolderId = m_requestedFolderId;
    m_requestedFolderId = 0;
    m_folderCaptureReady = false;
    const bool refocus = (m_folderOpenFocusTitleId != 0);
    if (m_folderBackdrop) m_folderBackdrop->show(refocus);
    if (m_folderHeader) m_folderHeader->setVisible(true);
    if (m_topHud) m_topHud->setVisible(false);
    if (m_leftSidebar) m_leftSidebar->setVisible(false);
    if (m_rightSidebar) m_rightSidebar->setVisible(false);
    if (m_pageIndicator)
        m_pageIndicator->setActiveColor(switchu::folders::colorForIndex(folder->colorIndex));
    if (m_folderHeaderLabel) {
        m_folderHeaderLabel->setText(folder->name);
        m_folderHeaderLabel->setTextColor(m_theme.textPrimary);
    }
    m_grid->setRect({kGridRectX, 148.f, kGridRectW, 470.f});
    // Don't inherit the root page number into the folder grid.
    m_grid->setPage(0);
    applyDisplayModel(buildOpenFolderModel(m_openFolderId), m_folderOpenFocusTitleId, false);
    m_folderOpenFocusTitleId = 0;
    syncPageIndicator();
    if (m_editMode) {
        // Always re-anchor: the previous target was a root-grid index.
        syncEditPlacementAfterModelChange(true);
        reattachEditSourceIcon();
    }
    if (!refocus)
        m_audio.playSfx(Sfx::ModalShow);
}

void WiiUMenuApp::closeFolder(bool preserveEditMode) {
    if (m_openFolderId == 0) return;
    const std::uint32_t oldId = m_openFolderId;
    if (preserveEditMode) {
        detachEditSourceIcon();
        unbindEditActions();
    }
    m_openFolderId = 0;
    if (m_folderBackdrop) m_folderBackdrop->hide();
    if (m_folderHeader) m_folderHeader->setVisible(false);
    if (m_topHud) m_topHud->setVisible(true);
    if (m_leftSidebar) m_leftSidebar->setVisible(true);
    if (m_rightSidebar) m_rightSidebar->setVisible(true);
    if (m_pageIndicator)
        m_pageIndicator->clearActiveColor();
    m_grid->setRect({kGridRectX, kGridRectY, kGridRectW, kGridRectH});
    applyDisplayModel(buildRootFolderModel(), folderTitleId(oldId), false);
    syncPageIndicator();
    if (preserveEditMode) {
        // Focus is on the folder we just left; use that as the root placement target.
        m_editTargetIndex = findTitleIndex(folderTitleId(oldId));
        syncEditPlacementAfterModelChange(false);
        reattachEditSourceIcon();
        m_titlePill->setText(nxui::I18n::instance().tr("game.move_prefix", "Move: ") + m_editHeldTitle);
        m_titlePill->setVisible(true);
    }
    m_audio.playSfx(Sfx::ModalHide);
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::activateApplication(GlossyIcon* source, AppEntry* entry,
                                      std::uint64_t titleId,
                                      const std::string& launchTitle) {
    if (!source || titleId == 0) return;
    if (m_launcher.isAppSuspended(titleId)) {
        m_audio.playSfx(Sfx::LaunchGame);
        m_launchAnim->start(source->focusRect(), source->texture(),
            source->cornerRadius(), m_theme.panelBase, m_theme.panelBorder,
            0, {}, nullptr,
            [this, titleId, launchTitle]() {
                m_widgetStore.recordLaunch(titleId, launchTitle,
                    static_cast<std::int64_t>(std::time(nullptr)));
                m_widgetStore.save();
                m_launcher.resumeApplication();
            });
        return;
    }

    if (entry && !entry->isLaunchable()) {
        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = source;
        std::string reason;
        auto& i18n = nxui::I18n::instance();
        if (entry->isGameCardNotInserted())
            reason = i18n.tr("error.gamecard_not_inserted", "Game card is not inserted.");
        else if (entry->needsVerify())
            reason = i18n.tr("error.needs_verify", "Game data needs verification.");
        else if (entry->needsUpdate())
            reason = i18n.tr("error.needs_update", "A required update is available.");
        else if (!entry->hasContents())
            reason = i18n.tr("error.no_contents", "Game data is missing.");
        else
            reason = i18n.tr("error.cannot_launch", "This game cannot be launched.");
        m_dialog->show(i18n.tr("error.title", "Cannot Launch"), reason,
                       {{i18n.tr("button.ok", "OK"), [this]() {}, true}}, 0, {});
        focusManager().setFocus(m_dialog.get());
        return;
    }

    const nxui::Rect frame = source->focusRect();
    const nxui::Texture* texture = source->texture();
    const float radius = source->cornerRadius();
    const nxui::Color base = m_theme.panelBase;
    const nxui::Color border = m_theme.panelBorder;
    auto startLaunch = [this, frame, texture, radius, base, border,
                        titleId, launchTitle](AccountUid uid) {
        m_audio.playSfx(Sfx::LaunchGame);
        m_launchAnim->start(frame, texture, radius, base, border, titleId, uid,
            [this, launchTitle](std::uint64_t id, AccountUid selectedUid) {
                m_widgetStore.recordLaunch(id, launchTitle,
                    static_cast<std::int64_t>(std::time(nullptr)));
                m_widgetStore.save();
                m_launcher.launchApplication(id, selectedUid);
            });
    };

    if (entry) {
        if (!entry->startupUserKnown) {
            entry->startupUserAccount = 1;
            entry->startupUserAccountOption = 0;
            entry->userRequired = true;
        }
        DebugLog::log("[launcher] user decision tid=%016lX startup_user=%u option=%u interactive_user=%d",
                      titleId, (unsigned)entry->startupUserAccount,
                      (unsigned)entry->startupUserAccountOption,
                      entry->userRequired ? 1 : 0);

        if (entry->startupUserAccount == 0) {
            AccountUid emptyUid{};
            startLaunch(emptyUid);
            return;
        }
        if (m_config.defaultProfileEnabled) {
            AccountUid defaultUid{};
            if (hexToAccountUid(m_config.defaultProfileUid, defaultUid)) {
                startLaunch(defaultUid);
                return;
            }
        }

        AccountUid silentUid{};
        const bool networkRequired = entry->startupUserAccount == 2;
        const Result silentResult = accountTrySelectUserWithoutInteraction(
            &silentUid, networkRequired);
        if (R_SUCCEEDED(silentResult) && accountUidIsValid(&silentUid)) {
            startLaunch(silentUid);
            return;
        }
        if (!entry->userRequired) {
            AccountUid emptyUid{};
            startLaunch(emptyUid);
            return;
        }
    }

    if (m_userSelect) {
        const bool usersLoaded = m_userSelect->loadUsers(app().gpu(), app().renderer());
        if (usersLoaded) m_audio.playSfx(Sfx::ModalShow);
        m_userSelect->showUserSelect(
            [startLaunch](AccountUid uid) { startLaunch(uid); });
        focusManager().setFocus(m_userSelect.get());
    }
}
#endif

std::shared_ptr<GlossyIcon> WiiUMenuApp::makeIcon(const AppEntry& entry) {
    auto icon = std::make_shared<GlossyIcon>();
    icon->setEntryKind(entry.kind);
    icon->setFolderPreviewCount(entry.folderPreviewCount);
    icon->setFolderVisualSeed(entry.folderId);
    icon->setFolderColorIndex(entry.folderColorIndex);
    icon->setFolderStyleIndex(m_config.folderStyle);
    if (entry.kind == GridEntryKind::WidgetContinuation) {
        icon->setTag("widget_continuation");
        icon->setFocusable(false);
        icon->setVisible(false);
        icon->forceVisible();
        return icon;
    }
    if (entry.kind == GridEntryKind::Empty) {
        icon->setTag("glossy_icon");
        icon->setTitle("");
        icon->setTitleId(0);
        icon->setFocusable(true);
        auto& i18n = nxui::I18n::instance();
        icon->setAccessibilityLabel(i18n.tr("accessibility.grid.empty_slot", "Empty slot"));
        icon->setAccessibilityRole(i18n.tr("accessibility.roles.slot", "slot"));
        icon->setAccessibilityHint(i18n.tr(
            "accessibility.hints.grid_empty",
            "Plus opens the add menu. A places software being moved out of a folder."));
        icon->setNotLaunchable(false);
        icon->setCornerRadius(m_theme.iconCornerRadius);
        icon->setPanelOpacity(0.96f);
        return icon;
    }

    if (entry.isWidget()) {
        auto& i18n = nxui::I18n::instance();
        const auto& recent = m_widgetStore.recentActivity();
        std::string primary;
        std::string secondary;
        switch (entry.widgetType) {
            case switchu::widgets::WidgetType::Clock:
                break;
            case switchu::widgets::WidgetType::RecentlyPlayed:
                primary = recent.title.empty()
                    ? i18n.tr("widget.no_recent_game", "No recent game") : recent.title;
                break;
            case switchu::widgets::WidgetType::RecentPlaytime:
                primary = recent.title.empty()
                    ? i18n.tr("widget.no_recent_game", "No recent game") : recent.title;
                secondary = widgetDurationLabel(
                    recent.totalSeconds > 0 ? recent.totalSeconds : recent.recentSeconds);
                break;
            case switchu::widgets::WidgetType::RandomScreenshot:
            case switchu::widgets::WidgetType::ImagePin:
            case switchu::widgets::WidgetType::Batteries:
                break;
        }
        std::string assetPath = entry.widgetType == switchu::widgets::WidgetType::RandomScreenshot
            ? randomScreenshotPath(entry.widgetId)
            : resolveWidgetAssetRef(entry.widgetAssetRef);
        icon->setTag("glossy_icon");
        icon->setFont(&m_fontNormal);
        icon->setTitle(entry.title);
        icon->setTitleId(entry.titleId);
        icon->setFocusable(true);
        icon->setCornerRadius(m_theme.iconCornerRadius);
        icon->setBaseColor(m_theme.iconDefault);
        icon->setLoadingColor(m_theme.cursorNormal);
        icon->setWidgetData(entry.widgetType, entry.widgetColumns, entry.widgetRows,
                            std::move(primary), std::move(secondary), assetPath,
                            &app().gpu(), &app().renderer(),
                            !assetPath.empty());
        if (entry.widgetType == switchu::widgets::WidgetType::RecentlyPlayed)
            icon->setWidgetHeader(i18n.tr("widget.recently_played", "Recently played"));
        if (entry.widgetType == switchu::widgets::WidgetType::RecentPlaytime)
            icon->setWidgetHeader(i18n.tr("widget.played_recently", "Played recently"));
        if (entry.widgetType == switchu::widgets::WidgetType::Batteries)
        icon->setConsoleBattery(m_consoleBatteryPercent,
                                m_consoleBatteryCharging);
        icon->setBatteryIconTextures(&m_batteryConsoleTex,
                                     &m_batteryJoyconLeftTex,
                                     &m_batteryJoyconRightTex);
        if ((entry.widgetType == switchu::widgets::WidgetType::RecentlyPlayed ||
             entry.widgetType == switchu::widgets::WidgetType::RecentPlaytime) &&
            recent.titleId != 0) {
            ensureRecentWidgetAssets(recent.titleId);
            const bool assetsMatch = m_recentWidgetLoadedTitleId == recent.titleId;
            icon->setWidgetGameTextures(recent.titleId,
                assetsMatch ? m_recentWidgetHero.get() : nullptr,
                assetsMatch ? m_recentWidgetLogo.get() : nullptr,
                assetsMatch ? m_recentWidgetIcon.get() : nullptr);
        }
        icon->setAccessibilityLabel(entry.title);
        icon->setAccessibilityRole(i18n.tr("accessibility.roles.widget", "widget"));
        if (entry.widgetType == switchu::widgets::WidgetType::RecentlyPlayed &&
            recent.titleId != 0) {
            icon->setAccessibilityHint(i18n.tr(
                "accessibility.hints.recently_played_widget",
                "A to launch the recently played game. Plus for widget options. Y to move."));
#ifdef SWITCHU_MENU
            GlossyIcon* raw = icon.get();
            const std::uint64_t recentTitleId = recent.titleId;
            const std::string recentTitle = recent.title;
            icon->setOnActivate([this, raw, recentTitleId, recentTitle]() {
                auto found = std::find_if(m_allApps.begin(), m_allApps.end(),
                    [recentTitleId](const AppEntry& appEntry) {
                        return appEntry.titleId == recentTitleId;
                    });
                AppEntry* appEntry = found == m_allApps.end() ? nullptr : &*found;
                activateApplication(raw, appEntry, recentTitleId, recentTitle);
            });
#endif
        } else {
            icon->setAccessibilityHint(i18n.tr(
                "accessibility.hints.widget", "Plus for widget options. Y to move."));
        }
        return icon;
    }

    if (entry.isFolder()) {
        icon->setTag("glossy_icon");
        icon->setFont(&m_fontSmall);
        icon->setTitle(entry.title);
        icon->setTitleId(entry.titleId);
        icon->setFocusable(true);
        icon->setCornerRadius(m_theme.iconCornerRadius);
        icon->setBaseColor(m_theme.iconDefault);
        icon->setAccessibilityLabel(entry.title);
        auto& i18n = nxui::I18n::instance();
        icon->setAccessibilityRole(i18n.tr("accessibility.roles.folder", "folder"));
        icon->setAccessibilityHint(i18n.tr(
            "folder.open_hint", "A to open. Plus for folder options. Y to move."));
        const std::uint32_t folderId = entry.folderId;
        icon->setOnActivate([this, folderId]() {
            requestOpenFolder(folderId);
        });
        return icon;
    }

    icon->setTag("glossy_icon");
    icon->setFont(&m_fontNormal);
    icon->setTitle(entry.title);
    icon->setTitleId(entry.titleId);
    icon->setAccessibilityLabel(entry.title);
    auto& i18n = nxui::I18n::instance();
    icon->setAccessibilityRole(entry.isGameCard()
        ? i18n.tr("accessibility.roles.game_card", "game card")
        : i18n.tr("accessibility.roles.game", "game"));
    icon->setAccessibilityHint(entry.isLaunchable()
        ? i18n.tr("accessibility.hints.game_launchable", "A to launch. Plus for options. Y to move. ZL or ZR to change page.")
        : i18n.tr("accessibility.hints.game_blocked", "A to show why this item is blocked."));
    // Texture is set by IconStreamer::onPageChanged() — not here.
    icon->setCornerRadius(m_theme.iconCornerRadius);
    icon->setLoadingColor(m_theme.cursorNormal);
    icon->setIsGameCard(entry.isGameCard());
    icon->setGameCardTexture(&m_gameCardTex);
    icon->setNotLaunchable(!entry.isLaunchable());
    icon->setGridSpan(entry.widgetColumns, entry.widgetRows);
    if (entry.widgetColumns > 1 && entry.widgetRows == 1 &&
        m_appLayoutMode == AppLayoutMode::Grid) {
        const auto artwork = m_gameArtwork.find(entry.titleId);
        if (artwork != m_gameArtwork.end())
            icon->setWideGameTextures(artwork->second.hero.get(),
                                      artwork->second.logo.get());
    }

#ifdef SWITCHU_MENU
    if (m_launcher.suspendedTitleId() != 0 &&
        entry.titleId == m_launcher.suspendedTitleId())
        icon->setSuspended(true);

    GlossyIcon* raw = icon.get();
    icon->setOnActivate([this, raw]() {
        uint64_t tid = raw->titleId();
        if (m_launcher.isAppSuspended(tid)) {
            m_audio.playSfx(Sfx::LaunchGame);
            nxui::Rect   fr   = raw->focusRect();
            const nxui::Texture* tex = raw->texture();
            float  cr   = raw->cornerRadius();
            nxui::Color  base = m_theme.panelBase;
            nxui::Color  bord = m_theme.panelBorder;
            m_launchAnim->start(fr, tex, cr, base, bord, 0, {},
                nullptr,
                [this, tid, title = raw->title()]() {
                    m_widgetStore.recordLaunch(tid, title,
                        static_cast<std::int64_t>(std::time(nullptr)));
                    m_widgetStore.save();
                    m_launcher.resumeApplication();
                });
        } else {
            AppEntry* entry = nullptr;
            int entryIndex = findTitleIndex(tid);
            if (entryIndex >= 0)
                entry = &m_model.at(entryIndex);
            if (entry && !entry->isLaunchable()) {
                m_audio.playSfx(Sfx::ModalShow);
                m_dialogReturnFocus = raw;
                std::string reason;
                auto& i18n = nxui::I18n::instance();
                if (entry->isGameCardNotInserted())
                    reason = i18n.tr("error.gamecard_not_inserted", "Game card is not inserted.");
                else if (entry->needsVerify())
                    reason = i18n.tr("error.needs_verify", "Game data needs verification.");
                else if (entry->needsUpdate())
                    reason = i18n.tr("error.needs_update", "A required update is available.");
                else if (!entry->hasContents())
                    reason = i18n.tr("error.no_contents", "Game data is missing.");
                else
                    reason = i18n.tr("error.cannot_launch", "This game cannot be launched.");
                m_dialog->show(
                    i18n.tr("error.title", "Cannot Launch"),
                    reason,
                    {{i18n.tr("button.ok", "OK"), [this]() {}, true}},
                    0, {}
                );
                focusManager().setFocus(m_dialog.get());
                return;
            }

            nxui::Rect   fr   = raw->focusRect();
            const nxui::Texture* tex = raw->texture();
            float  cr   = raw->cornerRadius();
            nxui::Color  base = m_theme.panelBase;
            nxui::Color  bord = m_theme.panelBorder;
            const std::string launchTitle = raw->title();
            auto startLaunch = [this, fr, tex, cr, base, bord, tid, launchTitle](AccountUid uid) {
                m_audio.playSfx(Sfx::LaunchGame);
                m_launchAnim->start(fr, tex, cr, base, bord, tid, uid,
                    [this, launchTitle](uint64_t id, AccountUid u) {
                        m_widgetStore.recordLaunch(id, launchTitle,
                            static_cast<std::int64_t>(std::time(nullptr)));
                        m_widgetStore.save();
                        m_launcher.launchApplication(id, u);
                    });
            };
            if (entry) {
                if (!entry->startupUserKnown) {
                    entry->startupUserAccount = 1;
                    entry->startupUserAccountOption = 0;
                    entry->userRequired = true;
                }
                DebugLog::log("[launcher] user decision tid=%016lX startup_user=%u option=%u interactive_user=%d",
                              tid,
                              (unsigned)entry->startupUserAccount,
                              (unsigned)entry->startupUserAccountOption,
                              entry->userRequired ? 1 : 0);

                if (entry->startupUserAccount == 0) {
                    AccountUid emptyUid = {};
                    DebugLog::log("[launcher] skipping user select: NACP StartupUserAccount=None");
                    startLaunch(emptyUid);
                    return;
                }

                if (m_config.defaultProfileEnabled) {
                    AccountUid defaultUid = {};
                    if (hexToAccountUid(m_config.defaultProfileUid, defaultUid)) {
                        DebugLog::log("[launcher] skipping user select: default profile configured uid[0]=0x%016lX uid[1]=0x%016lX",
                                      defaultUid.uid[0], defaultUid.uid[1]);
                        startLaunch(defaultUid);
                        return;
                    }
                    DebugLog::log("[launcher] default profile enabled but uid is invalid");
                }

                AccountUid silentUid = {};
                const bool networkRequired = entry->startupUserAccount == 2;
                Result silentRc = accountTrySelectUserWithoutInteraction(&silentUid, networkRequired);
                DebugLog::log("[launcher] TrySelectUserWithoutInteraction network_required=%d rc=0x%X uid_valid=%d uid[0]=0x%016lX uid[1]=0x%016lX",
                              networkRequired ? 1 : 0,
                              silentRc,
                              accountUidIsValid(&silentUid) ? 1 : 0,
                              silentUid.uid[0],
                              silentUid.uid[1]);
                if (R_SUCCEEDED(silentRc) && accountUidIsValid(&silentUid)) {
                    DebugLog::log("[launcher] skipping user select: silent account selection succeeded");
                    startLaunch(silentUid);
                    return;
                }

                if (!entry->userRequired) {
                    AccountUid emptyUid = {};
                    DebugLog::log("[launcher] skipping user select fallback: startup_user=%u option=%u did not require interactive picker",
                                  (unsigned)entry->startupUserAccount,
                                  (unsigned)entry->startupUserAccountOption);
                    startLaunch(emptyUid);
                    return;
                }
            }
            if (m_userSelect) {
                bool usersLoaded = m_userSelect->loadUsers(app().gpu(), app().renderer());
                DebugLog::log("[UserSelect] lazy load result=%d", usersLoaded ? 1 : 0);
                if (usersLoaded)
                    m_audio.playSfx(Sfx::ModalShow);
            }
            m_userSelect->showUserSelect([startLaunch](AccountUid uid) { startLaunch(uid); });
            focusManager().setFocus(m_userSelect.get());
        }
    });
#else
    icon->setOnActivate([this]() {
        m_audio.playSfx(Sfx::Activate);
    });
#endif
    return icon;
}

void WiiUMenuApp::buildGrid() {
    reloadThemePresets();

    m_activePresetName = m_config.themePreset;
    ThemePreset* preset = findPresetPtr(m_activePresetName);
    if (!preset) {
        m_activePresetName = "builtin:Default Light";
        preset = findPresetPtr(m_activePresetName);
    }
    if (!preset) {
        m_activePresetName = "Default Light";
        preset = findPresetPtr(m_activePresetName);
    }

    if (preset)
        m_activePresetName = preset->id.empty() ? preset->name : preset->id;

    m_activeColors = preset->colors;
    m_activeMode = preset->mode;

    m_effectivePreset = buildEffectiveThemePreset();
    m_theme = m_effectivePreset.toTheme();

    m_background = std::make_shared<WaraWaraBackground>();
    m_background->setRect({0, 0, 1280, 720});
    applyThemeResources(m_effectivePreset);

    const auto iconBuildStarted = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<GlossyIcon>> icons;
    for (int i = 0; i < m_model.count(); ++i)
        icons.push_back(makeIcon(m_model.at(i)));
    DebugLog::log("[init] grid icon objects built count=%d in %ldms",
                  m_model.count(),
                  static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - iconBuildStarted).count()));

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();

    m_grid = std::make_shared<IconGrid>();
    m_grid->setRect({kGridRectX, kGridRectY, kGridRectW, kGridRectH});
    m_grid->setLayoutMode(m_appLayoutMode);
    m_grid->setup(std::move(icons),
                  std::clamp(m_config.gridColumns, 3, 8),
                  std::clamp(m_config.gridRows, 2, 5),
                  gridMetrics.cellW, gridMetrics.cellH,
                  gridMetrics.padX, gridMetrics.padY);

    m_cursor = std::make_shared<SelectionCursor>();
    m_pointerCursor = std::make_shared<SelectionCursor>();
    m_pointerCursor->setVisible(false);

    m_clock = std::make_shared<DateTimeWidget>();
    m_clock->setSize(150, 62);
    m_clock->setMarginTop(14.f);
    m_clock->setMarginLeft(24.f);
    m_clock->setFont(&m_fontNormal);
    m_clock->setSmallFont(&m_fontSmall);
    m_clock->setClockService(&m_clockService);
    m_clock->setUse12HourClock(m_config.clockUse12Hour);
    m_clock->setCornerRadius(m_theme.cellCornerRadius);
    m_clock->setForceLiquidGlass(true);
    m_clock->setBlurEnabled(false);

    m_battery = std::make_shared<BatteryWidget>();
    m_battery->setMarginTop(14.f);
    m_battery->setMarginRight(24.f);
    m_battery->setSize(150, 62);
    m_battery->setFont(&m_fontSmall);
    m_battery->setCornerRadius(m_theme.cellCornerRadius);
    m_battery->setForceLiquidGlass(true);
    m_battery->setBlurEnabled(false);

    buildUserAvatarBar(!m_fastReturnRequested);

    m_titlePill = std::make_shared<TitlePillWidget>();
    m_titlePill->setPosition(0, 630.f);
    m_titlePill->setFont(&m_fontNormal);
    m_titlePill->setPadding(9.f, 22.f, 9.f, 22.f);
    m_titlePill->setForceLiquidGlass(true);
    m_titlePill->setBlurEnabled(false);

    m_pageIndicator = std::make_shared<PageIndicator>();
    m_pageIndicator->setRect({0, 685.f, 1280.f, 28.f});
    m_pageIndicator->setTheme(&m_theme);
    m_pageIndicator->setForceLiquidGlass(true);
    m_pageIndicator->setBlurEnabled(false);

    m_launchAnim = std::make_shared<LaunchAnimation>();

    m_userSelect = std::make_shared<OverlayDialog>();
    m_userSelect->setFont(&m_fontNormal);
    m_userSelect->setSmallFont(&m_fontSmall);
    m_userSelect->setTheme(&m_theme);
    m_userSelect->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                    m_config.accessibilitySpeakPosition);
    m_userSelect->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_userSelect->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_userSelect->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_userSelect->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_userSelect->onAccessibilityStructuredAnnouncement([this](const std::string& context,
                                                               const std::string& position,
                                                               const std::string& summary,
                                                               bool forceRepeat,
                                                               bool forceContext) {
        m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
    });

    m_dialog = std::make_shared<OverlayDialog>();
    m_dialog->setFont(&m_fontNormal);
    m_dialog->setSmallFont(&m_fontSmall);
    m_dialog->setTheme(&m_theme);
    m_dialog->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                m_config.accessibilitySpeakPosition);
    m_dialog->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_dialog->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_dialog->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_dialog->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_dialog->onAccessibilityStructuredAnnouncement([this](const std::string& context,
                                                           const std::string& position,
                                                           const std::string& summary,
                                                           bool forceRepeat,
                                                           bool forceContext) {
        m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
    });

    m_contextMenu = std::make_shared<ContextMenu>();
    m_contextMenu->setFont(&m_fontNormal);
    m_contextMenu->setSmallFont(&m_fontSmall);
    m_contextMenu->setTheme(&m_theme);
    m_contextMenu->onNavigate([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_contextMenu->onActivate([this]() { m_audio.playSfx(Sfx::Activate); });
    m_contextMenu->onClose([this]() {
        m_audio.playSfx(Sfx::ModalHide);
        if (!(m_dialog && m_dialog->isActive()) &&
            isCurrentFocusableWidget(m_contextMenuReturnFocus)) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_contextMenuReturnFocus);
        }
        m_contextMenuReturnFocus = nullptr;
        updateCursor();
    });

    m_progressDialog = std::make_shared<ProgressDialog>();
    m_progressDialog->setFont(&m_fontNormal);
    m_progressDialog->setSmallFont(&m_fontSmall);
    m_progressDialog->setTheme(&m_theme);

    app().renderer().setBoxWireframeEnabled(m_showWireframe);

    wireFocusCallback();
    m_grid->onPageSwitched([this]() {
        if (m_editMode && m_editTargetIndex >= 0) {
            const int perPage = std::max(1, m_grid->iconsPerPage());
            const int local = m_editTargetIndex % perPage;
            m_editTargetIndex = m_grid->currentPage() * perPage + local;
            if (m_editTargetIndex >= m_model.count())
                m_editTargetIndex = std::max(0, m_model.count() - 1);
            if (m_editGhostIcon)
                m_editGhostTargetRect = m_grid->gridSpanRect(
                    m_editTargetIndex,
                    m_editGhostIcon->gridSpanColumns(),
                    m_editGhostIcon->gridSpanRows());
        }
        // Stream icon textures for the new page.
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
        auto* target = m_grid->focusManager().current();
        if (target)
            focusManager().setFocus(target);
        updateCursor();
    });

    int initialPage = 0;
#ifdef SWITCHU_MENU
    if (m_launcher.suspendedTitleId() != 0) {
        int suspendedIndex = findTitleIndex(m_launcher.suspendedTitleId());
        if (suspendedIndex >= 0 && m_grid->iconsPerPage() > 0)
            initialPage = suspendedIndex / m_grid->iconsPerPage();
        if (initialPage > 0)
            m_grid->setPage(initialPage);
    }
#endif

    const bool fastReturn = m_fastReturnRequested;
    if (fastReturn) {
        // Application updates once before presenting its first frame. Two ticks
        // keep sidebar uploads off the pre-first-frame critical path.
        m_deferredInitialAssetFrames = 2;
        DebugLog::log("[init] return path: deferring initial icon/sidebar uploads");
    } else {
        // Load textures for the initial visible page.
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }

    if (fastReturn) {
        for (auto& icon : m_grid->allIcons())
            icon->forceVisible();
        // Placeholders cover progressively loaded artwork, so do not keep an
        // already usable HOME scene hidden behind the old opaque fade.
        m_returnFadeTimer = 0.f;
    } else {
        // The first visible page can animate again: the black-screen issue was
        // caused by a blocking GPU icon-upload wait, not by the appear tween.
        m_grid->startAppearAnimation();
    }
    if (m_tutorialStartupFade) {
        m_tutorialStartupFadeTimer = kTutorialStartupFadeDur;
        const std::uint64_t freq = armGetSystemTickFreq();
        m_tutorialStartupFadeDeadlineTick =
            armGetSystemTick() + static_cast<std::uint64_t>(
                kTutorialStartupFadeDur * static_cast<float>(freq));
    } else {
        m_tutorialStartupFadeDeadlineTick = 0;
    }

    SidebarManager::Actions sidebarActions;
#ifdef SWITCHU_MENU
    sidebarActions.onAlbum       = [this]() { m_launcher.launchAlbum(); };
    sidebarActions.onMiiEditor   = [this]() { m_launcher.launchMiiEditor(); };
    sidebarActions.onControllers = [this]() { m_launcher.launchControllerPairing(); };
#else
    sidebarActions.onAlbum       = [this]() { m_audio.playSfx(Sfx::Activate); };
    sidebarActions.onMiiEditor   = [this]() { m_audio.playSfx(Sfx::Activate); };
    sidebarActions.onControllers = [this]() { m_audio.playSfx(Sfx::Activate); };
#endif
    sidebarActions.onSettings = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (m_settings) {
            m_navigator.navigate(switchu::navigation::Route::Settings);
            if (m_themeShop && m_themeShop->isActive())
                m_themeShop->hide();
            m_settings->show();
            focusManager().setFocus(m_settings.get());
        }
    };
    sidebarActions.onSleep = [this]() {
        if (!m_dialog) return;
        auto& i18n = nxui::I18n::instance();
        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            i18n.tr("power.title", "Power"),
            i18n.tr("power.choose_action", "Choose a power action."),
            {
                // {i18n.tr("button.cancel", "Cancel"), [this]() {  }, true},
                {i18n.tr("power.sleep", "Sleep"), [this]() {
#ifdef SWITCHU_MENU
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.enterSleep();
#else
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    app().requestExit();
#endif
                }, true},
                {i18n.tr("power.shutdown", "Shutdown"), [this]() {
#ifdef SWITCHU_MENU
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.shutdown();
#else
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    app().requestExit();
#endif
                }, true},
                {i18n.tr("power.reboot", "Reboot"), [this]() {
#ifdef SWITCHU_MENU
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.reboot();
#else
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    app().requestExit();
#endif
                }, true}
            },
            0,
            {}
        );
        focusManager().setFocus(m_dialog.get());
    };
    sidebarActions.onMiiverse = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (!m_themeShop) return;
        m_navigator.navigate(switchu::navigation::Route::ThemeShop);
        if (m_settings && m_settings->isActive())
            m_settings->hide();
        refreshThemeShopState();
        m_themeShop->show();
        focusManager().setFocus(m_themeShop.get());
    };

    m_sidebar.build(app().gpu(), app().renderer(), SD_ASSETS, sidebarActions);
    configureDynamicLineNavigation();
    if (!fastReturn) {
        m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS,
                               resolveThemeAssetPath(m_effectivePreset, m_effectivePreset.icons.basePath));
    }

    wireGlobalActions();
    applyTheme();

    auto& root = rootBox();
    root.clearChildren();

    m_bgLayer = std::make_shared<nxui::Box>();
    m_bgLayer->setRect({0, 0, 1280, 720});
    m_bgLayer->setTag("bgLayer");
    m_bgLayer->setWireframeEnabled(false);
    m_bgLayer->addChild(m_background);
    m_steamGridDbBackdrop = std::make_shared<SteamGridDbBackdrop>(
        app().gpu(), app().renderer(), &m_threadPool);
    m_steamGridDbBackdrop->setEnabled(m_config.steamGridDbEnabled);
    m_steamGridDbBackdrop->setLayoutMode(m_appLayoutMode);

    m_contentLayer = std::make_shared<nxui::Box>();
    m_contentLayer->setRect({0, 0, 1280, 720});
    m_contentLayer->setTag("contentLayer");
    m_contentLayer->setWireframeEnabled(false);

    m_folderBackdrop = std::make_shared<FolderBackdrop>();
    m_folderBackdrop->setRect({0, 0, 1280, 720});
    m_folderBackdrop->setVisible(false);

    m_folderHeader = std::make_shared<nxui::GlassPanel>();
    m_folderHeader->setRect({410.f, 78.f, 460.f, 58.f});
    m_folderHeader->setCornerRadius(22.f);
    m_folderHeader->setLiquidGlassEnabled(true);
    m_folderHeader->setForceLiquidGlass(true);
    m_folderHeader->setBlurEnabled(false);
    m_folderHeader->setVisible(false);
    m_folderHeaderLabel = std::make_shared<nxui::Label>("");
    // nxui child rectangles are screen-space; using {18, 6} placed this
    // label outside its bubble in the upper-left corner.
    m_folderHeaderLabel->setRect({428.f, 84.f, 424.f, 46.f});
    m_folderHeaderLabel->setFont(&m_fontNormal);
    m_folderHeaderLabel->setScale(0.66f);
    m_folderHeaderLabel->setMultiline(true);
    m_folderHeaderLabel->setLineSpacing(1.0f);
    m_folderHeaderLabel->setHAlign(nxui::Label::HAlign::Center);
    m_folderHeaderLabel->setVAlign(nxui::Label::VAlign::Center);
    m_folderHeader->addChild(m_folderHeaderLabel);

    m_topHud = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    m_topHud->setRect({0, 0, 1280, 90});
    m_topHud->setTag("topHud");
    m_topHud->setWireframeEnabled(false);
    m_topHud->setJustifyContent(nxui::JustifyContent::SPACE_BETWEEN);
    m_topHud->setAlignItems(nxui::AlignItems::FLEX_START);
    m_topHud->addChild(m_clock);
    if (m_userAvatarBar)
        m_topHud->addChild(m_userAvatarBar);
    m_topHud->addChild(m_battery);
    m_topHud->layout();

    m_leftSidebar = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
    m_leftSidebar->setTag("leftSidebar");
    m_leftSidebar->setWireframeEnabled(false);
    for (auto& btn : m_sidebar.leftButtons())
        m_leftSidebar->addChild(btn);

    m_rightSidebar = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
    m_rightSidebar->setTag("rightSidebar");
    m_rightSidebar->setWireframeEnabled(false);
    for (auto& btn : m_sidebar.rightButtons())
        m_rightSidebar->addChild(btn);

    m_contentLayer->addChild(m_folderBackdrop);
    // Keep live SteamGridDB artwork above the folder's frozen transition
    // snapshot, while still placing it behind every interactive HOME widget.
    m_contentLayer->addChild(m_steamGridDbBackdrop);
    m_contentLayer->addChild(m_grid);
    m_contentLayer->addChild(m_folderHeader);
    m_contentLayer->addChild(m_leftSidebar);
    m_contentLayer->addChild(m_rightSidebar);
    m_contentLayer->addChild(m_topHud);
    m_contentLayer->addChild(m_titlePill);
    m_contentLayer->addChild(m_pageIndicator);

    m_overlayLayer = std::make_shared<nxui::Box>();
    m_overlayLayer->setRect({0, 0, 1280, 720});
    m_overlayLayer->setTag("overlayLayer");
    m_overlayLayer->setWireframeEnabled(false);
    m_overlayLayer->addChild(m_cursor);
    m_overlayLayer->addChild(m_userSelect);

    createSettings();
    createThemeShop();
    createGameOptions();
    m_steamGridDbPicker = std::make_shared<SteamGridDbPickerScreen>(
        app().gpu(), app().renderer(), m_threadPool);
    m_steamGridDbPicker->setFont(&m_fontNormal);
    m_steamGridDbPicker->setSmallFont(&m_fontSmall);
    m_steamGridDbPicker->setTheme(&m_theme);
    m_steamGridDbPicker->onClosed([this]() {
        if (m_gameOptions && m_gameOptions->isActive())
            focusManager().setFocus(m_gameOptions.get());
    });
    m_steamGridDbPicker->onSearch([this]() { editSteamGridDbPickerQuery(); });
    m_steamGridDbPicker->onApply(
        [this](const SteamGridDbManager::BrowseResult& browse,
               const SteamGridDbManager::Candidate& candidate) {
            applySteamGridDbCandidate(browse, candidate);
        });
    m_overlayLayer->addChild(m_steamGridDbPicker);
    createFolderOptions();
    createControllerTest();

    m_overlayLayer->addChild(m_contextMenu);
    m_overlayLayer->addChild(m_dialog);
    m_overlayLayer->addChild(m_progressDialog);
    m_overlayLayer->addChild(m_launchAnim);
    m_overlayLayer->addChild(m_pointerCursor);

    root.addChild(m_bgLayer);
    root.addChild(m_contentLayer);
    root.addChild(m_overlayLayer);

    if (!focusTitle(m_launcher.suspendedTitleId())) {
        if (auto* firstIcon = m_grid->focusManager().current())
            focusManager().setFocus(firstIcon);
    }
    updateCursor();
    showFocusedSteamGridDbArtwork();
    m_themeRenderDebugFrames = 12;

    if (m_layoutDirty)
        saveMenuLayout();
}

std::string WiiUMenuApp::resolveSoundPresetId(const std::string& preset) const {
    std::string effectivePreset = preset;
    if (!isPackageSoundPreset(effectivePreset) && effectivePreset != kBuiltInSoundPreset) {
        DebugLog::log("[audio] preset '%s' blocked, using '%s' instead",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
        return kBuiltInSoundPreset;
    }

    if (!isPackageSoundPreset(effectivePreset))
        return effectivePreset;

    if (!resolveThemeSoundBase(installedThemePathFromPackagePreset(effectivePreset)).empty()) {
        DebugLog::log("[audio] package preset '%s' resolved from install directory", effectivePreset.c_str());
        return effectivePreset;
    }

    for (const auto& themePreset : m_allPresets) {
        if (themePreset.source != ThemePresetSource::InstalledPackage || themePreset.installPath.empty())
            continue;
        if (themePreset.id != effectivePreset && themePreset.soundPreset != effectivePreset)
            continue;
        return effectivePreset;
    }

    DebugLog::log("[audio] package preset '%s' unavailable, using '%s' instead",
                  effectivePreset.c_str(),
                  kBuiltInSoundPreset);
    return kBuiltInSoundPreset;
}

void WiiUMenuApp::loadSoundPreset(const std::string& preset) {
    std::string effectivePreset = preset;
    const bool useBuiltInBase = (effectivePreset == kBuiltInSoundPreset);
    const std::string builtInBase = std::string(SD_ASSETS) + "/sounds/" + kBuiltInSoundPreset;

    std::string base;
    if (!useBuiltInBase) {
        base = resolveThemeSoundBase(installedThemePathFromPackagePreset(effectivePreset));

        for (const auto& themePreset : m_allPresets) {
            if (!base.empty())
                break;
            if (themePreset.source != ThemePresetSource::InstalledPackage || themePreset.installPath.empty())
                continue;
            if (themePreset.id != effectivePreset && themePreset.soundPreset != effectivePreset)
                continue;

            base = resolveThemeSoundBase(themePreset.installPath);
            break;
        }
    }

    if (base.empty()) {
        if (isPackageSoundPreset(effectivePreset)) {
            base = installedThemePathFromPackagePreset(effectivePreset);
        } else {
            base = std::string(SD_ASSETS) + "/sounds/" + effectivePreset;
        }
    }
    DebugLog::log("[audio] Loading preset '%s' from %s", effectivePreset.c_str(), base.c_str());

    const bool hasCustomSfx = directoryExists(base + "/sfx");
    const bool hasCustomMusic = directoryExists(base + "/music");
    const std::string musicBase = hasCustomMusic ? base : builtInBase;
    const std::string preferredSfxBase = (!useBuiltInBase && hasCustomSfx) ? base : std::string();
    auto sfxPath = [&](const char* relativePath) {
        return resolveAudioOverridePath(preferredSfxBase, builtInBase, relativePath);
    };

    if (!useBuiltInBase && !hasCustomSfx) {
        DebugLog::log("[audio] preset '%s' has no custom SFX directory, using '%s' SFX fallback",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
    }
    if (!useBuiltInBase && !hasCustomMusic) {
        DebugLog::log("[audio] preset '%s' has no custom music, using '%s' music fallback",
                      effectivePreset.c_str(),
                      kBuiltInSoundPreset);
    }

    m_audio.loadSfx(Sfx::Navigate,        sfxPath("sfx/navigation.wav"));
    m_audio.loadSfx(Sfx::Activate,        sfxPath("sfx/activation.wav"));
    m_audio.loadSfx(Sfx::PageChange,      sfxPath("sfx/tab_transition.wav"));
    m_audio.loadSfx(Sfx::ModalShow,       sfxPath("sfx/show_modal.wav"));
    m_audio.loadSfx(Sfx::ModalHide,       sfxPath("sfx/hide_modal.wav"));
    m_audio.loadSfx(Sfx::LaunchGame,      sfxPath("sfx/launch_game.wav"));
    m_audio.loadSfx(Sfx::ThemeToggle,     sfxPath("sfx/toggle_on.wav"));
    m_audio.loadSfx(Sfx::ToggleOff,       sfxPath("sfx/toggle_off.wav"));
    m_audio.loadSfx(Sfx::SliderUp,        sfxPath("sfx/slider_up.wav"));
    m_audio.loadSfx(Sfx::SliderDown,      sfxPath("sfx/slider_down.wav"));
    m_audio.loadSfx(Sfx::ConfirmPositive, sfxPath("sfx/confirm.wav"));
    m_audio.loadSfx(Sfx::Volume,          sfxPath("sfx/volume.wav"));

    std::string musicDir = musicBase + "/music";
    std::error_code ec;
    if (std::filesystem::is_directory(musicDir, ec)) {
        std::vector<std::string> tracks;
        ec.clear();
        for (const auto& entry : std::filesystem::directory_iterator(musicDir, ec)) {
            if (ec)
                break;

            std::string name = entry.path().filename().string();
            if (name.size() > 4 && name.substr(name.size() - 4) == ".mp3")
                tracks.push_back(name);
        }
        std::sort(tracks.begin(), tracks.end(), [](const std::string& left, const std::string& right) {
            const bool leftIsHome = (left == "home.mp3");
            const bool rightIsHome = (right == "home.mp3");
            if (leftIsHome != rightIsHome)
                return leftIsHome;
            return left < right;
        });
        for (const auto& t : tracks)
            m_audio.loadTrack(musicDir + "/" + t);
        DebugLog::log("[audio] Loaded %zu music tracks", tracks.size());
    } else {
        DebugLog::log("[audio] No music directory for preset '%s'", effectivePreset.c_str());
    }
}

void WiiUMenuApp::changeSoundPreset(const std::string& preset) {
    const std::string effectivePreset = resolveSoundPresetId(preset);
    if (m_presetChangePending && effectivePreset == m_pendingSoundPreset) {
        DebugLog::log("[audio] Preset change skipped; '%s' is already pending",
                      effectivePreset.c_str());
        return;
    }

    if (m_audioStarted && effectivePreset == m_loadedSoundPreset) {
        DebugLog::log("[audio] Preset change skipped; '%s' is already active",
                      effectivePreset.c_str());
        return;
    }

    m_audio.stop();
    m_audio.clearTracks();
    m_audio.clearSfx();

    m_presetChangePending = true;
    m_pendingSoundPreset = effectivePreset;
    m_audioFuture = m_threadPool.submit([this, effectivePreset]() {
        loadSoundPreset(effectivePreset);
    });
}

std::vector<std::string> WiiUMenuApp::scanAvailablePresets() {
    std::vector<std::string> presets;
    std::string soundsDir = std::string(SD_ASSETS) + "/sounds";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(soundsDir, ec)) {
        if (ec)
            break;

        std::string name = entry.path().filename().string();
        if (name != kBuiltInSoundPreset) continue;

        std::string sub = entry.path().string();
        if (!entry.is_directory(ec)) {
            ec.clear();
            continue;
        }

        std::string sfxSub = sub + "/sfx";
        std::string musicSub = sub + "/music";
        bool hasSfx = directoryExists(sfxSub);
        bool hasMusic = directoryExists(musicSub);
        if (hasSfx || hasMusic)
            presets.push_back(name);
    }
    std::sort(presets.begin(), presets.end());
    return presets;
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::refreshAppList() {
    DebugLog::log("[refresh] starting async app list fetch");

    if (m_editMode)
        exitEditMode();

    if (m_asyncRefreshPending) {
        DebugLog::log("[refresh] already in progress, queueing another pass");
        m_refreshQueued = true;
        return;
    }

    if (m_launchAnim && m_launchAnim->isPlaying()) m_launchAnim->stop();
    if (m_userSelect && m_userSelect->isActive()) m_userSelect->hide();

    m_refreshPrevPage = m_grid ? m_grid->currentPage() : 0;
    m_asyncRefreshPending = true;
    m_refreshQueued = false;

    m_appLoader.startAsync(m_threadPool);
}

void WiiUMenuApp::finalizeRefresh() {
    DebugLog::log("[refresh] finalizing (GPU upload)");
    m_asyncRefreshPending = false;

    GridModel refreshedModel;
    IconStreamer refreshedStreamer;
    if (!m_appLoader.finalize(refreshedModel, refreshedStreamer)) {
        DebugLog::log("[refresh] failed; preserving the current grid");
        m_refreshCooldownFrames = 20;
        return;
    }
    DebugLog::log("[refresh] found %d apps", refreshedModel.count());

    if (gridModelsRefreshEquivalent(m_model, refreshedModel)) {
        DebugLog::log("[refresh] unchanged, keeping existing grid");
        m_refreshCooldownFrames = 20;
        if (m_layoutDirty)
            saveMenuLayout();
        return;
    }

    // Keep already-uploaded textures mapped by title. Destroying and
    // rebuilding the complete streamer here forced a GPU idle and made every
    // icon reappear progressively after even a one-title catalogue change.
    // The refreshed compressed bytes were fetched on the worker already; move
    // those in as well so newly added titles do not need a second SD read.
    m_iconStreamer.reconcileCatalog(std::move(refreshedStreamer));
    if (m_openFolderId != 0) {
        auto* focusedWidget = m_grid ? m_grid->focusManager().current() : nullptr;
        const std::uint64_t focused = focusedWidget && focusedWidget->tag() == "glossy_icon"
            ? static_cast<GlossyIcon*>(focusedWidget)->titleId() : 0;
        applyDisplayModel(buildOpenFolderModel(m_openFolderId), focused, false);
        m_refreshCooldownFrames = 20;
        if (m_layoutDirty) saveMenuLayout();
        DebugLog::log("[refresh] folder view restored folder=%u", m_openFolderId);
        return;
    }
    m_model = std::move(refreshedModel);

    std::vector<std::shared_ptr<GlossyIcon>> icons;
    for (int i = 0; i < m_model.count(); ++i) {
        auto icon = makeIcon(m_model.at(i));
        icon->setBaseColor(m_theme.iconDefault);
        icons.push_back(std::move(icon));
    }

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();

    m_grid->setup(std::move(icons),
                  std::clamp(m_config.gridColumns, 3, 8),
                  std::clamp(m_config.gridRows, 2, 5),
                  gridMetrics.cellW, gridMetrics.cellH,
                  gridMetrics.padX, gridMetrics.padY);
    if (m_refreshPrevPage > 0) m_grid->setPage(m_refreshPrevPage);
    wireFocusCallback();
    m_grid->onPageSwitched([this]() {
        if (m_editMode && m_editTargetIndex >= 0) {
            const int perPage = std::max(1, m_grid->iconsPerPage());
            const int local = m_editTargetIndex % perPage;
            m_editTargetIndex = m_grid->currentPage() * perPage + local;
            if (m_editTargetIndex >= m_model.count())
                m_editTargetIndex = std::max(0, m_model.count() - 1);
            if (m_editGhostIcon)
                m_editGhostTargetRect = m_grid->gridSpanRect(
                    m_editTargetIndex,
                    m_editGhostIcon->gridSpanColumns(),
                    m_editGhostIcon->gridSpanRows());
        }
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
        auto* target = m_grid->focusManager().current();
        if (target) focusManager().setFocus(target);
        updateCursor();
    });

    // Load textures for the restored page.
    int page = m_refreshPrevPage > 0 ? m_refreshPrevPage : 0;
    m_iconStreamer.onPageChanged(page, m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());

    // A refresh updates the model in place visually. Existing entries should
    // not replay the startup cascade; new entries use the loading placeholder
    // until their background decode completes.
    for (auto& icon : m_grid->allIcons())
        icon->forceVisible();
    if (auto* firstIcon = m_grid->focusManager().current())
        focusManager().setFocus(firstIcon);

    // Keep a short cooldown to coalesce duplicate app-record notifications.
    m_refreshCooldownFrames = 20;
    applyTheme();
    if (m_layoutDirty)
        saveMenuLayout();
    DebugLog::log("[refresh] done, %d icons on page %d", m_model.count(), m_grid->currentPage());
}

#endif

void WiiUMenuApp::onUpdate(float dt) {
    // Widget-owned images are intentionally managed before recording the next
    // frame. This is the only safe point to retire Deko textures from pages
    // that have left the screen and to warm the next page.
    syncWidgetPageAssets();
    // Reading and decoding happens on a worker. Transfer at most one recent
    // widget texture to the GPU per frame so returning HOME remains smooth.
    pollRecentWidgetAssets();
    pollGameArtworkAssets();

#ifdef NXUI_BACKEND_DEKO3D
    // Text is cached as GPU textures. Keep a reserved image-memory margin for
    // labels that only appear after opening a screen (notably Settings).
    // Maintenance happens here, before Application starts recording the next
    // frame, so no texture referenced by the current command buffer is evicted.
    {
        auto& gpu = app().gpu();
        constexpr std::uint64_t kTextImageReserve = 4u * 1024u * 1024u;
        const std::size_t textBytesBefore =
            m_fontNormal.cacheBytes() + m_fontSmall.cacheBytes();
        const bool requested = m_fontNormal.maintenanceRequested() ||
                               m_fontSmall.maintenanceRequested();
        const bool memoryPressure = gpu.imageMemoryAvailable() < kTextImageReserve;
        constexpr std::size_t kPressureEntriesPerFont = 32;
        constexpr std::size_t kPressureBytesPerFont = 1u * 1024u * 1024u;
        const bool usefulPressureTrim = memoryPressure &&
            (m_fontNormal.cacheEntryCount() > kPressureEntriesPerFont ||
             m_fontSmall.cacheEntryCount() > kPressureEntriesPerFont ||
             m_fontNormal.cacheBytes() > kPressureBytesPerFont ||
             m_fontSmall.cacheBytes() > kPressureBytesPerFont);
        if (requested || usefulPressureTrim) {
            DebugLog::log(
                "[text-cache] maintenance requested=%d pressure=%d gpu=%llu/%llu text=%zu entries=%zu",
                requested ? 1 : 0, memoryPressure ? 1 : 0,
                static_cast<unsigned long long>(gpu.imageMemoryUsed()),
                static_cast<unsigned long long>(gpu.imageMemoryBudget()),
                textBytesBefore,
                m_fontNormal.cacheEntryCount() + m_fontSmall.cacheEntryCount());
            // If no cached texture remains, merely acknowledge the request.
            // This avoids an idle-wait loop when non-text artwork alone has
            // consumed all available image memory.
            if (textBytesBefore > 0)
                gpu.waitIdle();
            if (memoryPressure) {
                // A failed allocation means the fixed pressure target was not
                // enough. Halve both caches on each request until enough room
                // exists; a passive pressure trim keeps the normal 32/1 MiB
                // target instead.
                const std::size_t normalEntries = requested
                    ? m_fontNormal.cacheEntryCount() / 2
                    : kPressureEntriesPerFont;
                const std::size_t smallEntries = requested
                    ? m_fontSmall.cacheEntryCount() / 2
                    : kPressureEntriesPerFont;
                const std::size_t normalBytes = requested
                    ? m_fontNormal.cacheBytes() / 2
                    : kPressureBytesPerFont;
                const std::size_t smallBytes = requested
                    ? m_fontSmall.cacheBytes() / 2
                    : kPressureBytesPerFont;
                m_fontNormal.trimCache(
                    std::min(normalEntries, kPressureEntriesPerFont),
                    std::min(normalBytes, kPressureBytesPerFont));
                m_fontSmall.trimCache(
                    std::min(smallEntries, kPressureEntriesPerFont),
                    std::min(smallBytes, kPressureBytesPerFont));
            } else {
                m_fontNormal.trimCache();
                m_fontSmall.trimCache();
            }
            if (textBytesBefore > 0)
                app().renderer().reclaimReleasedTextureSlotsAfterIdle();
            DebugLog::log(
                "[text-cache] maintenance done gpu=%llu/%llu text=%zu entries=%zu",
                static_cast<unsigned long long>(gpu.imageMemoryUsed()),
                static_cast<unsigned long long>(gpu.imageMemoryBudget()),
                m_fontNormal.cacheBytes() + m_fontSmall.cacheBytes(),
                m_fontNormal.cacheEntryCount() + m_fontSmall.cacheEntryCount());
        }
    }
#endif

    syncSteamGridDb();

    // Dynamic-line navigation owns a small internal focus manager. Depending
    // on the route transition, its selected icon can change without producing
    // another global focus callback. Synchronise the artwork from that source
    // of truth while HOME is active; showTitle() is a no-op when unchanged.
    if (m_navigator.route() == switchu::navigation::Route::Home
        && !(m_dialog && m_dialog->isActive())
        && !(m_settings && m_settings->isActive())
        && !(m_themeShop && m_themeShop->isActive())
        && !(m_gameOptions && m_gameOptions->isActive())
        && !(m_folderOptions && m_folderOptions->isActive())
        && !(m_userSelect && m_userSelect->isActive())) {
        showFocusedSteamGridDbArtwork();
    }

    if (m_folderCaptureReady)
        openCapturedFolder();

    if (m_config.actionHintStyle != "panel")
        syncHintCapsules(dt);

    if (m_grid) { // smooth animation of the page arrow center position
        const nxui::Rect gr = m_grid->rect();
        const float target = gr.y + gr.height * 0.5f;
        if (!m_arrowCenterInit) {
            m_arrowCenterInit = true;
            m_arrowCenterY.setImmediate(target);
        } else if (std::abs(m_arrowCenterY.target() - target) > 0.5f) {
            m_arrowCenterY.set(target, 0.28f, nxui::Easing::outCubic);
        }
    }

    {
        const bool paging = pagingAvailable();
        const int page = m_grid ? m_grid->currentPage() : 0;
        const int total = m_grid ? m_grid->totalPages() : 1;
        auto step = [dt](PageArrowAnim& a, bool visible) {
            const float d = dt / kPageArrowFade;
            a.show = std::clamp(a.show + (visible ? d : -d), 0.f, 1.f);
            a.press = std::max(0.f, a.press - dt / kPageArrowKick);
        };
        m_addPageMode = addPageAvailable();
        step(m_arrowAnimLeft, paging && page > 0);
        step(m_arrowAnimRight, (paging && page < total - 1) || m_addPageMode);

        if (m_addPageMode) {
            const bool holding = m_addPageTouchHold
                              || app().input().isHeld(nxui::Button::ZR);
            if (holding) {
                m_addPageHold = std::min(1.f, m_addPageHold + dt / kAddPageHoldDur);
                if (m_addPageHold >= 1.f) {
                    m_addPageHold = 0.f;
                    m_addPageTouchHold = false;
                    createFolderPage();
                }
            } else {
                m_addPageHold = std::max(0.f, m_addPageHold - dt / (kAddPageHoldDur * 0.4f));
            }
        } else {
            m_addPageHold = 0.f;
            m_addPageTouchHold = false;
        }
    }

    const bool sliding = m_grid && m_grid->isTransitioning();
    if (sliding != m_gridSliding) {
        m_gridSliding = sliding;
        if (sliding) {
            if (m_cursor) m_cursor->setVisible(false);
        } else {
            if (m_cursor && focusManager().current())
                m_cursor->moveTo(focusManager().current()->focusRect().expanded(4.f), 0.01f);
            updateCursor();
        }
    }
#ifdef SWITCHU_DEBUG_UI
    if (m_debugOverlay) {
        m_debugOverlay->setDeltaTime(dt);
    }
#endif

    syncSoftwareDeletion();

    if (!m_accessibilityReady && m_accessibilityFuture.valid() &&
        m_accessibilityFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            m_accessibilityFuture.get();
            m_accessibilityReady = true;
            DebugLog::log("[init] accessibility background task completed");
        } catch (const std::exception& ex) {
            DebugLog::log("[init] accessibility task failed: %s", ex.what());
        } catch (...) {
            DebugLog::log("[init] accessibility task failed: unknown exception");
        }
    }

    const int streamPage = m_grid && m_appLayoutMode == AppLayoutMode::DynamicLine
        ? std::max(0, m_grid->focusedGlobalIndex())
        : (m_grid ? m_grid->currentPage() : 0);
    const int streamPageSize = m_grid && m_appLayoutMode == AppLayoutMode::DynamicLine
        ? 1 : (m_grid ? m_grid->iconsPerPage() : 1);
    if (m_deferredInitialAssetFrames == 0 && m_grid && m_iconStreamer.needsVisibleLoads(
            streamPage, streamPageSize)) {
        m_iconStreamer.onPageChanged(streamPage, streamPageSize,
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }

    if (m_deferredProfileListFuture.valid() &&
        m_deferredProfileListFuture.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready) {
        try {
            m_deferredProfileListFuture.get();
            if (m_deferredProfileList) {
                m_pendingProfileUids = std::move(m_deferredProfileList->uids);
                m_pendingProfileIndex = 0;
                m_deferredProfileFrames = 1;
                DebugLog::log("[profiles] deferred accountListAllUsers rc=0x%X count=%d",
                              m_deferredProfileList->result,
                              static_cast<int>(m_pendingProfileUids.size()));
                if (m_pendingProfileUids.empty())
                    appendAddUserButton();
            }
        } catch (const std::exception& ex) {
            DebugLog::log("[profiles] deferred enumeration failed: %s", ex.what());
            appendAddUserButton();
        } catch (...) {
            DebugLog::log("[profiles] deferred enumeration failed: unknown exception");
            appendAddUserButton();
        }
        m_deferredProfileList.reset();
    }

    if (m_deferredProfileFrames > 0) {
        --m_deferredProfileFrames;
    } else if (m_pendingProfileIndex < m_pendingProfileUids.size()) {
        const bool visibleIconsBusy = m_grid && m_iconStreamer.needsVisibleLoads(
            streamPage, streamPageSize);
        if (!visibleIconsBusy)
            loadNextUserAvatar();
    }

    if (m_returnFadeTimer > 0.f)
        m_returnFadeTimer = std::max(0.f, m_returnFadeTimer - dt);
    if (m_tutorialStartupFadeTimer > 0.f)
        m_tutorialStartupFadeTimer = std::max(0.f, m_tutorialStartupFadeTimer - dt);
    if (m_tutorialStartupFadeDeadlineTick != 0 &&
        armGetSystemTick() >= m_tutorialStartupFadeDeadlineTick) {
        m_tutorialStartupFadeTimer = 0.f;
        m_tutorialStartupFadeDeadlineTick = 0;
    }

    if (m_launchAnim && m_launchAnim->isPlaying()) { // fade out music while the launch animation is playing
        const float t = m_launchAnim->musicFadeProgress();
        m_audio.setMusicFade(1.f - nxui::Easing::outQuad(t));
        m_musicFadeActive = true;
    } else if (m_musicFadeActive) { // cancel fade if the animation was interrupted
        m_musicFadeActive = false;
        m_audio.setMusicFade(1.f);
    }

    syncThemePackageTransfer();
    retryPendingBackgroundImage();

    if (m_deferredInitialAssetFrames > 0) {
        --m_deferredInitialAssetFrames;
        if (m_deferredInitialAssetFrames == 0) {
            DebugLog::log("[init] deferred initial icon/sidebar uploads start");
            if (m_deferredStaticTextures) {
                m_deferredStaticTextures = false;
                loadStaticTextures();
            }
            if (m_grid) {
                m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                             app().gpu(), app().renderer(),
                                             m_grid->allIcons());
            }
            m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS,
                                   resolveThemeAssetPath(m_effectivePreset,
                                                         m_effectivePreset.icons.basePath));
            DebugLog::log("[init] deferred initial icon/sidebar uploads done");
        }
    }

    if (m_audioInitPending) {
        if (m_launcher.appHasForeground()) {
            if (!m_audioHeldLogged) {
                m_audioHeldLogged = true;
                DebugLog::log("[audio] held back: an application still has foreground");
            }
        } else {
            m_audioInitPending = false;
            DebugLog::log("[audio] no foreground application; starting audio subsystem");
            m_audioFuture = m_threadPool.submit([this]() {
                m_audio.initialize();
                m_availablePresets = scanAvailablePresets();
                if (!isPackageSoundPreset(m_config.soundPreset) &&
                    m_config.soundPreset != kBuiltInSoundPreset) {
                    DebugLog::log("[audio] preset '%s' is no longer shipped, falling back to '%s'",
                                  m_config.soundPreset.c_str(), kBuiltInSoundPreset);
                    m_config.soundPreset = kBuiltInSoundPreset;
                }
                loadSoundPreset(resolveSoundPresetId(m_config.soundPreset));
            });
        }
    }

    if (!m_audioStarted && m_audioFuture.valid() &&
        m_audioFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_audioFuture.get();
        m_audio.setVolume(m_config.musicVolume);
        m_audio.setSfxVolume(m_config.sfxVolume);
        if (m_config.musicEnabled) m_audio.play();
        m_loadedSoundPreset = resolveSoundPresetId(m_config.soundPreset);
        m_audioStarted = true;
        DebugLog::log("[init] Audio ready (deferred)");
    }

    if (m_presetChangePending && m_audioFuture.valid() &&
        m_audioFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_audioFuture.get();
        m_audio.setVolume(m_config.musicVolume);
        m_audio.setSfxVolume(m_config.sfxVolume);
        if (m_config.musicEnabled)
            m_audio.play();
        m_loadedSoundPreset = m_pendingSoundPreset.empty() ? resolveSoundPresetId(m_config.soundPreset)
                                                           : m_pendingSoundPreset;
        m_pendingSoundPreset.clear();
        m_presetChangePending = false;
        DebugLog::log("[audio] Preset change complete: %s", m_config.soundPreset.c_str());
    }

    if (m_settingsNeedRefresh && m_settings) {
        m_settingsNeedRefresh = false;
        m_settings->refreshTranslations();
    }

    if (m_pendingNetConnect) {
        m_pendingNetConnect = false;
        m_launcher.launchNetConnect();
        return;
    }

#ifdef SWITCHU_MENU
    {
        AppletStorage notifySt;
        while (R_SUCCEEDED(appletPopInteractiveInData(&notifySt))) {
            switchu::smi::DaemonNotification notif{};
            s64 sz = 0;
            appletStorageGetSize(&notifySt, &sz);
            if (sz >= (s64)sizeof(notif))
                appletStorageRead(&notifySt, 0, &notif, sizeof(notif));
            appletStorageClose(&notifySt);

            if (notif.magic != switchu::smi::kNotifyMagic) continue;
            DebugLog::log("[notify] msg=%u", (unsigned)notif.msg);

            switch (notif.msg) {
            case switchu::smi::MenuMessage::HomeRequest:
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::ApplicationExited:
                m_launcher.setAppRunning(false);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(0);
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::ApplicationSuspended:
                m_launcher.setAppRunning(true);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(notif.app_id);
                m_sysMsg.pushAction(SysAction::HomeButton);
                break;
            case switchu::smi::MenuMessage::AppRecordsChanged:
            case switchu::smi::MenuMessage::GameCardMountFailure:
                m_refreshQueued = true;
                m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 3);
                break;
            case switchu::smi::MenuMessage::AppViewFlagsUpdate: {
                uint64_t tid = notif.app_id;
                uint32_t flags = notif.payload;
                m_model.updateViewFlags(tid, flags);
                for (auto& icon : m_grid->allIcons()) {
                    if (icon->titleId() == tid) {
                        bool launchable = (flags == 0) ||
                            (flags & switchu::ns::AppViewFlag_CanLaunch);
                        icon->setNotLaunchable(!launchable);
                        icon->setIsGameCard(
                            flags & switchu::ns::AppViewFlag_IsGameCard);
                        break;
                    }
                }
                break;
            }
            case switchu::smi::MenuMessage::BatteryStatusChanged:
                if (m_battery) {
                    const uint32_t percent = switchu::smi::batteryPayloadPercentage(notif.payload);
                    const bool charging = switchu::smi::batteryPayloadCharging(notif.payload);
                    m_consoleBatteryPercent = static_cast<int>(percent);
                    m_consoleBatteryCharging = charging;
                    m_battery->setBatteryStatus(percent, charging);
                    if (m_grid) {
                        for (const auto& icon : m_grid->allIcons()) {
                            if (icon && icon->entryKind() == GridEntryKind::Widget)
                                icon->setConsoleBattery(m_consoleBatteryPercent,
                                                        m_consoleBatteryCharging);
                        }
                    }
                    DebugLog::log("[battery] daemon status percent=%u charging=%d",
                                  (unsigned)percent,
                                  charging ? 1 : 0);
                }
                break;
            case switchu::smi::MenuMessage::WakeUp:
                m_clockService.invalidate();
                break;
            case switchu::smi::MenuMessage::OperationFailed:
                if (m_dialog) {
                    char message[96]{};
                    std::snprintf(message, sizeof(message),
                                  "%s (0x%08X)",
                                  nxui::I18n::instance().tr(
                                      "error.operation_failed", "Operation failed").c_str(),
                                  notif.payload);
                    m_dialogReturnFocus = focusManager().current();
                    m_dialog->show(
                        nxui::I18n::instance().tr(
                            "error.operation_failed", "Operation failed"),
                        message,
                        {{nxui::I18n::instance().tr("button.ok", "OK"),
                          []() {}, true}},
                        0, {});
                    focusManager().setFocus(m_dialog.get());
                }
                break;
            default:
                break;
            }
        }
    }
    m_sysMsg.pump();
    if (m_refreshCooldownFrames > 0)
        --m_refreshCooldownFrames;
    if (m_deferredRefreshFrames > 0)
        --m_deferredRefreshFrames;
    if (m_refreshQueued && m_deferredRefreshFrames == 0 &&
        !m_asyncRefreshPending && m_refreshCooldownFrames == 0) {
        DebugLog::log("[update] deferred refresh triggered, starting refreshAppList");
        refreshAppList();
    }
    if (m_asyncRefreshPending && m_appLoader.isReady()) {
        finalizeRefresh();
    }
#endif

    bool debugTouchBlocked = false;
#ifdef SWITCHU_DEBUG_UI
    debugTouchBlocked = m_showDebugOverlay;
#endif

    if (handleAccessibilityToggleCombo()) {
        m_plusExitPending = false;
        m_plusExitPendingTimer = 0.f;
    }

    if (!app().input().isDown(nxui::Button::Plus) || !app().input().isDown(nxui::Button::Minus))
        m_accessibilityToggleComboHeld = false;

#ifdef SWITCHU_MENU
    // Handle Plus directly from the frame input. The root action depended on
    // the focused icon still being attached through every transient grid
    // rebuild, which could make Plus silently disappear after a page/model
    // update even though the icon remained visibly selected.
    if (app().input().isDown(nxui::Button::Plus) &&
        !app().input().isDown(nxui::Button::Minus) &&
        m_navigator.route() == switchu::navigation::Route::Home &&
        !m_editMode &&
        !(m_contextMenu && m_contextMenu->isActive()) &&
        !(m_dialog && m_dialog->isActive()) &&
        !(m_settings && m_settings->isActive()) &&
        !(m_themeShop && m_themeShop->isActive()) &&
        !(m_gameOptions && m_gameOptions->isActive()) &&
        !(m_folderOptions && m_folderOptions->isActive()) &&
        !(m_controllerTest && m_controllerTest->isActive()) &&
        !(m_userSelect && m_userSelect->isActive())) {
        auto* current = focusManager().current();
        if (current && current->tag() == "glossy_icon" && m_grid) {
            auto* icon = static_cast<GlossyIcon*>(current);
            const auto& icons = m_grid->allIcons();
            const auto found = std::find_if(
                icons.begin(), icons.end(),
                [icon](const auto& candidate) { return candidate.get() == icon; });
            const int index = found == icons.end()
                ? -1 : static_cast<int>(std::distance(icons.begin(), found));
            if (index >= 0 && index < m_model.count()) {
                DebugLog::log("[plus] direct index=%d kind=%u title=0x%016lX",
                              index, static_cast<unsigned>(m_model.at(index).kind),
                              static_cast<unsigned long>(m_model.at(index).titleId));
                // titleId=0 is the canonical empty-slot marker. Keep this
                // defensive check even if a stale loader entry carries the
                // wrong kind so Plus can never route an empty slot as a game.
                if (m_model.at(index).titleId == 0 ||
                    m_model.at(index).kind == GridEntryKind::Empty) {
                    if (m_openFolderId == 0) // folders do not nest
                        showAddContextMenu(index, icon->focusRect());
                } else if (m_model.at(index).isFolder())
                    showFolderContextMenu(m_model.at(index).folderId);
                else if (m_model.at(index).isWidget())
                    showWidgetOptionsMenu(m_model.at(index).widgetId,
                                          index, icon->focusRect());
                else if (m_model.at(index).isApplication())
                    showGameContextMenu(icon);
            }
        }
    }
#endif

    if (app().input().isDown(nxui::Button::Minus) &&
        !app().input().isDown(nxui::Button::Plus) &&
        m_navigator.route() == switchu::navigation::Route::Home &&
        !m_editMode &&
        !(m_contextMenu && m_contextMenu->isActive()) &&
        !(m_dialog && m_dialog->isActive()) &&
        !(m_settings && m_settings->isActive()) &&
        !(m_themeShop && m_themeShop->isActive()) &&
        !(m_gameOptions && m_gameOptions->isActive()) &&
        !(m_folderOptions && m_folderOptions->isActive()) &&
        !(m_controllerTest && m_controllerTest->isActive()) &&
        !(m_userSelect && m_userSelect->isActive())) {
        toggleAppLayoutMode();
    }

    if (m_plusExitPending) {
        m_plusExitPendingTimer -= dt;
        if (m_plusExitPendingTimer <= 0.f) {
            m_plusExitPending = false;
            m_plusExitPendingTimer = 0.f;
#ifdef SWITCHU_HOMEBREW
            m_audio.playSfx(Sfx::ModalHide);
            app().requestExit();
#endif
        }
    }

    if (!debugTouchBlocked
        && !m_launchAnim->isPlaying()
        && !(m_contextMenu && m_contextMenu->isActive())
        && !(m_dialog && m_dialog->isActive())
        && !(m_themeShop && m_themeShop->isActive())
        && !(m_settings && m_settings->isActive())
        && !(m_gameOptions && m_gameOptions->isActive())
        && !(m_folderOptions && m_folderOptions->isActive())
        && !(m_controllerTest && m_controllerTest->isActive())
        && !(m_userSelect && m_userSelect->isActive()))
    {
        handleTouch();
    }

    bool dialogActiveNow = (m_dialog && m_dialog->isActive());
    if (!debugTouchBlocked && m_contextMenu && m_contextMenu->isActive())
        m_contextMenu->handleTouch(app().input());
    if (!debugTouchBlocked && dialogActiveNow)
        m_dialog->handleTouch(app().input());

    if (!debugTouchBlocked && m_themeShop && m_themeShop->isActive())
        m_themeShop->handleTouch(app().input());

    if (!debugTouchBlocked && m_settings && m_settings->isActive()
        && !(m_controllerTest && m_controllerTest->isActive())) {
        m_settings->handleTouch(app().input());
    }

    if (!debugTouchBlocked && m_gameOptions && m_gameOptions->isActive()
        && !(m_steamGridDbPicker && m_steamGridDbPicker->isActive()))
        m_gameOptions->handleTouch(app().input());

    if (!debugTouchBlocked && m_steamGridDbPicker && m_steamGridDbPicker->isActive())
        m_steamGridDbPicker->handleTouch(app().input());

    if (!debugTouchBlocked && m_folderOptions && m_folderOptions->isActive())
        m_folderOptions->handleTouch(app().input());

    if (!debugTouchBlocked && m_controllerTest && m_controllerTest->isActive())
        m_controllerTest->handleTouch(app().input());

    if (m_dialogWasActive && !dialogActiveNow) {
        if (isCurrentFocusableWidget(m_dialogReturnFocus)) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_dialogReturnFocus);
        }
        m_dialogReturnFocus = nullptr;
    }
    m_dialogWasActive = dialogActiveNow;

    if (!debugTouchBlocked && m_userSelect && m_userSelect->isActive())
        m_userSelect->handleTouch(app().input());

    if (!(m_userSelect && m_userSelect->isActive())
        && !(m_dialog && m_dialog->isActive())
        && !m_launchAnim->isPlaying())
    {
        auto* cur = focusManager().current();
        if (!cur || !cur->isFocusable()) {
            if (m_themeShop && m_themeShop->isActive()) {
                focusManager().setFocus(m_themeShop.get());
            } else if (m_controllerTest && m_controllerTest->isActive()) {
                focusManager().setFocus(m_controllerTest.get());
            } else if (m_gameOptions && m_gameOptions->isActive()) {
                focusManager().setFocus(m_gameOptions.get());
            } else if (m_folderOptions && m_folderOptions->isActive()) {
                focusManager().setFocus(m_folderOptions.get());
            } else if (m_settings && m_settings->isActive()) {
                focusManager().setFocus(m_settings.get());
            } else {
                auto* target = m_grid->focusManager().current();
                if (target)
                    focusManager().setFocus(target);
            }
        }
    }

    m_sidebar.update(dt, focusManager().current());

    if (m_pointerCursor) {
        bool showPointer = app().input().virtualPointerEnabled();
        m_pointerCursor->setVisible(showPointer);
        if (showPointer) {
            constexpr float kPointerSize = 30.f;
            float half = kPointerSize * 0.5f;
            nxui::Rect pointerRect {
                app().input().virtualPointerX() - half,
                app().input().virtualPointerY() - half,
                kPointerSize,
                kPointerSize,
            };
            m_pointerCursor->setOpacity(app().input().isTouching() ? 1.f : 0.92f);
            m_pointerCursor->moveTo(pointerRect, half, 0.06f);
        }
    }

    nxui::AnimationManager::instance().update(dt);

    // In dynamic-line mode the focused widget itself is moving. Sample its
    // interpolated display rectangle every frame so the focus ring remains
    // attached to the app throughout the carousel transition.
    if (m_grid && m_grid->isDynamicLine()) {
        auto* focused = focusManager().current();
        if (focused && focused->tag() == "glossy_icon")
            updateCursor();
    }

    // Sample the cursor after animation update to avoid one-frame lag.
    updateEditGhost(dt);

    if (m_editMode && m_editGhostIcon)
        m_editGhostIcon->update(dt);
}

std::vector<WiiUMenuApp::ActionHint> WiiUMenuApp::buildActionHints() {
    std::vector<ActionHint> hints;
    auto& i18n = nxui::I18n::instance();
    auto add = [&](const std::string& icon, const std::string& label) {
        if (!icon.empty() && !label.empty())
            hints.push_back({icon, label});
    };
    auto addVoiceControls = [&]() {
        if (!m_config.accessibilityEnabled)
            return;
        add(buttonGlyph(nxui::Button::L), i18n.tr("hint.repeat", "Repeat"));
        add(buttonGlyph(nxui::Button::Plus) + buttonGlyph(nxui::Button::Minus),
            i18n.tr("hint.voice", "Voice"));
    };

    if (m_launchAnim && m_launchAnim->isPlaying())
        return hints;

    if (m_contextMenu && m_contextMenu->isActive()) {
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        return hints;
    }

    if (m_dialog && m_dialog->isActive()) {
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.confirm", "Confirm"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        addVoiceControls();
        return hints;
    }

    if (m_userSelect && m_userSelect->isActive()) {
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        addVoiceControls();
        return hints;
    }

    if (m_steamGridDbPicker && m_steamGridDbPicker->isActive()) {
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        add(buttonGlyph(nxui::Button::X), i18n.tr("hint.search", "Search"));
        return hints;
    }

    if (m_themeShop && m_themeShop->isActive()) {
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        add(buttonGlyph(nxui::Button::X), i18n.tr("hint.search", "Search"));
        addVoiceControls();
        return hints;
    }

    if (m_controllerTest && m_controllerTest->isActive())
        return hints;

    if (m_gameOptions && m_gameOptions->isActive())
        return hints;

    if (m_folderOptions && m_folderOptions->isActive())
        return hints;

    if (m_settings && m_settings->isActive()) {
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        addVoiceControls();
        return hints;
    }

    if (m_editMode) {
        add(dpadGlyph(), i18n.tr("hint.move", "Move"));
        add(buttonGlyph(nxui::Button::A), i18n.tr("hint.place", "Place"));
        add(buttonGlyph(nxui::Button::B), m_openFolderId != 0
            ? i18n.tr("folder.leave_while_moving", "Leave folder")
            : i18n.tr("hint.cancel", "Cancel"));
        addVoiceControls();
        return hints;
    }

    if (m_openFolderId != 0)
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));

    nxui::Widget* cur = focusManager().current();
    if (cur && cur->tag() == "glossy_icon") {
        auto* icon = static_cast<GlossyIcon*>(cur);
        if (icon->titleId() != 0) {
            const int index = findTitleIndex(icon->titleId());
            const AppEntry* entry = index >= 0 ? &m_model.at(index) : nullptr;
            if (entry && entry->isFolder()) {
                add(buttonGlyph(nxui::Button::A), i18n.tr("folder.open", "Open"));
                add(buttonGlyph(nxui::Button::Plus), i18n.tr("hint.options", "Options"));
                if (m_openFolderId == 0)
                    add(buttonGlyph(nxui::Button::Y), i18n.tr("hint.move", "Move"));
            } else if (entry && entry->isWidget()) {
                add(buttonGlyph(nxui::Button::Plus), i18n.tr("hint.options", "Options"));
                if (m_openFolderId == 0)
                    add(buttonGlyph(nxui::Button::Y), i18n.tr("hint.move", "Move"));
            } else {
#ifdef SWITCHU_MENU
                add(buttonGlyph(nxui::Button::A),
                    m_launcher.isAppSuspended(icon->titleId())
                        ? i18n.tr("hint.resume", "Resume")
                        : i18n.tr("hint.launch", "Launch"));
                if (m_launcher.isAppSuspended(icon->titleId()))
                    add(buttonGlyph(nxui::Button::X), i18n.tr("hint.close", "Close"));
                add(buttonGlyph(nxui::Button::Plus), i18n.tr("hint.options", "Options"));
#else
                add(buttonGlyph(nxui::Button::A), i18n.tr("hint.open", "Open"));
#endif
                if (m_openFolderId == 0)
                    add(buttonGlyph(nxui::Button::Y), i18n.tr("hint.move", "Move"));
                else
                    add(buttonGlyph(nxui::Button::Y), i18n.tr("folder.move", "Move"));
            }
        } else if (m_openFolderId == 0) {
#ifdef SWITCHU_MENU
            add(buttonGlyph(nxui::Button::Plus), i18n.tr("add.title", "Add"));
#endif // in homebrew builds Plus quits the app, so no hint here
        }
    } else if (cur) {
        for (const auto& btn : m_sidebar.leftButtons()) {
            if (btn.get() == cur) {
                add(buttonGlyph(nxui::Button::A), btn->label());
                break;
            }
        }
        for (const auto& btn : m_sidebar.rightButtons()) {
            if (btn.get() == cur) {
                add(buttonGlyph(nxui::Button::A), btn->label());
                break;
            }
        }
        for (const auto& avatar : m_userAvatarButtons) {
            if (avatar.get() == cur) {
                add(buttonGlyph(nxui::Button::A), i18n.tr("hint.profile", "Profile"));
                break;
            }
        }
    }

    if (m_navigator.route() == switchu::navigation::Route::Home && !m_editMode) {
        add(buttonGlyph(nxui::Button::Minus), i18n.tr("hint.switch_layout", "Switch view"));
    }

    // Paging lives on the arrows flanking the grid, not in the capsules.
    addVoiceControls();

    return hints;
}

float WiiUMenuApp::hintCapsuleWidth(const std::string& icon, const std::string& label) {
    return kHintCapPadX * 2.f
         + m_fontIcons.measure(icon).x * kHintIconScale
         + kHintIconGap
         + m_fontSmall.measure(label).x * kHintTextScale;
}

void WiiUMenuApp::syncHintCapsules(float dt) {
    std::vector<ActionHint> hints = buildActionHints();
    if ((int)hints.size() > kHintMaxItems)
        hints.resize((size_t)kHintMaxItems);

    bool sameBindings = hints.size() == m_hintCapsules.size();
    for (size_t i = 0; sameBindings && i < hints.size(); ++i)
        sameBindings = (hints[i].icon == m_hintCapsules[i].icon);

    if (!sameBindings) {
        m_hintCapsules.clear();
        m_hintCapsules.reserve(hints.size());
        for (const auto& h : hints) {
            HintCapsule c;
            c.icon = h.icon;
            c.label = h.label;
            c.width = c.widthFrom = c.widthTo = hintCapsuleWidth(h.icon, h.label);
            m_hintCapsules.push_back(std::move(c));
        }
        if (!hints.empty()) {
            if (!m_hintCapsulesInitialized) {
                m_hintCapsulesInitialized = true;
                m_hintContentReveal.setImmediate(1.f);
            } else {
                m_hintContentReveal.setImmediate(0.45f);
                m_hintContentReveal.set(1.f, 0.18f, nxui::Easing::outCubic);
            }
        }
    } else { // if the button key is the same, just change string
        for (size_t i = 0; i < hints.size(); ++i) {
            HintCapsule& c = m_hintCapsules[i];
            if (c.label == hints[i].label)
                continue;
            c.outgoing  = c.label;
            c.label     = hints[i].label;
            c.swapT     = 0.f;
            c.widthFrom = c.width;
            c.widthTo   = hintCapsuleWidth(c.icon, c.label);
            c.widthT    = 0.f;
        }
    }

    for (HintCapsule& c : m_hintCapsules) {
        if (c.widthT < 1.f) {
            c.widthT = std::min(1.f, c.widthT + dt / kHintWidthDur);
            c.width  = c.widthFrom + (c.widthTo - c.widthFrom) * nxui::Easing::outCubic(c.widthT);
        }
        if (c.swapT < 1.f) {
            c.swapT = std::min(1.f, c.swapT + dt / kHintSwapDur);
            if (c.swapT >= 1.f)
                c.outgoing.clear();
        }
    }
}

void WiiUMenuApp::renderActionHintBar(nxui::Renderer& ren) {
    const int count = (int)m_hintCapsules.size();
    if (count <= 0)
        return;

    std::vector<std::pair<int, int>> rows;
    for (int i = 0; i < count;) {
        float w = 0.f;
        int j = i;
        while (j < count) {
            const float add = m_hintCapsules[(size_t)j].width + (j > i ? kHintCapGap : 0.f);
            if (j > i && w + add > kHintRowMaxW) break;
            w += add;
            ++j;
        }
        rows.emplace_back(i, j);
        i = j;
    }

    const float reveal = std::clamp(m_hintContentReveal.value(), 0.f, 1.f);
    const float blockH = rows.size() * kHintCapH + (rows.size() - 1) * kHintRowGap;
    float y = 720.f - kHintEdgeY - blockH + (1.f - reveal) * 4.f;

    const nxui::Color tint = m_theme.panelBase.withAlpha(
        m_theme.mode == nxui::ThemeMode::Dark ? 0.30f : 0.24f);

    for (const auto& [first, last] : rows) {
        float rowW = 0.f;
        for (int i = first; i < last; ++i)
            rowW += m_hintCapsules[(size_t)i].width + (i > first ? kHintCapGap : 0.f);

        float x = 1280.f - kHintEdgeX - rowW;
        for (int i = first; i < last; ++i) {
            const HintCapsule& c = m_hintCapsules[(size_t)i];
            const nxui::Rect cap = {x, y, c.width, kHintCapH};
            const float radius = kHintCapH * 0.5f;

            ren.drawRoundedRect({cap.x, cap.y + 3.f, cap.width, cap.height},
                                nxui::Color(0.f, 0.f, 0.f, 0.14f * reveal), radius);
            ren.drawFrostedInset(cap, tint.withAlpha(tint.a * reveal),
                                 m_theme.panelBorder.withAlpha(0.24f * reveal),
                                 m_theme.panelHighlight.withAlpha(0.08f * reveal),
                                 radius, 0.86f);

            const nxui::Vec2 iconSize = m_fontIcons.measure(c.icon);
            const float iconW = iconSize.x * kHintIconScale;
            ren.drawText(c.icon,
                         {cap.x + kHintCapPadX,
                          cap.y + (kHintCapH - iconSize.y * kHintIconScale) * 0.5f},
                         &m_fontIcons, m_theme.textPrimary.withAlpha(0.94f * reveal), kHintIconScale);

            const float textX = cap.x + kHintCapPadX + iconW + kHintIconGap;
            const float swap  = nxui::Easing::outCubic(std::clamp(c.swapT, 0.f, 1.f));

            ren.pushClipRect(cap);
            if (!c.outgoing.empty()) {
                const nxui::Vec2 os = m_fontSmall.measure(c.outgoing);
                ren.drawText(c.outgoing,
                             {textX - 7.f * swap,
                              cap.y + (kHintCapH - os.y * kHintTextScale) * 0.5f},
                             &m_fontSmall,
                             m_theme.textSecondary.withAlpha(0.90f * reveal * (1.f - swap)),
                             kHintTextScale);
            }
            const nxui::Vec2 ls = m_fontSmall.measure(c.label);
            ren.drawText(c.label,
                         {textX + 7.f * (1.f - swap),
                          cap.y + (kHintCapH - ls.y * kHintTextScale) * 0.5f},
                         &m_fontSmall,
                         m_theme.textSecondary.withAlpha(0.90f * reveal * swap),
                         kHintTextScale);
            ren.popClipRect();

            x += c.width + kHintCapGap;
        }
        y += kHintCapH + kHintRowGap;
    }
}

void WiiUMenuApp::renderActionHintPanel(nxui::Renderer& ren) {
    std::vector<ActionHint> hints = buildActionHints();
    if (hints.empty())
        return;

    constexpr float kIconScale = 0.66f;
    constexpr float kTextScale = 0.54f;
    constexpr float kRowH = 22.f;
    constexpr float kRowGap = 3.f;
    constexpr float kPadX = 10.f;
    constexpr float kPadY = 8.f;
    constexpr float kIconTextGap = 6.f;
    constexpr float kScreenMargin = 18.f;

    const int count = std::min((int)hints.size(), kHintMaxItems);
    float contentW = 0.f;
    std::string signature;
    for (int i = 0; i < count; ++i) {
        const ActionHint& hint = hints[(size_t)i];
        const nxui::Vec2 iconSize = m_fontIcons.measure(hint.icon);
        const nxui::Vec2 labelSize = m_fontSmall.measure(hint.label);
        contentW = std::max(contentW,
                            iconSize.x * kIconScale + kIconTextGap + labelSize.x * kTextScale);
        signature += hint.icon;
        signature += '\n';
        signature += hint.label;
        signature += '\n';
    }

    float panelW = std::clamp(contentW + kPadX * 2.f, 104.f, 210.f);
    float panelH = kPadY * 2.f + count * kRowH + (count - 1) * kRowGap;
    if (!m_hintPanelInitialized) {
        m_hintPanelInitialized = true;
        m_hintPanelW.setImmediate(panelW);
        m_hintPanelH.setImmediate(panelH);
        m_hintContentReveal.setImmediate(1.f);
        m_hintSignature = signature;
    } else {
        if (std::abs(m_hintPanelW.target() - panelW) > 0.5f)
            m_hintPanelW.set(panelW, 0.20f, nxui::Easing::outCubic);
        if (std::abs(m_hintPanelH.target() - panelH) > 0.5f)
            m_hintPanelH.set(panelH, 0.20f, nxui::Easing::outCubic);
        if (m_hintSignature != signature) {
            m_hintSignature = signature;
            m_hintContentReveal.setImmediate(0.45f);
            m_hintContentReveal.set(1.f, 0.18f, nxui::Easing::outCubic);
        }
    }

    panelW = std::max(1.f, m_hintPanelW.value());
    panelH = std::max(1.f, m_hintPanelH.value());
    const nxui::Rect panel = {
        1280.f - kScreenMargin - panelW,
        720.f - kScreenMargin - panelH,
        panelW,
        panelH
    };
    constexpr float kRadius = 16.f;
    const float reveal = std::clamp(m_hintContentReveal.value(), 0.f, 1.f);

    ren.drawRoundedRect({panel.x, panel.y + 4.f, panel.width, panel.height},
                        nxui::Color(0.f, 0.f, 0.f, 0.12f), kRadius);
    const nxui::Color tint = m_theme.panelBase.withAlpha(
        m_theme.mode == nxui::ThemeMode::Dark ? 0.22f : 0.18f);
    ren.drawFrostedInset(panel, tint,
                        m_theme.panelBorder.withAlpha(0.26f),
                        m_theme.panelHighlight.withAlpha(0.09f),
                        kRadius, 0.86f);

    ren.pushClipRect(panel.shrunk(3.f));
    float y = panel.y + kPadY + (1.f - reveal) * 4.f;
    for (int i = 0; i < count; ++i) {
        const ActionHint& hint = hints[(size_t)i];
        const nxui::Vec2 iconSize = m_fontIcons.measure(hint.icon);
        const nxui::Vec2 labelSize = m_fontSmall.measure(hint.label);
        const float iconX = panel.x + kPadX;
        const float iconY = y + (kRowH - iconSize.y * kIconScale) * 0.5f;
        const float labelX = iconX + iconSize.x * kIconScale + kIconTextGap;
        const float labelY = y + (kRowH - labelSize.y * kTextScale) * 0.5f;

        ren.drawText(hint.icon, {iconX, iconY}, &m_fontIcons,
                     m_theme.textPrimary.withAlpha(0.88f * reveal), kIconScale);
        ren.drawText(hint.label, {labelX, labelY}, &m_fontSmall,
                     m_theme.textSecondary.withAlpha(0.82f * reveal), kTextScale);
        y += kRowH + kRowGap;
    }
    ren.popClipRect();
}

bool WiiUMenuApp::pagingAvailable() {
    if (m_appLayoutMode == AppLayoutMode::DynamicLine)
        return false;
    return m_navigator.route() == switchu::navigation::Route::Home
        && focusRoot() == &rootBox()
        && m_grid && m_grid->totalPages() > 1;
}

nxui::Rect WiiUMenuApp::pageArrowRect(bool left) {
    const float cx = left ? kPageArrowInset : 1280.f - kPageArrowInset;
    const float cy = m_arrowCenterY.value();
    return {cx - kPageArrowW * 0.5f, cy - kPageArrowH * 0.5f, kPageArrowW, kPageArrowH};
}

void WiiUMenuApp::kickPageArrow(int dir) {
    (dir < 0 ? m_arrowAnimLeft : m_arrowAnimRight).press = 1.f;
}

bool WiiUMenuApp::addPageAvailable() {
    if (m_appLayoutMode == AppLayoutMode::DynamicLine)
        return false;
    if (m_openFolderId == 0 || !m_grid || m_editMode)
        return false;
    if (m_navigator.route() != switchu::navigation::Route::Home || focusRoot() != &rootBox())
        return false;
    const auto* folder = m_folderStore.find(m_openFolderId);
    if (!folder || folder->pageCount >= switchu::folders::kMaxFolderPages)
        return false;
    return m_grid->currentPage() >= m_grid->totalPages() - 1;
}

void WiiUMenuApp::createFolderPage() {
    if (m_openFolderId == 0 || !m_grid)
        return;
    const auto* folder = m_folderStore.find(m_openFolderId);
    if (!folder)
        return;

    const auto [cols, rows] = folderGridDimensions(m_openFolderId);
    const int perPage = std::max(1, cols * rows);
    const int pages = std::max(folder->pageCount, m_grid->totalPages());
    if (pages >= switchu::folders::kMaxFolderPages)
        return;
    if (!m_folderStore.setPageCount(m_openFolderId, pages + 1))
        return;
    if (!saveFoldersOrReport("add_folder_page"))
        return;

    applyDisplayModel(buildOpenFolderModel(m_openFolderId), 0, false);
    syncPageIndicator();

    const int target = pages;
    m_grid->setPage(target - 1);
    m_grid->startPageTransition(target);
    if (m_grid->focusGlobalIndex(target * perPage)) {
        if (auto* cur = m_grid->focusManager().current())
            focusManager().setFocus(cur);
    }
    kickPageArrow(+1);
    m_audio.playSfx(Sfx::ConfirmPositive);
    m_accessibility.announce(nxui::I18n::instance().tr(
        "folder.page_added", "Page added"), true, true);
    updateCursor();
}

bool WiiUMenuApp::flipPage(int dir) {
    if (!m_grid || m_grid->isTransitioning())
        return false;
    const int p = m_grid->currentPage() + dir;
    if (p < 0 || p >= m_grid->totalPages())
        return false;
    if (m_editMode && m_editTargetIndex >= 0) {
        const int perPage = std::max(1, m_grid->iconsPerPage());
        m_editTargetIndex = p * perPage + (m_editTargetIndex % perPage);
        if (m_editTargetIndex >= m_model.count())
            m_editTargetIndex = std::max(0, m_model.count() - 1);
    }
    m_grid->startPageTransition(p);
    m_audio.playSfx(Sfx::PageChange);
    kickPageArrow(dir);
    return true;
}

void WiiUMenuApp::renderPageArrows(nxui::Renderer& ren) {
    constexpr float kGlyphScale = 0.70f;

    auto drawArrow = [&](bool left, const nxui::Texture& tex,
                         const PageArrowAnim& anim, const std::string& glyph,
                         bool plus) {
        if (anim.show <= 0.002f || (!plus && !tex.valid()))
            return;

        const float e = anim.show * anim.show * (3.f - 2.f * anim.show);
        const float bump = anim.press * anim.press;
        const nxui::Rect base = pageArrowRect(left);
        const float outward = (left ? -1.f : 1.f) * ((1.f - e) * 16.f + bump * 9.f);
        const float grow = plus ? 0.10f * m_addPageHold : 0.f;
        const float scale = (0.86f + 0.14f * e) * (1.f + 0.18f * bump + grow);

        const float cx = base.x + base.width * 0.5f + outward;
        const float cy = base.y + base.height * 0.5f;
        const float w = base.width * scale;
        const float h = base.height * scale;

        if (plus) {
            const float ring = std::min(w, h) * 0.40f;
            ren.drawCircle({cx, cy + 2.f}, ring,
                           nxui::Color(0.02f, 0.04f, 0.06f, 0.32f * e), 28);
            ren.drawCircle({cx, cy}, ring,
                           m_theme.panelBase.withAlpha(0.88f * e), 28);
            ren.drawCircle({cx, cy}, ring - 1.6f,
                           m_theme.panelHighlight.withAlpha(0.10f * e), 28);

            const float bar = ring * 0.92f;
            const float thick = std::max(2.f, ring * 0.17f);
            const nxui::Color ink = m_theme.textPrimary.withAlpha(0.92f * e);
            ren.drawRoundedRect({cx - bar * 0.5f, cy - thick * 0.5f, bar, thick},
                                ink, thick * 0.5f);
            ren.drawRoundedRect({cx - thick * 0.5f, cy - bar * 0.5f, thick, bar},
                                ink, thick * 0.5f);

            if (m_addPageHold > 0.002f) {
                constexpr int kSegments = 44;
                const float rr = ring + 3.5f;
                const int lit = std::max(1, (int)std::ceil(kSegments * m_addPageHold));
                const nxui::Color arc = m_theme.cursorNormal.withAlpha(0.95f * e);
                for (int i = 0; i < lit; ++i) {
                    const float a0 = -1.5707963f + 6.2831853f * (float)i / kSegments;
                    const float a1 = -1.5707963f + 6.2831853f * (float)(i + 1) / kSegments;
                    ren.drawLine({cx + std::cos(a0) * rr, cy + std::sin(a0) * rr},
                                 {cx + std::cos(a1) * rr, cy + std::sin(a1) * rr},
                                 arc, 3.f);
                }
            }
        } else {
            ren.drawTexture(&tex, {cx - w * 0.5f, cy - h * 0.5f, w, h},
                            nxui::Color(1.f, 1.f, 1.f, e));
        }

        const nxui::Vec2 gs = m_fontIcons.measure(glyph);
        ren.drawText(glyph, {cx - gs.x * kGlyphScale * 0.5f, cy + h * 0.5f + 6.f},
                     &m_fontIcons, m_theme.textPrimary.withAlpha(0.9f * e), kGlyphScale);
    };

    drawArrow(true, m_arrowTexLeft, m_arrowAnimLeft, buttonGlyph(nxui::Button::ZL), false);
    drawArrow(false, m_arrowTexRight, m_arrowAnimRight, buttonGlyph(nxui::Button::ZR),
              m_addPageMode);
}

void WiiUMenuApp::onRender(nxui::Renderer& ren) {
    if (m_folderCaptureRequested) {
        if (ren.gpu().offscreenReady()) {
            // Cache one complete HOME frame, then use the fork's compact,
            // overlapping blur kernel. Keeping the sample spread below 2.5
            // avoids the visible square lattice produced by wide taps.
            ren.captureToOffscreen(false);
            ren.applyBlur(4.f, 2);
            ren.copyOffscreen(0, 2);
        }
        m_folderCaptureRequested = false;
        m_folderCaptureReady = true;
    }
    if (m_fastReturnStartupTick != 0) {
        const auto elapsedMs = static_cast<unsigned long>(
            armTicksToNs(armGetSystemTick() - m_fastReturnStartupTick) / 1'000'000ULL);
        DebugLog::log("[first-frame] fast HOME return rendered in %lums", elapsedMs);
        m_fastReturnStartupTick = 0;
    }
    if (m_returnFadeTimer > 0.f) {
        float alpha = m_returnFadeTimer / kReturnFadeInDur;
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, alpha));
    }
    if (m_tutorialStartupFadeTimer > 0.f &&
        (m_tutorialStartupFadeDeadlineTick == 0 ||
         armGetSystemTick() < m_tutorialStartupFadeDeadlineTick)) {
        float t = std::clamp(m_tutorialStartupFadeTimer / kTutorialStartupFadeDur, 0.f, 1.f);
        float alpha = nxui::Easing::outCubic(t);
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(1.f, 1.f, 1.f, alpha));
    } else if (m_tutorialStartupFadeTimer > 0.f) {
        m_tutorialStartupFadeTimer = 0.f;
        m_tutorialStartupFadeDeadlineTick = 0;
    }

    if (m_touchHitIndex >= 0 && !m_touchOnFocused && app().input().isTouching()) {
        auto icons = m_grid->pageIcons();
        if (m_touchHitIndex < (int)icons.size()) {
            nxui::Rect r = icons[m_touchHitIndex]->focusRect();
            float cr = icons[m_touchHitIndex]->cornerRadius();
            ren.drawRoundedRect(r, nxui::Color(1.f, 1.f, 1.f, 0.18f), cr);
        }
    }

    syncPageIndicator();

    if (m_themeRenderDebugFrames > 0) {
        nxui::Widget* focus = focusManager().current();
        nxui::Widget* focusParent = focus ? focus->parent() : nullptr;
        std::vector<GlossyIcon*> pageIcons = m_grid ? m_grid->pageIcons() : std::vector<GlossyIcon*>();
        GlossyIcon* firstPageIcon = pageIcons.empty() ? nullptr : pageIcons.front();
        const nxui::Texture* firstTexture = firstPageIcon ? firstPageIcon->texture() : nullptr;

        DebugLog::log("[theme-render] preset=%s focus=%s parent=%s rootChildren=%zu contentChildren=%zu overlayChildren=%zu grid(all=%zu page=%zu vis=%d op=%.2f firstTex=%d firstIconVis=%d firstIconOp=%.2f) settings(active=%d vis=%d op=%.2f) themeshop(active=%d vis=%d op=%.2f)",
                      m_activePresetName.c_str(),
                      safeTag(focus),
                      safeTag(focusParent),
                      rootBox().children().size(),
                      m_contentLayer ? m_contentLayer->children().size() : 0,
                      m_overlayLayer ? m_overlayLayer->children().size() : 0,
                      m_grid ? m_grid->allIcons().size() : 0,
                      pageIcons.size(),
                      m_grid && m_grid->isVisible() ? 1 : 0,
                      m_grid ? m_grid->opacity() : 0.f,
                      (firstTexture && firstTexture->valid()) ? 1 : 0,
                      (firstPageIcon && firstPageIcon->isVisible()) ? 1 : 0,
                      firstPageIcon ? firstPageIcon->opacity() : 0.f,
                      (m_settings && m_settings->isActive()) ? 1 : 0,
                      (m_settings && m_settings->isVisible()) ? 1 : 0,
                      m_settings ? m_settings->opacity() : 0.f,
                      (m_themeShop && m_themeShop->isActive()) ? 1 : 0,
                      (m_themeShop && m_themeShop->isVisible()) ? 1 : 0,
                      m_themeShop ? m_themeShop->opacity() : 0.f);
        --m_themeRenderDebugFrames;
    }

    // Final topmost pass for move-mode ghost.
    if (m_editMode && m_editGhostIcon)
        m_editGhostIcon->render(ren);

    renderPageArrows(ren);
    if (m_config.actionHintStyle == "panel")
        renderActionHintPanel(ren);
    else
        renderActionHintBar(ren);

#ifdef SWITCHU_DEBUG_UI
    if (m_debugOverlay) {
        m_debugOverlay->render(ren, app().input(), m_showDebugOverlay);
    }
#endif
}
