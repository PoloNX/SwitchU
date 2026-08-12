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
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <system_error>
#include <tuple>

namespace {

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
           a.folderColorIndex == b.folderColorIndex;
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
    loadMenuLayout();
    if (!m_folderStore.load())
        DebugLog::log("[folders] store unavailable; continuing with an empty folder list");
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

    m_audioFuture = m_threadPool.submit([this]() {
        m_audio.initialize();
        m_availablePresets = scanAvailablePresets();
        if (!isPackageSoundPreset(m_config.soundPreset) && m_config.soundPreset != kBuiltInSoundPreset) {
            DebugLog::log("[audio] preset '%s' is no longer shipped, falling back to '%s'",
                          m_config.soundPreset.c_str(),
                          kBuiltInSoundPreset);
            m_config.soundPreset = kBuiltInSoundPreset;
        }
        loadSoundPreset(resolveSoundPresetId(m_config.soundPreset));
    });
    DebugLog::log("[init] Audio loading started on background thread");

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
    std::string fontPath = std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf";
    if (m_fontNormal.load(app().gpu(), app().renderer(), fontPath, 24))
        m_loadedRegularFontPath = fontPath;
    if (m_fontSmall.load(app().gpu(), app().renderer(), fontPath, 18))
        m_loadedSmallFontPath = fontPath;
    m_fontIcons.load(app().gpu(), app().renderer(), std::string(SD_ASSETS) + "/fonts/switch_icons.ttf", 24);

    std::string gameCardPath = std::string(SD_ASSETS) + "/icons/gamecard.png";
    if (m_gameCardTex.loadFromFile(app().gpu(), app().renderer(), gameCardPath))
        m_loadedGameCardPath = gameCardPath;

    m_arrowTexLeft.loadFromFile(app().gpu(), app().renderer(),
                                std::string(SD_ASSETS) + "/icons/page_arrow_left.png");
    m_arrowTexRight.loadFromFile(app().gpu(), app().renderer(),
                                 std::string(SD_ASSETS) + "/icons/page_arrow_right.png");

    m_appLoader.load(m_model, m_iconStreamer);
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

    AccountUid uids[8] = {};
    s32 count = 0;
    Result rc = accountListAllUsers(uids, 8, &count);
    DebugLog::log("[profiles] accountListAllUsers rc=0x%X count=%d", rc, count);
    m_pendingProfileUids.clear();
    m_pendingProfileIndex = 0;
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
    for (std::size_t i = 0; i < m_userAvatarButtons.size(); ++i) {
        auto* current = m_userAvatarButtons[i].get();
        auto* left = i > 0 ? m_userAvatarButtons[i - 1].get() : current;
        auto* right = i + 1 < m_userAvatarButtons.size()
            ? m_userAvatarButtons[i + 1].get()
            : current;
        current->setCustomNavigation(nxui::FocusDirection::LEFT, left);
        current->setCustomNavigation(nxui::FocusDirection::RIGHT, right);
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

    std::ifstream f(kLayoutPath);
    if (!f.is_open())
        return;

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return;
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

    std::ofstream f(kLayoutPath, std::ios::trunc);
    if (!f.is_open())
        return;
    f << j.dump(2);
    m_layoutDirty = false;
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
        entry.titleId = pending.titleId;
        entry.viewFlags = pending.viewFlags;
        entry.userRequired = pending.userRequired;
        entry.startupUserKnown = pending.startupUserKnown;
        entry.startupUserAccount = pending.startupUserAccount;
        entry.startupUserAccountOption = pending.startupUserAccountOption;
        entry.kind = GridEntryKind::Application;
        m_allApps.push_back(std::move(entry));
    }

    std::unordered_map<uint64_t, PendingApp> byId;
    byId.reserve(apps.size() + m_folderStore.all().size());
    std::vector<uint64_t> itemOrder;
    itemOrder.reserve(apps.size() + m_folderStore.all().size());
    for (auto& app : apps) {
        if (app.titleId != 0 && m_folderStore.folderForTitle(app.titleId) == 0) {
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
        item.folderPreviewCount = static_cast<int>(folder.titleIds.size());
        item.folderColorIndex = folder.colorIndex;
        itemOrder.push_back(item.titleId);
        byId.emplace(item.titleId, std::move(item));
    }

    std::vector<uint64_t> slots = m_layoutSlots;
    if (slots.empty()) {
        slots = itemOrder;
    }

    std::unordered_set<uint64_t> placed;
    placed.reserve(byId.size());
    for (auto& slotTid : slots) {
        if (slotTid == 0)
            continue;
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

    std::vector<PendingApp> ordered;
    ordered.reserve(slots.size());
    for (uint64_t tid : slots) {
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
    GridModel model;
    std::unordered_map<std::uint64_t, AppEntry> entries;
    for (const auto& app : m_allApps) {
        if (m_folderStore.folderForTitle(app.titleId) == 0)
            entries.emplace(app.titleId, app);
    }
    for (const auto& folder : m_folderStore.all()) {
        AppEntry entry;
        entry.id = "folder:" + std::to_string(folder.id);
        entry.title = folder.name;
        entry.titleId = folderTitleId(folder.id);
        entry.kind = GridEntryKind::Folder;
        entry.folderId = folder.id;
        entry.folderPreviewCount = static_cast<int>(folder.titleIds.size());
        entry.folderColorIndex = folder.colorIndex;
        entries.emplace(entry.titleId, std::move(entry));
    }

    const int perPage = std::max(1, std::clamp(m_config.gridColumns, 3, 8) *
                                     std::clamp(m_config.gridRows, 2, 5));
    if (m_layoutSlots.empty()) {
        for (const auto& app : m_allApps)
            if (entries.count(app.titleId)) m_layoutSlots.push_back(app.titleId);
        for (const auto& folder : m_folderStore.all())
            m_layoutSlots.push_back(folderTitleId(folder.id));
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

    for (auto titleId : m_layoutSlots) {
        auto found = entries.find(titleId);
        if (found != entries.end()) {
            model.addEntry(found->second);
        } else {
            model.addEntry({});
        }
    }
    return model;
}

GridModel WiiUMenuApp::buildOpenFolderModel(std::uint32_t folderId) const {
    GridModel model;
    const auto* folder = m_folderStore.find(folderId);
    if (!folder)
        return model;
    for (std::uint64_t titleId : folder->titleIds) {
        auto found = std::find_if(m_allApps.begin(), m_allApps.end(),
            [titleId](const AppEntry& app) { return app.titleId == titleId; });
        if (found != m_allApps.end())
            model.addEntry(*found);
        else
            DebugLog::log("[folders] missing title ignored folder=%u tid=%016lX",
                          folderId, static_cast<unsigned long>(titleId));
    }
    const auto [folderCols, folderRows] = folderGridDimensions(folderId);
    const int perPage = std::max(1, folderCols * folderRows);
    // A folder always offers a second page, and a spare one once the last fills up.
    const int count = std::max(2 * perPage, (model.count() / perPage + 1) * perPage);
    while (model.count() < count)
        model.addEntry({});
    return model;
}

void WiiUMenuApp::applyDisplayModel(GridModel model, std::uint64_t focusId, bool animate) {
    if (!m_grid)
        return;
    m_model = std::move(model);
    std::vector<std::shared_ptr<GlossyIcon>> icons;
    std::vector<std::uint64_t> titleIds;
    icons.reserve(m_model.count());
    titleIds.reserve(m_model.count());
    for (int i = 0; i < m_model.count(); ++i) {
        auto icon = makeIcon(m_model.at(i));
        icon->setBaseColor(m_theme.iconDefault);
        icons.push_back(std::move(icon));
        titleIds.push_back(m_model.at(i).isApplication() ? m_model.at(i).titleId : 0);
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
    m_grid->setup(std::move(icons), columns, rows, metrics.cellW, metrics.cellH,
                  metrics.padX, metrics.padY);
    wireFocusCallback();
    m_grid->onEdgePage([this](int dir) { flipPageFromEdge(dir); });
    m_grid->onPageSwitched([this]() {
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
    if (animate) m_grid->startAppearAnimation();
    else for (auto& icon : m_grid->allIcons()) icon->forceVisible();
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

void WiiUMenuApp::requestOpenFolder(std::uint32_t folderId) {
    if (!m_folderStore.find(folderId) || m_folderCaptureRequested) return;
    m_requestedFolderId = folderId;
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
    const int total = m_grid->totalPages();
    m_pageIndicator->setVisible(total > 1); // a lone page would draw an empty pill
    m_pageIndicator->setPageCount(total);
    m_pageIndicator->setCurrentPage(m_grid->currentPage());
}

void WiiUMenuApp::openCapturedFolder() {
    const auto* folder = m_folderStore.find(m_requestedFolderId);
    if (!folder) return;
    m_openFolderId = m_requestedFolderId;
    m_requestedFolderId = 0;
    m_folderCaptureReady = false;
    if (m_folderBackdrop) m_folderBackdrop->show();
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
    applyDisplayModel(buildOpenFolderModel(m_openFolderId), 0, false);
    syncPageIndicator();
    if (m_editMode)
        reattachEditSourceIcon();
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
        reattachEditSourceIcon();
        m_titlePill->setText(nxui::I18n::instance().tr("game.move_prefix", "Move: ") + m_editHeldTitle);
        m_titlePill->setVisible(true);
    }
    m_audio.playSfx(Sfx::ModalHide);
}

std::shared_ptr<GlossyIcon> WiiUMenuApp::makeIcon(const AppEntry& entry) {
    auto icon = std::make_shared<GlossyIcon>();
    icon->setEntryKind(entry.kind);
    icon->setFolderPreviewCount(entry.folderPreviewCount);
    icon->setFolderVisualSeed(entry.folderId);
    icon->setFolderColorIndex(entry.folderColorIndex);
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
            "Plus creates a folder. A places software being moved out of a folder."));
        icon->setNotLaunchable(false);
        icon->setCornerRadius(m_theme.iconCornerRadius);
        icon->setPanelOpacity(0.96f);
        return icon;
    }

    if (entry.isFolder()) {
        icon->setTag("glossy_icon");
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
                [this]() { m_launcher.resumeApplication(); });
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
            auto startLaunch = [this, fr, tex, cr, base, bord, tid](AccountUid uid) {
                m_audio.playSfx(Sfx::LaunchGame);
                m_launchAnim->start(fr, tex, cr, base, bord, tid, uid,
                    [this](uint64_t id, AccountUid u) { m_launcher.launchApplication(id, u); });
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

    std::vector<std::shared_ptr<GlossyIcon>> icons;
    for (int i = 0; i < m_model.count(); ++i)
        icons.push_back(makeIcon(m_model.at(i)));

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();

    m_grid = std::make_shared<IconGrid>();
    m_grid->setRect({kGridRectX, kGridRectY, kGridRectW, kGridRectH});
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

    m_progressDialog = std::make_shared<ProgressDialog>();
    m_progressDialog->setFont(&m_fontNormal);
    m_progressDialog->setSmallFont(&m_fontSmall);
    m_progressDialog->setTheme(&m_theme);

    app().renderer().setBoxWireframeEnabled(m_showWireframe);

    wireFocusCallback();
    m_grid->onPageSwitched([this]() {
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
    createFolderOptions();
    createControllerTest();

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
        step(m_arrowAnimLeft, paging && page > 0);
        step(m_arrowAnimRight, paging && page < total - 1);
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

    if (m_deferredInitialAssetFrames == 0 && m_grid && m_iconStreamer.needsVisibleLoads(
            m_grid->currentPage(), m_grid->iconsPerPage())) {
        m_iconStreamer.onPageChanged(m_grid->currentPage(), m_grid->iconsPerPage(),
                                     app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }

    if (m_deferredProfileFrames > 0) {
        --m_deferredProfileFrames;
    } else if (m_pendingProfileIndex < m_pendingProfileUids.size()) {
        const bool visibleIconsBusy = m_grid && m_iconStreamer.needsVisibleLoads(
            m_grid->currentPage(), m_grid->iconsPerPage());
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
                    m_battery->setBatteryStatus(percent, charging);
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
                        createFolder(index);
                } else if (m_model.at(index).isFolder())
                    showFolderContextMenu(m_model.at(index).folderId);
                else if (m_model.at(index).isApplication())
                    showGameContextMenu(icon);
            }
        }
    }
#endif

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
    if (!debugTouchBlocked && dialogActiveNow)
        m_dialog->handleTouch(app().input());

    if (!debugTouchBlocked && m_themeShop && m_themeShop->isActive())
        m_themeShop->handleTouch(app().input());

    if (!debugTouchBlocked && m_settings && m_settings->isActive()
        && !(m_controllerTest && m_controllerTest->isActive())) {
        m_settings->handleTouch(app().input());
    }

    if (!debugTouchBlocked && m_gameOptions && m_gameOptions->isActive())
        m_gameOptions->handleTouch(app().input());

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
            add(buttonGlyph(nxui::Button::Plus), i18n.tr("folder.create", "Create folder"));
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

bool WiiUMenuApp::flipPage(int dir) {
    if (!m_grid || m_grid->isTransitioning())
        return false;
    const int p = m_grid->currentPage() + dir;
    if (p < 0 || p >= m_grid->totalPages())
        return false;
    m_grid->startPageTransition(p);
    m_audio.playSfx(Sfx::PageChange);
    kickPageArrow(dir);
    return true;
}

void WiiUMenuApp::renderPageArrows(nxui::Renderer& ren) {
    constexpr float kGlyphScale = 0.70f;

    auto drawArrow = [&](bool left, const nxui::Texture& tex,
                         const PageArrowAnim& anim, const std::string& glyph) {
        if (anim.show <= 0.002f || !tex.valid())
            return;

        const float e = anim.show * anim.show * (3.f - 2.f * anim.show);
        const float bump = anim.press * anim.press;
        const nxui::Rect base = pageArrowRect(left);
        const float outward = (left ? -1.f : 1.f) * ((1.f - e) * 16.f + bump * 9.f);
        const float scale = (0.86f + 0.14f * e) * (1.f + 0.18f * bump);

        const float cx = base.x + base.width * 0.5f + outward;
        const float cy = base.y + base.height * 0.5f;
        const float w = base.width * scale;
        const float h = base.height * scale;

        ren.drawTexture(&tex, {cx - w * 0.5f, cy - h * 0.5f, w, h},
                        nxui::Color(1.f, 1.f, 1.f, e));
        const nxui::Vec2 gs = m_fontIcons.measure(glyph);
        ren.drawText(glyph, {cx - gs.x * kGlyphScale * 0.5f, cy + h * 0.5f + 6.f},
                     &m_fontIcons, m_theme.textPrimary.withAlpha(0.9f * e), kGlyphScale);
    };

    drawArrow(true, m_arrowTexLeft, m_arrowAnimLeft, buttonGlyph(nxui::Button::ZL));
    drawArrow(false, m_arrowTexRight, m_arrowAnimRight, buttonGlyph(nxui::Button::ZR));
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
