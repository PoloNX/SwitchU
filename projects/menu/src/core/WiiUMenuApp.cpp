#include "WiiUMenuApp.hpp"
#include "widgets/GlossyIcon.hpp"
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
#include <dirent.h>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

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
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

bool directoryExists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
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

const char* safeTag(const nxui::Widget* widget) {
    if (!widget || widget->tag().empty())
        return "<none>";
    return widget->tag().c_str();
}

}


WiiUMenuApp::WiiUMenuApp() {}
WiiUMenuApp::~WiiUMenuApp() {
    rootBox().clearChildren();
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::setStartupStatus(uint64_t suspendedTitleId, bool appRunning) {
    m_launcher.setStartupStatus(suspendedTitleId, appRunning);
}
#endif

bool WiiUMenuApp::onCreate() {
    DebugLog::log("[init] onCreate enter");
    m_config.load();
    loadMenuLayout();
    m_appLoader.setPendingTransform([this](std::vector<PendingApp>& apps) {
        applyMenuLayoutToPending(apps);
    });

    nxui::I18n::instance().initialize(std::string(SD_ASSETS) + "/i18n", "en-US");
    applyUiLanguage();

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

    bluetooth::Initialize();
    DebugLog::log("[init] Bluetooth manager initialized");
    DebugLog::log("[init] Theme Shop HTTP runtime deferred until first request");

    DebugLog::log("[init] Config loaded (theme=%s, musicVol=%.2f, sfxVol=%.2f)",
                  m_config.themePreset.c_str(), m_config.musicVolume, m_config.sfxVolume);

    m_launcher.init({
        .playSfxModalHide = [this]() { m_audio.playSfx(Sfx::ModalHide); },
        .playSfxLaunchGame = [this]() { m_audio.playSfx(Sfx::LaunchGame); },
        .requestExit = [this]() { app().requestExit(); },
#ifdef SWITCHU_STANDALONE
        .suspendForApp = [this]() {
            DebugLog::log("[suspend-sa] entering standalone suspend (render off)");
            m_musicWasPlaying = m_audio.isPlaying();
            m_audio.stop();
            app().setRenderEnabled(false);
            m_suspended = true;
            DebugLog::log("[suspend-sa] now idle");
        },
        .launchLibraryApplet = [this](AppletId id) {
            // Translate AppletId to a StandaloneManager request.
            if (id == AppletId_LibraryAppletPhotoViewer)   m_standalone.requestLaunchAlbum();
            else if (id == AppletId_LibraryAppletMiiEdit)  m_standalone.requestLaunchMiiEditor();
            else if (id == AppletId_LibraryAppletNetConnect) m_standalone.requestLaunchNetConnect();
        },
#elif defined(SWITCHU_MENU)
        .suspendForApp = [this]() {
            DebugLog::log("[suspend] entering keep-alive suspend");
            m_musicWasPlaying = m_audio.isPlaying();
            m_audio.stop();
            app().setRenderEnabled(false);
            switchu::menu::smi_cmd::menuSuspending();
            switchu::menu::smi_cmd::drainAllResponses();
            m_suspended = true;
            DebugLog::log("[suspend] now idle");
        },
#else
        .suspendForApp = nullptr,
#endif
        .waitGpuIdle = [this]() { app().gpu().waitIdle(); },
        .setRenderEnabled = [this](bool e) { app().setRenderEnabled(e); },
    });


    DebugLog::log("[init] loadResources...");
    loadResources();
    DebugLog::log("[init] buildGrid...");
    buildGrid();

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
#ifdef SWITCHU_STANDALONE
    {
        // Wire StandaloneManager callbacks. These fire from the main-thread
        // update() call so all callbacks are safe to touch menu state directly.
        StandaloneCallbacks sacbs;
        sacbs.onAppSuspended = [this](uint64_t tid) {
            DebugLog::log("[sa-cb] app suspended tid=0x%016lX", tid);
            m_launcher.setAppRunning(true);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(tid);
            m_wakeReason       = 0;
            m_wakeSuspendedTid = tid;
            m_suspended        = false;
            m_sysMsg.pushAction(SysAction::HomeButton);
        };
        sacbs.onAppExited = [this]() {
            DebugLog::log("[sa-cb] app exited");
            m_launcher.setAppRunning(false);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(0);
            m_wakeReason       = 1;
            m_wakeSuspendedTid = 0;
            m_suspended        = false;
            m_sysMsg.pushAction(SysAction::HomeButton);
        };
        sacbs.onAppRecordsChanged = [this]() {
            m_refreshQueued        = true;
            m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 3);
        };
        sacbs.onGcMountFailure = [this](uint32_t) {
            m_refreshQueued        = true;
            m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 3);
        };
        sacbs.onHomeRequest = [this]() {
            m_sysMsg.pushAction(SysAction::HomeButton);
        };
        sacbs.onLibAppletStarted = [this]() {
            DebugLog::log("[sa-cb] library applet starting");
            m_musicWasPlaying    = m_audio.isPlaying();
            m_audio.stop();
            app().setRenderEnabled(false);
            m_libAppletInProgress = true;
        };
        sacbs.onLibAppletReturned = [this]() {
            DebugLog::log("[sa-cb] library applet returned");
            m_libAppletInProgress = false;
            // render + music re-enable handled by !renderEnabled() block in onUpdate
        };
        m_standalone.setCallbacks(std::move(sacbs));
        m_standalone.start();
    }
    DebugLog::log("[init] StandaloneManager started");
#else
    DebugLog::log("[init] async notifications via AppletStorage only");
    switchu::menu::smi_cmd::menuReady();
#endif // SWITCHU_STANDALONE
#endif // SWITCHU_MENU

    DebugLog::log("[init] DONE");
    return true;
}

void WiiUMenuApp::onDestroy() {
    if (m_audioFuture.valid()) m_audioFuture.get();

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
#ifdef SWITCHU_STANDALONE
    m_standalone.stop();
    DebugLog::log("[destroy] StandaloneManager stopped");
#else
    switchu::menu::smi_cmd::menuClosing();
    switchu::menu::smi_cmd::drainAllResponses();
#endif
#endif
    if (m_layoutDirty)
        saveMenuLayout();
    m_audio.shutdown();
}

void WiiUMenuApp::loadResources() {
    std::string fontPath = std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf";
    m_fontNormal.load(app().gpu(), app().renderer(), fontPath, 24);
    m_fontSmall.load(app().gpu(), app().renderer(), fontPath, 18);

    std::string gameCardPath = std::string(SD_ASSETS) + "/icons/gamecard.png";
    m_gameCardTex.loadFromFile(app().gpu(), app().renderer(), gameCardPath);

    m_appLoader.load(m_model, m_iconStreamer);
}

WiiUMenuApp::GridLayoutMetrics WiiUMenuApp::computeGridLayoutMetrics() const {
    const int cols = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);

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
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/SwitchU", 0777);

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
    const int cols = std::clamp(m_config.gridColumns, 3, 8);
    const int rows = std::clamp(m_config.gridRows, 2, 5);
    const int perPage = std::max(1, cols * rows);

    std::unordered_map<uint64_t, PendingApp> byId;
    byId.reserve(apps.size());
    for (auto& app : apps) {
        if (app.titleId != 0)
            byId.emplace(app.titleId, std::move(app));
    }

    std::vector<uint64_t> slots = m_layoutSlots;
    if (slots.empty()) {
        slots.reserve(apps.size());
        for (const auto& app : apps)
            if (app.titleId != 0)
                slots.push_back(app.titleId);
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

    for (const auto& app : apps) {
        if (app.titleId == 0 || placed.count(app.titleId))
            continue;

        auto emptyIt = std::find(slots.begin(), slots.end(), 0);
        if (emptyIt != slots.end())
            *emptyIt = app.titleId;
        else
            slots.push_back(app.titleId);

        placed.insert(app.titleId);
    }

    int minSlots = std::max(perPage * kMinHomePages, (int)slots.size());
    int roundedSlots = ((minSlots + perPage - 1) / perPage) * perPage;
    if ((int)slots.size() < roundedSlots)
        slots.resize(roundedSlots, 0);

    std::vector<PendingApp> ordered;
    ordered.reserve(slots.size());
    for (uint64_t tid : slots) {
        if (tid == 0) {
            ordered.emplace_back();
            continue;
        }
        auto it = byId.find(tid);
        if (it != byId.end()) {
            ordered.push_back(std::move(it->second));
        } else {
            ordered.emplace_back();
        }
    }

    if (slots != m_layoutSlots) {
        m_layoutSlots = std::move(slots);
        m_layoutDirty = true;
    }

    apps = std::move(ordered);
}

std::shared_ptr<GlossyIcon> WiiUMenuApp::makeIcon(const AppEntry& entry) {
    auto icon = std::make_shared<GlossyIcon>();
    if (entry.titleId == 0) {
        icon->setTag("glossy_icon");
        icon->setTitle("");
        icon->setTitleId(0);
        icon->setFocusable(true);
        icon->setNotLaunchable(false);
        icon->setCornerRadius(m_theme.iconCornerRadius);
        return icon;
    }

    icon->setTag("glossy_icon");
    icon->setTitle(entry.title);
    icon->setTitleId(entry.titleId);
    // Texture is set by IconStreamer::onPageChanged() — not here.
    icon->setCornerRadius(m_theme.iconCornerRadius);
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
            const AppEntry* entry = m_model.findByTitleId(tid);
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
            if (m_userSelect) {
                bool usersLoaded = m_userSelect->loadUsers(app().gpu(), app().renderer());
                DebugLog::log("[UserSelect] lazy load result=%d", usersLoaded ? 1 : 0);
            }
            m_userSelect->show([this, fr, tex, cr, base, bord, tid](AccountUid uid) {
                m_audio.playSfx(Sfx::LaunchGame);
                m_launchAnim->start(fr, tex, cr, base, bord, tid, uid,
                    [this](uint64_t id, AccountUid u) { m_launcher.launchApplication(id, u); });
            });
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
        m_activePresetName = "builtin:Default Dark";
        preset = findPresetPtr(m_activePresetName);
    }
    if (!preset) {
        m_activePresetName = "Default Dark";
        preset = findPresetPtr(m_activePresetName);
    }

    if (preset)
        m_activePresetName = preset->id.empty() ? preset->name : preset->id;

    m_activeColors = preset->colors;
    if (m_config.accentH >= 0.f) m_activeColors.accentH = m_config.accentH;
    if (m_config.accentS >= 0.f) m_activeColors.accentS = m_config.accentS;
    if (m_config.accentL >= 0.f) m_activeColors.accentL = m_config.accentL;
    if (m_config.bgH     >= 0.f) m_activeColors.bgH     = m_config.bgH;
    if (m_config.bgS     >= 0.f) m_activeColors.bgS     = m_config.bgS;
    if (m_config.bgL     >= 0.f) m_activeColors.bgL     = m_config.bgL;
    if (m_config.bgAccH  >= 0.f) m_activeColors.bgAccH  = m_config.bgAccH;
    if (m_config.bgAccS  >= 0.f) m_activeColors.bgAccS  = m_config.bgAccS;
    if (m_config.bgAccL  >= 0.f) m_activeColors.bgAccL  = m_config.bgAccL;
    if (m_config.shapeH  >= 0.f) m_activeColors.shapeH  = m_config.shapeH;
    if (m_config.shapeS  >= 0.f) m_activeColors.shapeS  = m_config.shapeS;
    if (m_config.shapeL  >= 0.f) m_activeColors.shapeL  = m_config.shapeL;

    if (m_config.themeMode == "dark")
        m_activeMode = nxui::ThemeMode::Dark;
    else if (m_config.themeMode == "light")
        m_activeMode = nxui::ThemeMode::Light;
    else
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

    m_userSelect = std::make_shared<UserSelectScreen>();
    m_userSelect->setFont(&m_fontNormal);
    m_userSelect->setSmallFont(&m_fontSmall);
    m_userSelect->setAudio(&m_audio);

    m_dialog = std::make_shared<OverlayDialog>();
    m_dialog->setFont(&m_fontNormal);
    m_dialog->setSmallFont(&m_fontSmall);
    m_dialog->setTheme(&m_theme);
    m_dialog->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_dialog->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_dialog->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });

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

    // Load textures for the initial page (page 0).
    m_iconStreamer.onPageChanged(0, m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());

    m_grid->startAppearAnimation();

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
            if (m_themeShop && m_themeShop->isActive())
                m_themeShop->hide();
            m_settings->show();
            focusManager().setFocus(m_settings.get());
        }
    };
    sidebarActions.onSleep = [this]() {
        if (!m_dialog) return;
        m_audio.playSfx(Sfx::ModalShow);
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            "Sleep",
            "Put the console into sleep mode?",
            {
                {"Cancel", [this]() {  }, true},
                {"Sleep", [this]() {
#ifdef SWITCHU_MENU
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_launcher.enterSleep();
#else
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    app().requestExit();
#endif
                }, true}
            },
            1,
            {}
        );
        focusManager().setFocus(m_dialog.get());
    };
    sidebarActions.onMiiverse = [this]() {
        m_audio.playSfx(Sfx::ModalShow);
        if (!m_themeShop) return;
        if (m_settings && m_settings->isActive())
            m_settings->hide();
        refreshThemeShopState();
        m_themeShop->show();
        focusManager().setFocus(m_themeShop.get());
    };

    m_sidebar.build(app().gpu(), app().renderer(), SD_ASSETS, sidebarActions);
    m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS,
                           resolveThemeAssetPath(m_effectivePreset, m_effectivePreset.icons.basePath));

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

    m_topHud = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    m_topHud->setRect({0, 0, 1280, 90});
    m_topHud->setTag("topHud");
    m_topHud->setWireframeEnabled(false);
    m_topHud->setJustifyContent(nxui::JustifyContent::SPACE_BETWEEN);
    m_topHud->setAlignItems(nxui::AlignItems::FLEX_START);

    // Build user-avatar bar (center of top HUD).
    m_userAvatarBar = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    m_userAvatarBar->setMarginTop(17.f);
    m_userAvatarBar->setGap(10.f);  // Proper spacing between buttons
    m_userAvatarBar->setTag("userAvatarBar");
    m_userAvatarBar->setWireframeEnabled(false);
    m_userAvatarButtons.clear();
    {
        AccountUid uids[8] = {};
        s32 count = 0;
        if (R_SUCCEEDED(accountListAllUsers(uids, 8, &count)) && count > 0) {
            for (int i = 0; i < count; ++i) {
                AccountProfile prof;
                if (R_FAILED(accountGetProfile(&prof, uids[i]))) continue;
                auto btn = std::make_shared<UserAvatarButton>();
                btn->setSize(56.f, 56.f);
                btn->setCornerRadius(28.f);
                btn->setUid(uids[i]);
                
                // Load user nickname
                AccountProfileBase base = {};
                AccountUserData userData = {};
                if (R_SUCCEEDED(accountProfileGet(&prof, &userData, &base))) {
                    btn->setNickname(base.nickname);
                }
                
                u32 imgSize = 0;
                if (R_SUCCEEDED(accountProfileGetImageSize(&prof, &imgSize)) && imgSize > 0) {
                    std::vector<uint8_t> imgBuf(imgSize);
                    u32 realSize = 0;
                    if (R_SUCCEEDED(accountProfileLoadImage(&prof, imgBuf.data(),
                                                            imgSize, &realSize))
                            && realSize > 0) {
                        btn->loadAvatar(app().gpu(), app().renderer(),
                                        imgBuf.data(), realSize);
                    }
                }
                accountProfileClose(&prof);
#ifdef SWITCHU_STANDALONE
                {
                    AccountUid uid = uids[i];
                    btn->addAction(static_cast<uint64_t>(nxui::Button::A), [this, uid]() {
                        m_audio.playSfx(Sfx::Activate);
                        m_standalone.requestLaunchMyPage(uid);
                    });
                }
#endif
                m_userAvatarButtons.push_back(btn);
                m_userAvatarBar->addChild(btn);
            }
        }
    }

    m_topHud->addChild(m_clock);
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

    m_contentLayer->addChild(m_grid);
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

    m_overlayLayer->addChild(m_dialog);
    m_overlayLayer->addChild(m_launchAnim);
    m_overlayLayer->addChild(m_pointerCursor);

    root.addChild(m_bgLayer);
    root.addChild(m_contentLayer);
    root.addChild(m_overlayLayer);

    if (auto* firstIcon = m_grid->focusManager().current())
        focusManager().setFocus(firstIcon);

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

    if (base.empty())
        base = std::string(SD_ASSETS) + "/sounds/" + effectivePreset;
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
    DIR* dir = opendir(musicDir.c_str());
    if (dir) {
        std::vector<std::string> tracks;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.size() > 4 && name.substr(name.size() - 4) == ".mp3")
                tracks.push_back(name);
        }
        closedir(dir);
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
    DIR* dir = opendir(soundsDir.c_str());
    if (!dir) return presets;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (name != kBuiltInSoundPreset) continue;

        std::string sub = soundsDir + "/" + name;
        struct stat st;
        if (stat(sub.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        std::string sfxSub = sub + "/sfx";
        std::string musicSub = sub + "/music";
        struct stat st2;
        bool hasSfx = (stat(sfxSub.c_str(), &st2) == 0 && S_ISDIR(st2.st_mode));
        bool hasMusic = (stat(musicSub.c_str(), &st2) == 0 && S_ISDIR(st2.st_mode));
        if (hasSfx || hasMusic)
            presets.push_back(name);
    }
    closedir(dir);
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

    app().gpu().waitIdle();
    m_grid->clearChildren();
    m_model.clear();
    m_iconStreamer.clear();

    m_appLoader.finalize(m_model, m_iconStreamer);
    DebugLog::log("[refresh] found %d apps", m_model.count());

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
    DebugLog::log("[refresh] calling onPageChanged(page=%d iconsPerPage=%d allIcons=%d)",
                  page, m_grid->iconsPerPage(), (int)m_grid->allIcons().size());
    m_iconStreamer.onPageChanged(page, m_grid->iconsPerPage(),
                                 app().gpu(), app().renderer(),
                                 m_grid->allIcons());
    DebugLog::log("[refresh] onPageChanged returned");

    m_grid->startAppearAnimation();
    DebugLog::log("[refresh] startAppearAnimation done");
    if (auto* firstIcon = m_grid->focusManager().current())
        focusManager().setFocus(firstIcon);
    DebugLog::log("[refresh] focus set");

    // Keep a short cooldown to coalesce duplicate app-record notifications.
    m_refreshCooldownFrames = 20;
    applyTheme();
    DebugLog::log("[refresh] applyTheme done");
    if (m_layoutDirty)
        saveMenuLayout();
    DebugLog::log("[refresh] done, %d icons on page %d", m_model.count(), m_grid->currentPage());
}

#endif

void WiiUMenuApp::onUpdate(float dt) {
#ifdef SWITCHU_DEBUG_UI
    if (m_debugOverlay) {
        m_debugOverlay->setDeltaTime(dt);
    }
#endif

    if (m_suspended) {
#ifdef SWITCHU_STANDALONE
        // In standalone mode the wake is fired by StandaloneManager callbacks
        // which clear m_suspended directly.  Just keep driving the event loop.
        m_standalone.update();
        if (m_suspended) {
            m_sysMsg.pump();
            return;
        }
        // else: m_suspended was cleared by onAppSuspended / onAppExited callback.
        // Fall through to the rest of onUpdate() this frame.
#elif defined(SWITCHU_MENU)
        AppletStorage wakeSt;
        if (R_SUCCEEDED(appletPopInteractiveInData(&wakeSt))) {
            switchu::smi::WakeSignal ws{};
            s64 sz = 0;
            appletStorageGetSize(&wakeSt, &sz);
            if (sz >= (s64)sizeof(ws))
                appletStorageRead(&wakeSt, 0, &ws, sizeof(ws));
            appletStorageClose(&wakeSt);

            if (ws.magic == switchu::smi::kWakeMagic) {
                DebugLog::log("[suspend] wake signal received (reason=%u tid=0x%016lX)",
                              ws.reason, ws.suspended_tid);
                m_wakeReason = ws.reason;
                m_wakeSuspendedTid = ws.suspended_tid;
                m_suspended = false;
            } else {
                return;
            }
        } else {
            m_sysMsg.pump();
            return;
        }
#else
        return;
#endif
    }

    if (m_returnFadeTimer > 0.f)
        m_returnFadeTimer = std::max(0.f, m_returnFadeTimer - dt);

    syncThemePackageTransfer();

    if (!app().renderEnabled()) {
        DebugLog::log("[suspend] resuming GPU on main thread");
        if (m_launchAnim && m_launchAnim->isPlaying()) m_launchAnim->stop();

#ifdef SWITCHU_MENU
#ifdef SWITCHU_STANDALONE
        // For a library applet return the launcher state was already set by the
        // onAppSuspended / onAppExited callbacks; skip the wake-reason path.
        if (!m_libAppletInProgress) {
            if (m_wakeReason == 0) {
                m_launcher.setAppRunning(true);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(m_wakeSuspendedTid);
            } else {
                m_launcher.setAppRunning(false);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(0);
            }
        }
#else
        if (m_wakeReason == 0) {
            m_launcher.setAppRunning(true);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(m_wakeSuspendedTid);
        } else {
            m_launcher.setAppRunning(false);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(0);
        }
#endif // SWITCHU_STANDALONE

        {
            uint64_t sTid = m_launcher.suspendedTitleId();
            for (auto& ic : m_grid->allIcons())
                ic->setSuspended(sTid != 0 && ic->titleId() == sTid);
            if (auto* cur = m_grid->focusManager().current()) {
                auto* icon = static_cast<GlossyIcon*>(cur);
                if (m_launcher.isAppSuspended(icon->titleId()))
                    m_titlePill->setText(std::string("\xe2\x96\xb6  ") + icon->title());
                else
                    m_titlePill->setText(icon->title());
            }
            DebugLog::log("[suspend] icons updated (suspendedTid=0x%016lX)", sTid);
        }
#endif // SWITCHU_MENU
        app().gpu().waitIdle();
        app().setRenderEnabled(true);
        if (m_musicWasPlaying && m_config.musicEnabled) m_audio.play();
        m_musicWasPlaying = false;
        m_returnFadeTimer = kReturnFadeInDur;
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
#ifdef SWITCHU_STANDALONE
    // Standalone: drive the system-applet event loop directly.
    // Callbacks fire synchronously and push to m_sysMsg or mutate state.
    m_standalone.update();
    m_sysMsg.pump();
#else
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
            default:
                break;
            }
        }
    }
    m_sysMsg.pump();
#endif // SWITCHU_STANDALONE
    if (m_refreshCooldownFrames > 0)
        --m_refreshCooldownFrames;
    if (m_deferredRefreshFrames > 0)
        --m_deferredRefreshFrames;
    if (m_refreshQueued && m_deferredRefreshFrames == 0 &&
        !m_asyncRefreshPending && m_refreshCooldownFrames == 0 &&
        m_returnFadeTimer <= 0.f) {
        DebugLog::log("[update] deferred refresh triggered, starting refreshAppList");
        refreshAppList();
    }
    if (m_asyncRefreshPending && m_appLoader.isReady() && m_returnFadeTimer <= 0.f) {
        finalizeRefresh();
    }
#endif

    if (m_showLoadingScreen && ++m_loadingScreenFrames > 60) {
        m_showLoadingScreen = false;
        m_loadingScreenFrames = 0;
    }

    bool debugTouchBlocked = false;
#ifdef SWITCHU_DEBUG_UI
    debugTouchBlocked = m_showDebugOverlay;
#endif

    if (!debugTouchBlocked
        && !m_launchAnim->isPlaying()
        && !(m_dialog && m_dialog->isActive())
        && !(m_themeShop && m_themeShop->isActive())
        && !(m_settings && m_settings->isActive())
        && !(m_userSelect && m_userSelect->isActive()))
    {
        handleTouch();
    }

    bool dialogActiveNow = (m_dialog && m_dialog->isActive());
    if (!debugTouchBlocked && dialogActiveNow)
        m_dialog->handleTouch(app().input());

    if (!debugTouchBlocked && m_themeShop && m_themeShop->isActive())
        m_themeShop->handleTouch(app().input());

    if (!debugTouchBlocked && m_settings && m_settings->isActive())
        m_settings->handleTouch(app().input());

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

void WiiUMenuApp::onRender(nxui::Renderer& ren) {
    if (m_showLoadingScreen && !m_launchAnim->isPlaying())
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, 1.f));

    if (m_returnFadeTimer > 0.f) {
        float alpha = m_returnFadeTimer / kReturnFadeInDur;
        ren.drawRect({0, 0, 1280, 720}, nxui::Color(0, 0, 0, alpha));
    }

    if (m_touchHitIndex >= 0 && !m_touchOnFocused && app().input().isTouching()) {
        auto icons = m_grid->pageIcons();
        if (m_touchHitIndex < (int)icons.size()) {
            nxui::Rect r = icons[m_touchHitIndex]->focusRect();
            float cr = icons[m_touchHitIndex]->cornerRadius();
            ren.drawRoundedRect(r, nxui::Color(1.f, 1.f, 1.f, 0.18f), cr);
        }
    }

    m_pageIndicator->setPageCount(m_grid->totalPages());
    m_pageIndicator->setCurrentPage(m_grid->currentPage());

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

#ifdef SWITCHU_DEBUG_UI
    if (m_debugOverlay) {
        m_debugOverlay->render(ren, app().input(), m_showDebugOverlay);
    }
#endif
}

