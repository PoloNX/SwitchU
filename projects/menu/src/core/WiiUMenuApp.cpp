#include "WiiUMenuApp.hpp"
#include <switchu/sd_commit.hpp>
#include "widgets/GlossyIcon.hpp"
#include "widgets/FolderPalette.hpp"
#include "themeshop/ThemeHttp.hpp"
#include <nxui/core/Animation.hpp>
#include <nxui/core/I18n.hpp>
#include "DebugLog.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include "LeaveFrameCache.hpp"
#include "screen_fit.hpp"
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
#include <limits>

namespace {

// First empty slot not covered by a multi-cell tile anchored earlier (those
// cells also hold 0). Returns slots.size() when none is free.
template <typename SpanOf>
std::size_t firstFreeSlot(const std::vector<std::uint64_t>& slots, int columns,
                          int perPage, bool grid, SpanOf spanOf) {
    std::vector<bool> covered(slots.size(), false);
    if (grid) {
        for (std::size_t index = 0; index < slots.size(); ++index) {
            if (slots[index] == 0) continue;
            const auto [spanColumns, spanRows] = spanOf(slots[index]);
            if (spanColumns <= 1 && spanRows <= 1) continue;
            const int local = static_cast<int>(index) % perPage;
            if (local % columns + spanColumns > columns) continue;
            for (int dy = 0; dy < spanRows; ++dy)
                for (int dx = 0; dx < spanColumns; ++dx) {
                    const std::size_t cell = index + static_cast<std::size_t>(dy * columns + dx);
                    if (cell < covered.size()) covered[cell] = true;
                }
        }
    }
    for (std::size_t index = 0; index < slots.size(); ++index)
        if (slots[index] == 0 && !covered[index]) return index;
    return slots.size();
}

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
static constexpr int kMinHomePages = 1;
static constexpr int kMaxHomePages = switchu::folders::kMaxFolderPages;
static constexpr const char* kBuiltInSoundPreset = "wiiu";

static constexpr float kGridRectX = 0.f;
static constexpr float kGridRectY = 90.f;
static constexpr float kGridRectW = 1280.f;
static constexpr float kGridRectH = 540.f;
// The profile tile keeps the clock / battery height but stays square, so the
// HUD row reads as one set of chips while the avatar is not stretched.
static constexpr float kProfileLockWidth = 62.f;
static constexpr float kProfileLockHeight = 62.f;

static const nxui::Rect kFolderGridRect{kGridRectX, 148.f, kGridRectW, 470.f};
static constexpr float kFolderPanelPadX = 22.f;
static constexpr float kFolderPanelPadY = 16.f;
// The title pill sits at y=630 below the folder grid.
static constexpr float kFolderPanelMaxBottom = 622.f;
// The folder title floats just above the panel; the top HUD is hidden then.
static constexpr float kFolderHeaderGap  = 10.f;
static constexpr float kFolderHeaderMinY = 14.f;
static constexpr float kFolderHeaderSlide = 14.f;

// Icons start once the panel has taken shape, and on close they are all back
// inside the tile by the time the panel has shrunk into it.
static constexpr float kFolderZoomCascadeDelay   = 0.12f;
static constexpr float kFolderZoomCascadeStagger = 0.20f;
static constexpr float kFolderCloseStagger       = 0.10f;

static constexpr float kGridBaseCellW = 150.f;
static constexpr float kGridBaseCellH = 150.f;
static constexpr float kGridBasePadX  = 20.f;
static constexpr float kGridBasePadY  = 16.f;

GridModel compactDynamicLineEntries(const GridModel& source) {
    GridModel compacted;
    for (const auto& entry : source.entries()) {
        if (entry.kind == GridEntryKind::Empty ||
            entry.kind == GridEntryKind::WidgetContinuation)
            continue;
        compacted.addEntry(entry);
    }
    // Single-row view: omit trailing empty pads (they stay useful in grid).
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


bool isMp3Filename(const std::string& name) {
    if (name.size() < 4)
        return false;
    const auto ext = name.substr(name.size() - 4);
    return ext == ".mp3" || ext == ".MP3";
}

std::vector<uint8_t> readBinaryFileLimited(const std::string& path, size_t maxBytes = 2 * 1024 * 1024) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    f.seekg(0, std::ios::end);
    const auto sz = static_cast<size_t>(f.tellg());
    if (sz == 0 || sz > maxBytes)
        return {};
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(sz);
    f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(sz));
    if (!f)
        return {};
    return bytes;
}

std::vector<uint8_t> loadAlbumFolderCover(const std::string& albumDir) {
    static const char* kNames[] = {
        "cover.jpg", "cover.png", "folder.jpg", "folder.png",
        "AlbumArt.jpg", "AlbumArt.png", "albumart.jpg", "albumart.png"
    };
    for (const char* name : kNames) {
        auto bytes = readBinaryFileLimited(albumDir + "/" + name);
        if (!bytes.empty())
            return bytes;
    }
    return {};
}

void sortMusicTrackNames(std::vector<std::string>& names) {
    std::sort(names.begin(), names.end(), [](const std::string& left, const std::string& right) {
        const bool leftIsHome = (left == "home.mp3" || left == "home.MP3");
        const bool rightIsHome = (right == "home.mp3" || right == "home.MP3");
        if (leftIsHome != rightIsHome)
            return leftIsHome;
        return left < right;
    });
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
static constexpr float kHintIconScale = 0.67f;
static constexpr float kHintTextScale = 0.57f;
static constexpr float kHintCapH      = 25.f;
static constexpr float kHintCapPadX   = 10.f;
static constexpr float kHintIconGap   = 5.f;
static constexpr float kHintCapGap    = 7.f;
static constexpr float kHintRowGap    = 6.f;
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

std::string accountUidToHex(const AccountUid& uid) {
    char buf[33] = {};
    std::snprintf(buf, sizeof(buf), "%016llX%016llX",
                  (unsigned long long)uid.uid[0],
                  (unsigned long long)uid.uid[1]);
    return std::string(buf);
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
           a.folderCoverTitleId == b.folderCoverTitleId &&
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

bool WiiUMenuApp::presentInitialFrame(nxui::Renderer& ren) {
#ifdef SWITCHU_MENU
    if (!m_fastReturnRequested)
        return false;

    LeaveFrameImage image;
    if (LeaveFrameCache::load(m_leaveSession, image) && image.valid()
        && m_leaveSplashTex.loadFromPixels(app().gpu(), ren,
                                           image.rgba.data(), image.width, image.height)) {
        ren.drawTexture(&m_leaveSplashTex, {0.f, 0.f, 1280.f, 720.f});
        m_leaveSplashDrawn = true;
        m_leaveSplashPhase = LeaveSplashPhase::Hold;
        m_leaveSplashHoldRemaining = kLeaveSplashHoldDur;
        m_leaveSplashFade = 1.f;
        m_leaveMotionFreezePending = true;
        DebugLog::log("[leave] splash %dx%d page=%d folder=%u focus=%016lX",
                      image.width, image.height, m_leaveSession.page,
                      m_leaveSession.openFolderId,
                      static_cast<unsigned long>(m_leaveSession.focusTitleId));
        return true;
    }

    // No cached frame ΓÇö still avoid pure black while onCreate runs.
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, nxui::Color(0.07f, 0.12f, 0.18f, 1.f));
    m_leaveSplashDrawn = true;
    m_leaveSplashPhase = LeaveSplashPhase::None;
    m_leaveSplashFade = 0.f;
    DebugLog::log("[leave] splash fallback solid");
    return true;
#else
    (void)ren;
    return false;
#endif
}

void WiiUMenuApp::scheduleLeaveCapture(std::function<void()> afterCapture,
                                       std::uint64_t previewSuspendedTitleId) {
    // Title launch/resume: lock green pulse, blue cursor, and session focus to
    // the launching title before this frame renders. Without that, a fast
    // Spharia↔MPO hop can capture the previous tile's focus while the green
    // outline already moved.
    if (previewSuspendedTitleId != 0)
        previewLeaveCaptureTitle(previewSuspendedTitleId);
    else
        m_leaveCaptureFocusTitleId = 0;

    // User/profile/settings overlays animate out. Capturing the next frame
    // immediately would persist the fading overlay instead of the HOME scene.
    if (hasActiveLeaveCaptureOverlay()) {
        m_leaveCaptureDeferred = true;
        if (previewSuspendedTitleId != 0)
            m_leaveCaptureDeferredSuspendedTitleId = previewSuspendedTitleId;
        if (afterCapture) {
            if (m_leaveCaptureDeferredAfter) {
                auto previous = std::move(m_leaveCaptureDeferredAfter);
                m_leaveCaptureDeferredAfter = [previous = std::move(previous),
                                               next = std::move(afterCapture)]() mutable {
                    if (previous) previous();
                    if (next) next();
                };
            } else {
                m_leaveCaptureDeferredAfter = std::move(afterCapture);
            }
        }
        return;
    }

    if (m_leaveCapturePending) {
        if (!afterCapture)
            return;
        if (m_leaveCaptureAfter) {
            auto prev = std::move(m_leaveCaptureAfter);
            m_leaveCaptureAfter = [prev = std::move(prev),
                                   next = std::move(afterCapture)]() mutable {
                if (prev) prev();
                if (next) next();
            };
        } else {
            m_leaveCaptureAfter = std::move(afterCapture);
        }
        return;
    }
    m_leaveCapturePending = true;
    m_leaveCaptureAfter = std::move(afterCapture);
}

bool WiiUMenuApp::hasActiveLeaveCaptureOverlay() const {
    return (m_userSelect && m_userSelect->isActive())
        || (m_contextMenu && m_contextMenu->isActive())
        || (m_dialog && m_dialog->isActive())
        || (m_quickSettings && m_quickSettings->isActive())
        || (m_textEntry && m_textEntry->isActive())
        || (m_progressDialog && m_progressDialog->isActive())
        || (m_settings && m_settings->isActive())
        || (m_themeShop && m_themeShop->isActive())
        || (m_gameOptions && m_gameOptions->isActive())
        || (m_folderOptions && m_folderOptions->isActive())
        || (m_controllerTest && m_controllerTest->isActive())
        || (m_profileCarousel && m_profileCarousel->isActive())
        || (m_steamGridDbPicker && m_steamGridDbPicker->isActive());
}

void WiiUMenuApp::pollDeferredLeaveCapture() {
    if (!m_leaveCaptureDeferred || hasActiveLeaveCaptureOverlay())
        return;

    auto afterCapture = std::move(m_leaveCaptureDeferredAfter);
    const std::uint64_t previewTitleId = m_leaveCaptureDeferredSuspendedTitleId;
    m_leaveCaptureDeferred = false;
    m_leaveCaptureDeferredAfter = {};
    m_leaveCaptureDeferredSuspendedTitleId = 0;
    scheduleLeaveCapture(std::move(afterCapture), previewTitleId);
}

void WiiUMenuApp::setSuspendedIconVisuals(std::uint64_t titleId) {
    if (!m_grid)
        return;
    for (auto& icon : m_grid->allIcons())
        icon->setSuspended(titleId != 0 && icon->titleId() == titleId);
}

void WiiUMenuApp::previewLeaveCaptureTitle(std::uint64_t titleId) {
    m_leaveCaptureFocusTitleId = titleId;
    setSuspendedIconVisuals(titleId);
    if (titleId == 0 || !m_grid)
        return;

    // Only retarget focus when the launching title is already on the current
    // grid (root page or open folder). Never open a folder here — that would
    // fight the splash composition.
    const int idx = findTitleIndex(titleId);
    if (idx < 0 || !m_grid->focusGlobalIndex(idx))
        return;

    if (auto* cur = m_grid->focusManager().current()) {
        focusManager().setFocus(cur);
        if (m_cursor) {
            // Snap immediately so the captured splash does not bake in a
            // mid-tween blue ring still sitting on the previous title.
            m_cursor->moveTo(cur->focusRect().expanded(4.f), 0.f);
            m_cursor->setVisible(true);
        }
    }
}

LeaveFrameSession WiiUMenuApp::captureLeaveSession() const {
    LeaveFrameSession session;
    session.valid = true;
    session.openFolderId = m_openFolderId;
    session.page = m_grid ? m_grid->currentPage() : 0;
    session.focusTitleId = 0;

    // Prefer the launching title stamped by scheduleLeaveCapture — focusManager
    // can still briefly report the previous icon after a fast hop.
    if (m_leaveCaptureFocusTitleId != 0) {
        session.focusTitleId = m_leaveCaptureFocusTitleId;
        return session;
    }

    if (auto* cur = focusManager().current()) {
        if (cur->tag() == "glossy_icon")
            session.focusTitleId = static_cast<const GlossyIcon*>(cur)->titleId();
    }
    if (session.focusTitleId == 0 && m_grid) {
        const int idx = m_grid->focusedGlobalIndex();
        const auto& icons = m_grid->allIcons();
        if (idx >= 0 && idx < static_cast<int>(icons.size()) && icons[idx])
            session.focusTitleId = icons[idx]->titleId();
    }
    return session;
}

bool WiiUMenuApp::saveLeaveFrame(nxui::Renderer& ren) {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    if (!ren.downloadFramebufferRgba(rgba, width, height, false)) {
        DebugLog::log("[leave] framebuffer download failed");
        m_leaveCaptureFocusTitleId = 0;
        return false;
    }

    const LeaveFrameSession session = captureLeaveSession();
    m_leaveCaptureFocusTitleId = 0;
    if (!LeaveFrameCache::save(session, rgba.data(), width, height)) {
        DebugLog::log("[leave] cache write failed");
        return false;
    }

    DebugLog::log("[leave] saved %dx%d page=%d folder=%u focus=%016lX",
                  width, height, session.page, session.openFolderId,
                  static_cast<unsigned long>(session.focusTitleId));
    return true;
}

void WiiUMenuApp::onAfterPresent(nxui::Renderer& ren) {
    // Upload layout-prefetch textures after HOME has painted so GPU work
    // doesn't stall the first post-toggle frames.
    if (m_layoutTransition.kind == LayoutTransitionKind::PrefetchThenMorph) {
        m_iconStreamer.pumpShadowPrefetch(app().gpu(), ren);
    }

    if (!m_leaveCapturePending)
        return;

    m_leaveCapturePending = false;
    saveLeaveFrame(ren);
    if (m_leaveCaptureAfter) {
        auto after = std::move(m_leaveCaptureAfter);
        m_leaveCaptureAfter = {};
        after();
    }
}

void WiiUMenuApp::restoreLeaveSession() {
    if (!m_leaveSession.valid || !m_grid)
        return;

    const LeaveFrameSession session = m_leaveSession;
    m_leaveSession.valid = false;

    if (session.openFolderId != 0 && m_folderStore.find(session.openFolderId)) {
        // Do NOT openCapturedFolder here — HUD/header/sidebars/offscreen blur
        // are not built yet. Defer through requestOpenFolder after layers exist
        // so the frosted root capture runs under the leave splash.
        m_leaveRestoreFolderId = session.openFolderId;
        m_leaveRestoreFolderFocus = session.focusTitleId;
        const int folderIdx = findTitleIndex(folderTitleId(session.openFolderId));
        if (folderIdx >= 0 && m_grid->focusGlobalIndex(folderIdx)) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
        DebugLog::log("[leave] deferred folder restore id=%u focus=%016lX",
                      session.openFolderId,
                      static_cast<unsigned long>(session.focusTitleId));
        return;
    }

    if (m_appLayoutMode == AppLayoutMode::Grid && session.page > 0)
        m_grid->setPage(session.page);

    // Ensure single-row is snapped (no leftover boot morph) before focus restore.
    m_grid->setLayoutMode(m_appLayoutMode, false);

    if (session.focusTitleId != 0) {
        const int idx = findTitleIndex(session.focusTitleId);
        const bool instant = m_appLayoutMode == AppLayoutMode::DynamicLine;
        if (idx >= 0 && m_grid->focusGlobalIndex(idx, instant)) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
    }

    DebugLog::log("[leave] restored page=%d focus=%016lX layout=%d",
                  session.page, static_cast<unsigned long>(session.focusTitleId),
                  static_cast<int>(m_appLayoutMode));
}

bool WiiUMenuApp::leaveSplashActive() const {
    return m_leaveSplashPhase != LeaveSplashPhase::None;
}

void WiiUMenuApp::setLeaveMotionFrozen(bool frozen) {
    if (m_leaveMotionFrozen == frozen)
        return;
    m_leaveMotionFrozen = frozen;

    if (m_background)
        m_background->setMotionPaused(frozen);
    if (m_cursor)
        m_cursor->setMotionPaused(frozen);
    if (m_grid) {
        for (auto& icon : m_grid->allIcons()) {
            if (icon)
                icon->setMotionPaused(frozen);
        }
    }
}

void WiiUMenuApp::updateLeaveSplashHandoff(float dt) {
    if (m_leaveMotionFreezePending && m_background) {
        setLeaveMotionFrozen(true);
        m_leaveMotionFreezePending = false;
        // Snap the cursor so the live frame under the splash matches focus.
        if (m_cursor && focusManager().current()) {
            m_cursor->moveTo(focusManager().current()->focusRect().expanded(4.f), 0.f);
            m_cursor->setVisible(true);
        }
    }

    if (m_leaveSplashPhase == LeaveSplashPhase::Hold) {
        m_leaveSplashHoldRemaining -= dt;
        if (m_leaveSplashHoldRemaining <= 0.f) {
            m_leaveSplashHoldRemaining = 0.f;
            m_leaveSplashPhase = LeaveSplashPhase::Fade;
            DebugLog::log("[leave] splash crossfade begin");
        }
        return;
    }

    if (m_leaveSplashPhase == LeaveSplashPhase::Fade) {
        m_leaveSplashFade = std::max(0.f, m_leaveSplashFade - dt / kLeaveSplashFadeDur);
        if (m_leaveSplashFade <= 0.f) {
            m_leaveSplashFade = 0.f;
            m_leaveSplashPhase = LeaveSplashPhase::None;
            setLeaveMotionFrozen(false);
            // Leave-restore can open the folder before the grid has its final
            // content rect; re-anchor glass + title once HOME is interactive.
            refreshOpenFolderChrome();
            if (m_grid) {
                m_grid->setLayoutMode(m_appLayoutMode, false);
                configureDynamicLineNavigation();
            }
            // Splash blocks focusRoot() the whole hold/fade. Re-pin a title
            // when input returns so D-pad isn't stranded on chrome/spatial.
            {
                std::uint64_t pin = 0;
                if (auto* cur = focusManager().current();
                    cur && cur->tag() == "glossy_icon")
                    pin = static_cast<GlossyIcon*>(cur)->titleId();
                if (pin == 0 && m_grid) {
                    const int idx = m_grid->focusedGlobalIndex();
                    if (idx >= 0 && idx < m_model.count())
                        pin = m_model.at(idx).titleId;
                }
                ensureHomeTitleFocus(pin);
                if (m_appLayoutMode == AppLayoutMode::DynamicLine && m_grid) {
                    for (auto& icon : m_grid->allIcons()) {
                        if (icon)
                            icon->forceVisible();
                    }
                }
                updateCursor();
                DebugLog::log("[focus] splash handoff pin=%016lX cur=%s gridIdx=%d",
                              static_cast<unsigned long>(pin),
                              focusManager().current()
                                  ? focusManager().current()->tag().c_str()
                                  : "<none>",
                              m_grid ? m_grid->focusedGlobalIndex() : -1);
            }
            // Soft-land then pace the deferred burst: tight streamer → widen →
            // paced hero preload → peeks last (logs showed all three dumping
            // together when the old 2s gate opened).
            if (preferSingleRowPosters()) {
                m_deferBackdropPreloadSec = 2.0f;
                m_returnPreloadPaceSec = 4.0f; // paced heroes past defer
                m_iconStreamer.startSoftLanding(2.0f, 2, 1.75f, 3);
                if (m_steamGridDbBackdrop)
                    m_steamGridDbBackdrop->setPreloadMinInterval(0.45f);
            }
            if (m_armFolderCoverFillAfterSplash) {
                m_armFolderCoverFillAfterSplash = false;
                // Composites are disk/async — start immediately (no soft-land wait).
                m_pendingFolderCoverFill = false;
                m_folderCoverFillDelayFrames = 0;
                m_pendingFolderCoverNext = 0;
                m_pendingFolderCoverSkipRadius = -1;
                warmFolderCompositesEarly();
            }
            // Splash is gone — stop treating later rebuilds as soft-return.
            m_fastReturnRequested = false;
            const auto handoffArt = std::chrono::steady_clock::now();
            showFocusedSteamGridDbArtwork();
            DebugLog::log("[leave] splash handoff complete art=%ldms fillDelay=%d softLand=2.00+1.75 pace=%.2f",
                          static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - handoffArt).count()),
                          m_folderCoverFillDelayFrames,
                          m_returnPreloadPaceSec);
        }
    }
}

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
    if (fastReturn)
        m_iconStreamer.startBurst(1.5f);

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
                      "%s failed.\nCommand: %u ┬╖ Request: %lu\nResult: 0x%X ┬╖ Module: %u ┬╖ Description: %u",
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
    if (m_configSaveFuture.valid()) {
        m_configSaveFuture.wait();
        try { m_configSaveFuture.get(); } catch (...) {}
    }
    if (m_configSaveDeferred) {
        m_configSaveDeferred = false;
        m_configSaveFuture = m_threadPool.submit([config = m_config]() {
            config.save();
        });
        m_configSaveFuture.wait();
        try { m_configSaveFuture.get(); } catch (...) {}
    }
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
    std::string gameCardPath = std::string(SD_ASSETS) + "/icons/default/gamecard.png";
    if (m_gameCardTex.loadFromFile(app().gpu(), app().renderer(), gameCardPath))
        m_loadedGameCardPath = gameCardPath;

    m_arrowTexLeft.loadFromFile(app().gpu(), app().renderer(),
                                std::string(SD_ASSETS) + "/icons/default/page_arrow_left.png");
    m_arrowTexRight.loadFromFile(app().gpu(), app().renderer(),
                                 std::string(SD_ASSETS) + "/icons/default/page_arrow_right.png");
    m_batteryConsoleTex.loadFromFile(app().gpu(), app().renderer(),
                                     std::string(SD_ASSETS) + "/icons/default/widget_battery_switch.png");
    m_batteryJoyconLeftTex.loadFromFile(app().gpu(), app().renderer(),
                                        std::string(SD_ASSETS) + "/icons/default/widget_battery_joycon_left.png");
    m_batteryJoyconRightTex.loadFromFile(app().gpu(), app().renderer(),
                                         std::string(SD_ASSETS) + "/icons/default/widget_battery_joycon_right.png");
}

void WiiUMenuApp::loadProfiles(bool loadImmediately) {
    m_profiles.clear();
    m_profilesComplete = false;
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
            loadNextProfile();
        finishProfileLoading();
    } else if (!m_pendingProfileUids.empty()) {
        m_deferredProfileFrames = 1;
        DebugLog::log("[profiles] %d avatars deferred until after first frame", count);
    } else {
        finishProfileLoading();
    }
}

void WiiUMenuApp::finishProfileLoading() {
    m_profilesComplete = true;
    refreshProfileLockButton();
    DebugLog::log("[profiles] loaded count=%d", static_cast<int>(m_profiles.size()));
}

void WiiUMenuApp::refreshProfileLockButton() {
    std::optional<AccountUid> locked;
    AccountUid lockedUid{};
    if (m_config.defaultProfileEnabled &&
        hexToAccountUid(m_config.defaultProfileUid, lockedUid))
        locked = lockedUid;

    if (m_profileLock) {
        const HomeProfile* match = nullptr;
        if (locked) {
            for (const auto& profile : m_profiles) {
                if (profile.uid.uid[0] == locked->uid[0] &&
                    profile.uid.uid[1] == locked->uid[1]) {
                    match = &profile;
                    break;
                }
            }
        }
        // A locked profile that was deleted from System Settings falls back
        // to the "ask" state without rewriting the user's config.
        if (match)
            m_profileLock->setLockedProfile(match->avatar, match->nickname);
        else
            m_profileLock->clearLockedProfile();
    }

    if (m_profileCarousel) {
        m_profileCarousel->setProfiles(m_profiles,
                                       m_profilesComplete && m_profiles.size() < 8);
        m_profileCarousel->setLockedUid(locked);
    }
}

void WiiUMenuApp::setLockedProfile(std::optional<AccountUid> uid) {
    m_config.defaultProfileEnabled = uid.has_value();
    m_config.defaultProfileUid = uid ? accountUidToHex(*uid) : std::string();
    m_config.save();
    refreshProfileLockButton();
    DebugLog::log("[profiles] launch profile %s", uid ? "locked" : "cleared");
}

void WiiUMenuApp::placeLayoutFlipButton() {
    if (!m_layoutFlip || !m_clock)
        return;
    constexpr float kSize = 52.f;
    constexpr float kGap = 12.f;
    const nxui::Rect clock = m_clock->rect();
    const float x = clock.x + clock.width + kGap;
    const float y = clock.y + (clock.height - kSize) * 0.5f;
    m_layoutFlip->setRect({x, y, kSize, kSize});
    m_layoutFlip->setVisible(m_topHud && m_topHud->isVisible() && m_openFolderId == 0);
}

void WiiUMenuApp::restoreLayoutFlipFocusIfNeeded() {
    if (!m_retainLayoutFlipFocus || !m_layoutFlip)
        return;
    // Wait out the layout morph so focus isn't stolen by icon rebuilds.
    if (m_grid && m_grid->isLayoutMorphing())
        return;
    if (m_layoutTransition.kind != LayoutTransitionKind::Idle)
        return;
    placeLayoutFlipButton();
    // Rebuild strip neighbors (flip LEFT/RIGHT) after the morph settles.
    wireProfileLockNavigation();
    if (m_layoutFlip->isVisible()) {
        focusManager().setFocus(m_layoutFlip.get());
        updateCursor();
    }
    m_retainLayoutFlipFocus = false;
}

void WiiUMenuApp::focusHomeGridFallback() {
    if (!m_grid || m_navigator.route() != switchu::navigation::Route::Home)
        return;
    if (auto* target = m_grid->focusManager().current();
        target && target->isFocusable() && target->isVisible()) {
        focusManager().setFocus(target);
        return;
    }
    for (int i = 0; i < static_cast<int>(m_grid->allIcons().size()); ++i) {
        if (!m_grid->focusGlobalIndex(i, true))
            continue;
        if (auto* cur = m_grid->focusManager().current()) {
            focusManager().setFocus(cur);
            return;
        }
    }
}

std::uint64_t WiiUMenuApp::mostRecentlyOpenedTitleId() const {
    std::uint64_t bestId = 0;
    std::uint64_t bestSeq = 0;
    for (const auto& entry : m_config.lastOpened) {
        if (entry.second >= bestSeq && entry.first != 0) {
            bestSeq = entry.second;
            bestId = entry.first;
        }
    }
    return bestId;
}

void WiiUMenuApp::ensureHomeTitleFocus(std::uint64_t preferredTitleId) {
    if (!m_grid)
        return;

    auto focusOk = [this]() {
        auto* cur = focusManager().current();
        return cur && cur->isFocusable() && cur->isVisible()
            && isCurrentFocusableWidget(cur);
    };

    if (preferredTitleId != 0 && focusTitle(preferredTitleId, true) && focusOk())
        return;

#ifdef SWITCHU_MENU
    const std::uint64_t suspended = m_launcher.suspendedTitleId();
    if (suspended != 0 && focusTitle(suspended, true) && focusOk())
        return;
#endif

    const std::uint64_t recent = mostRecentlyOpenedTitleId();
    if (recent != 0 && focusTitle(recent, true) && focusOk())
        return;

    if (auto* gridCur = m_grid->focusManager().current();
        gridCur && gridCur->isFocusable()) {
        focusManager().setFocus(gridCur);
        if (focusOk())
            return;
    }

    for (int i = 0; i < static_cast<int>(m_grid->allIcons().size()); ++i) {
        if (m_grid->focusGlobalIndex(i, true)) {
            if (auto* cur = m_grid->focusManager().current()) {
                focusManager().setFocus(cur);
                if (focusOk())
                    return;
            }
        }
    }
    DebugLog::log("[focus] ensureHomeTitleFocus failed layout=%d",
                  static_cast<int>(m_appLayoutMode));
}

void WiiUMenuApp::wireLayoutFlipNavigation() {
    if (!m_layoutFlip)
        return;
    auto* flip = m_layoutFlip.get();
    // UP/DOWN only — LEFT/RIGHT neighbors are owned by wireProfileLockNavigation
    // so this helper cannot clobber the chrome strip.
    flip->setCustomNavigation(nxui::FocusDirection::UP, flip);
    flip->removeAction(static_cast<uint64_t>(nxui::Button::DDown));
    flip->removeAction(static_cast<uint64_t>(nxui::Button::LStickD));
    flip->removeAction(static_cast<uint64_t>(nxui::Button::RStickD));
    flip->addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        focusHomeGridFallback();
    });
}

void WiiUMenuApp::wireProfileLockNavigation() {
    if (!m_profileLock)
        return;
    auto* lock = m_profileLock.get();
    const bool dynamicLine = m_appLayoutMode == AppLayoutMode::DynamicLine;
    const auto& leftButtons = m_sidebar.leftButtons();
    const auto& rightButtons = m_sidebar.rightButtons();
    auto* flip = (m_layoutFlip && m_layoutFlip->isVisible()) ? m_layoutFlip.get() : nullptr;
    auto* battery = (m_battery && m_battery->isVisible()) ? m_battery.get() : nullptr;
    nxui::Widget* firstLeft = leftButtons.empty() ? nullptr : leftButtons.front().get();
    nxui::Widget* lastLeft = leftButtons.empty() ? nullptr : leftButtons.back().get();
    nxui::Widget* firstRight = rightButtons.empty() ? nullptr : rightButtons.front().get();
    nxui::Widget* lastRight = rightButtons.empty() ? nullptr : rightButtons.back().get();

    // Change View is the chrome hub: titles jump here on UP in both layouts.
    nxui::Widget* chromeUp = flip
        ? static_cast<nxui::Widget*>(flip)
        : static_cast<nxui::Widget*>(lock);

    lock->setCustomNavigation(nxui::FocusDirection::LEFT, lock);
    lock->setCustomNavigation(nxui::FocusDirection::UP, lock);
    lock->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
    lock->removeAction(static_cast<uint64_t>(nxui::Button::DDown));
    lock->removeAction(static_cast<uint64_t>(nxui::Button::LStickD));
    lock->removeAction(static_cast<uint64_t>(nxui::Button::RStickD));

    if (battery) {
        battery->setCustomNavigation(nxui::FocusDirection::UP, battery);
        battery->setCustomNavigation(nxui::FocusDirection::RIGHT, battery);
        battery->removeAction(static_cast<uint64_t>(nxui::Button::DDown));
        battery->removeAction(static_cast<uint64_t>(nxui::Button::LStickD));
        battery->removeAction(static_cast<uint64_t>(nxui::Button::RStickD));
    }

    wireLayoutFlipNavigation();

    if (dynamicLine) {
        // Strip: [profile] → [left…] → [flip] → [right…] → [battery]
        nxui::Widget* lockRight = firstLeft
            ? firstLeft
            : (flip ? static_cast<nxui::Widget*>(flip)
                    : (battery ? static_cast<nxui::Widget*>(battery)
                               : static_cast<nxui::Widget*>(lock)));
        lock->setCustomNavigation(nxui::FocusDirection::RIGHT, lockRight);
        m_sidebar.setDynamicLineProfileTarget(lock);

        nxui::Widget* afterLeft = flip
            ? static_cast<nxui::Widget*>(flip)
            : (firstRight ? firstRight
                          : (battery ? static_cast<nxui::Widget*>(battery) : lastLeft));
        if (lastLeft && afterLeft)
            lastLeft->setCustomNavigation(nxui::FocusDirection::RIGHT, afterLeft);

        if (flip) {
            flip->setCustomNavigation(nxui::FocusDirection::LEFT,
                                      lastLeft ? lastLeft : static_cast<nxui::Widget*>(lock));
            nxui::Widget* flipRight = firstRight
                ? firstRight
                : (battery ? static_cast<nxui::Widget*>(battery)
                           : static_cast<nxui::Widget*>(flip));
            flip->setCustomNavigation(nxui::FocusDirection::RIGHT, flipRight);
            if (firstRight)
                firstRight->setCustomNavigation(nxui::FocusDirection::LEFT, flip);
        }

        if (battery) {
            nxui::Widget* batteryLeft = lastRight
                ? lastRight
                : (flip ? static_cast<nxui::Widget*>(flip)
                        : static_cast<nxui::Widget*>(lock));
            battery->setCustomNavigation(nxui::FocusDirection::LEFT, batteryLeft);
            if (lastRight)
                lastRight->setCustomNavigation(nxui::FocusDirection::RIGHT, battery);
            battery->addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
                focusHomeGridFallback();
            });
        }

        lock->addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
            focusHomeGridFallback();
        });
        if (m_grid)
            m_grid->setDynamicLineUpTarget(chromeUp);
        m_sidebar.setDynamicLineUpTarget(nullptr);
    } else {
        // Grid chrome: [profile] ↔ [flip] ↔ [battery]
        // DOWN from profile → left column; DOWN from battery → right column.
        m_sidebar.setDynamicLineProfileTarget(nullptr);
        lock->setCustomNavigation(nxui::FocusDirection::RIGHT,
                                  flip ? static_cast<nxui::Widget*>(flip)
                                       : (battery ? static_cast<nxui::Widget*>(battery)
                                                  : nullptr));
        if (flip) {
            flip->setCustomNavigation(nxui::FocusDirection::LEFT, lock);
            flip->setCustomNavigation(nxui::FocusDirection::RIGHT,
                                      battery ? static_cast<nxui::Widget*>(battery)
                                              : static_cast<nxui::Widget*>(flip));
        }
        if (battery) {
            battery->setCustomNavigation(nxui::FocusDirection::LEFT,
                                         flip ? static_cast<nxui::Widget*>(flip)
                                              : static_cast<nxui::Widget*>(lock));
            if (firstRight) {
                battery->setCustomNavigation(nxui::FocusDirection::DOWN, firstRight);
                firstRight->setCustomNavigation(nxui::FocusDirection::UP, battery);
            } else {
                battery->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
                battery->addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
                    focusHomeGridFallback();
                });
            }
        }
        if (firstLeft) {
            lock->setCustomNavigation(nxui::FocusDirection::DOWN, firstLeft);
            firstLeft->setCustomNavigation(nxui::FocusDirection::UP, lock);
        }
        if (m_grid)
            m_grid->setDynamicLineUpTarget(chromeUp);
    }
}

void WiiUMenuApp::loadNextProfile() {
    if (m_pendingProfileIndex >= m_pendingProfileUids.size())
        return;

    const AccountUid uid = m_pendingProfileUids[m_pendingProfileIndex++];
    AccountProfile profile{};
    if (R_SUCCEEDED(accountGetProfile(&profile, uid))) {
        HomeProfile entry;
        entry.uid = uid;

        AccountProfileBase base{};
        AccountUserData userData{};
        if (R_SUCCEEDED(accountProfileGet(&profile, &userData, &base)))
            entry.nickname = base.nickname;

        u32 imgSize = 0;
        if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imgSize)) && imgSize > 0) {
            std::vector<uint8_t> imgBuf(imgSize);
            u32 realSize = 0;
            if (R_SUCCEEDED(accountProfileLoadImage(&profile, imgBuf.data(), imgSize, &realSize))
                    && realSize > 0) {
                auto avatar = std::make_shared<nxui::Texture>();
                if (avatar->loadFromMemory(app().gpu(), app().renderer(),
                                           imgBuf.data(), realSize, 256))
                    entry.avatar = std::move(avatar);
            }
        }

        accountProfileClose(&profile);
        m_profiles.push_back(std::move(entry));
        refreshProfileLockButton();
    }

    if (m_pendingProfileIndex >= m_pendingProfileUids.size())
        finishProfileLoading();
}

WiiUMenuApp::GridLayoutMetrics WiiUMenuApp::computeGridLayoutMetrics() const {
    const int cols = std::clamp(m_config.gridColumns, 1, 8);
    const int rows = std::clamp(m_config.gridRows, 1, 5);
    return computeGridLayoutMetrics(cols, rows);
}

WiiUMenuApp::GridLayoutMetrics WiiUMenuApp::computeGridLayoutMetrics(int cols,
                                                                      int rows) const {
    cols = std::clamp(cols, 1, 8);
    rows = std::clamp(rows, 1, 5);

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

    const int cols = std::clamp(m_config.gridColumns, 1, 8);
    const int rows = std::clamp(m_config.gridRows, 1, 5);
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
            if (rebuiltModel.at(i).isFolder()) {
                const auto& entry = rebuiltModel.at(i);
                oldIcons[i]->setFolderPreviewCount(entry.folderPreviewCount);
                oldIcons[i]->setFolderColorIndex(entry.folderColorIndex);
                oldIcons[i]->setFolderCoverTitleId(entry.folderCoverTitleId);
                oldIcons[i]->setFolderStyleIndex(m_config.folderStyle);
                oldIcons[i]->setFolderShowCover(m_config.folderShowCover);
                oldIcons[i]->setPosterFrame(preferSingleRowPosters());
            }
        } else {
            auto icon = makeIcon(rebuiltModel.at(i));
            icon->setBaseColor(m_theme.iconDefault);
            icons.push_back(std::move(icon));
        }
    }

    m_model = std::move(rebuiltModel);
    syncSingleRowPosterPresentation(false);
    std::vector<uint64_t> reflowedTitleIds;
    reflowedTitleIds.reserve((size_t)std::max(0, m_model.count()));
    for (int i = 0; i < m_model.count(); ++i)
        reflowedTitleIds.push_back(m_model.at(i).titleId);
    m_iconStreamer.reconcileTitleIds(reflowedTitleIds);

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();
    m_grid->setSlideTransition(m_config.pageTransitionMode == 0);
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
    const int cols = std::clamp(m_config.gridColumns, 1, 8);
    const int rows = std::clamp(m_config.gridRows, 1, 5);
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
        item.folderCoverTitleId = resolveFolderCoverTitleId(
            folder, preferSingleRowPosters());
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

        const std::size_t free = firstFreeSlot(
            slots, cols, perPage, m_appLayoutMode == AppLayoutMode::Grid,
            [&](std::uint64_t id) {
                auto it = byId.find(id);
                return it == byId.end()
                    ? std::pair<int, int>{1, 1}
                    : std::pair<int, int>{std::max(1, it->second.widgetColumns),
                                          std::max(1, it->second.widgetRows)};
            });
        if (free < slots.size())
            slots[free] = itemId;
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
        entry.folderCoverTitleId = resolveFolderCoverTitleId(
            folder, preferSingleRowPosters());
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

    const int perPage = std::max(1, std::clamp(m_config.gridColumns, 1, 8) *
                                     std::clamp(m_config.gridRows, 1, 5));
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
            const std::size_t free = firstFreeSlot(
                m_layoutSlots, std::clamp(m_config.gridColumns, 1, 8), perPage,
                m_appLayoutMode == AppLayoutMode::Grid,
                [&](std::uint64_t id) {
                    auto it = entries.find(id);
                    return it == entries.end()
                        ? std::pair<int, int>{1, 1}
                        : std::pair<int, int>{std::max(1, it->second.widgetColumns),
                                              std::max(1, it->second.widgetRows)};
                });
            if (free >= m_layoutSlots.size()) m_layoutSlots.push_back(pair.first);
            else m_layoutSlots[free] = pair.first;
            m_layoutDirty = true;
        }
    }
    const int minimum = perPage * kMinHomePages;
    const int rounded = ((std::max(minimum, static_cast<int>(m_layoutSlots.size())) + perPage - 1) / perPage) * perPage;
    if (static_cast<int>(m_layoutSlots.size()) < rounded) {
        m_layoutSlots.resize(rounded, 0);
        m_layoutDirty = true;
    }

    // Automatic views are only a projection of the personal layout. Folders
    // and widgets remain anchored (including every cell of a wide tile), while
    // applications fill the remaining cells in the requested order.
    std::vector<std::uint64_t> projected;
    if (m_config.sortMode != 0 && m_appLayoutMode == AppLayoutMode::Grid) {
        const int columns = std::clamp(m_config.gridColumns, 1, 8);
        const int gridRows = std::clamp(m_config.gridRows, 1, 5);
        // With sortLargeTiles, applications that use a bigger tile also join
        // the order instead of staying anchored where they were placed.
        const bool sortLarge = m_config.sortLargeTiles;
        projected.assign(m_layoutSlots.size(), 0);
        std::vector<bool> reserved(m_layoutSlots.size(), false);

        for (std::size_t index = 0; index < m_layoutSlots.size(); ++index) {
            const auto stored = m_layoutSlots[index];
            const auto found = entries.find(stored);
            const bool movableApplication = found != entries.end() &&
                found->second.isApplication() &&
                (sortLarge || (found->second.widgetColumns == 1 &&
                               found->second.widgetRows == 1));
            if (stored == 0 || found == entries.end() || movableApplication)
                continue;
            projected[index] = stored;
            const int spanColumns = std::max(1, found->second.widgetColumns);
            const int spanRows = std::max(1, found->second.widgetRows);
            for (int dy = 0; dy < spanRows; ++dy) {
                for (int dx = 0; dx < spanColumns; ++dx) {
                    const std::size_t cell = index +
                        static_cast<std::size_t>(dy * columns + dx);
                    if (cell < reserved.size()) reserved[cell] = true;
                }
            }
        }

        std::unordered_map<std::uint64_t, int> personalRank;
        int rank = 0;
        for (const auto stored : m_layoutSlots) {
            if (stored != 0 && !personalRank.count(stored))
                personalRank.emplace(stored, rank++);
        }
        std::vector<std::uint64_t> applications;
        applications.reserve(entries.size());
        for (const auto& pair : entries) {
            if (pair.second.isApplication() &&
                (sortLarge || (pair.second.widgetColumns == 1 &&
                               pair.second.widgetRows == 1)))
                applications.push_back(pair.first);
        }
        std::sort(applications.begin(), applications.end(),
                  [&](const auto left, const auto right) {
            const auto leftIt = personalRank.find(left);
            const auto rightIt = personalRank.find(right);
            const int leftRank = leftIt == personalRank.end()
                ? std::numeric_limits<int>::max() : leftIt->second;
            const int rightRank = rightIt == personalRank.end()
                ? std::numeric_limits<int>::max() : rightIt->second;
            return leftRank != rightRank ? leftRank < rightRank : left < right;
        });

        const int mode = m_config.sortMode;
        std::stable_sort(applications.begin(), applications.end(),
                         [&](const auto left, const auto right) {
            if (mode == 2) {
                const auto leftOpened = m_config.lastOpenedAt(left);
                const auto rightOpened = m_config.lastOpenedAt(right);
                return leftOpened != rightOpened && leftOpened > rightOpened;
            }
            const auto& leftTitle = entries.at(left).title;
            const auto& rightTitle = entries.at(right).title;
            const std::size_t shared = std::min(leftTitle.size(), rightTitle.size());
            for (std::size_t i = 0; i < shared; ++i) {
                const auto leftChar = static_cast<unsigned char>(
                    std::tolower(static_cast<unsigned char>(leftTitle[i])));
                const auto rightChar = static_cast<unsigned char>(
                    std::tolower(static_cast<unsigned char>(rightTitle[i])));
                if (leftChar != rightChar) return leftChar < rightChar;
            }
            return leftTitle.size() < rightTitle.size();
        });

        if (sortLarge) {
            // First fit: each application takes the first spot where its whole
            // tile fits inside one page; new pages are appended when needed.
            auto fitsAt = [&](std::size_t index, int spanColumns, int spanRows) {
                const int local = static_cast<int>(index % static_cast<std::size_t>(perPage));
                if (local % columns + spanColumns > columns ||
                    local / columns + spanRows > gridRows)
                    return false;
                for (int dy = 0; dy < spanRows; ++dy) {
                    for (int dx = 0; dx < spanColumns; ++dx) {
                        const std::size_t cell = index +
                            static_cast<std::size_t>(dy * columns + dx);
                        if (cell >= reserved.size() || reserved[cell]) return false;
                    }
                }
                return true;
            };
            for (const auto id : applications) {
                const auto& entry = entries.at(id);
                const int spanColumns = std::clamp(entry.widgetColumns, 1, columns);
                const int spanRows = std::clamp(entry.widgetRows, 1, gridRows);
                for (std::size_t index = 0;; ++index) {
                    if (index >= projected.size()) {
                        projected.resize(projected.size() + static_cast<std::size_t>(perPage), 0);
                        reserved.resize(projected.size(), false);
                    }
                    if (!fitsAt(index, spanColumns, spanRows)) continue;
                    projected[index] = id;
                    for (int dy = 0; dy < spanRows; ++dy)
                        for (int dx = 0; dx < spanColumns; ++dx)
                            reserved[index + static_cast<std::size_t>(dy * columns + dx)] = true;
                    break;
                }
            }
        } else {
            std::size_t next = 0;
            for (std::size_t index = 0;
                 index < projected.size() && next < applications.size(); ++index) {
                if (!reserved[index]) projected[index] = applications[next++];
            }
            while (next < applications.size()) projected.push_back(applications[next++]);
        }
    }
    const auto& displaySlots = projected.empty() ? m_layoutSlots : projected;

    const int columns = std::clamp(m_config.gridColumns, 1, 8);
    const int rows = std::clamp(m_config.gridRows, 1, 5);
    std::vector<int> coveredBy(displaySlots.size(), -1);
    if (m_appLayoutMode == AppLayoutMode::Grid) {
        for (int index = 0; index < static_cast<int>(displaySlots.size()); ++index) {
            auto found = entries.find(displaySlots[static_cast<std::size_t>(index)]);
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
                    if (cell >= static_cast<int>(displaySlots.size()) ||
                        (cell != index && displaySlots[static_cast<std::size_t>(cell)] != 0) ||
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

    for (int index = 0; index < static_cast<int>(displaySlots.size()); ++index) {
        const auto storedTitleId = displaySlots[static_cast<std::size_t>(index)];
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
        const auto titleId = displaySlots[static_cast<std::size_t>(index)];
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

void WiiUMenuApp::prepareRetainedImagePins(const GridModel& newModel) {
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
    // Retention only bridges a reflow/move of the current model. Keeping pins
    // from a closed folder or another page tree would keep every GIF frame
    // alive indefinitely.
    for (auto it = m_retainedImagePins.begin(); it != m_retainedImagePins.end();) {
        bool stillPresent = false;
        for (int i = 0; i < newModel.count(); ++i) {
            const auto& entry = newModel.at(i);
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
}

std::shared_ptr<GlossyIcon> WiiUMenuApp::makeDisplayIcon(const AppEntry& entry) {
    const auto isImageAssetWidget = [](switchu::widgets::WidgetType type) {
        return type == switchu::widgets::WidgetType::ImagePin ||
               type == switchu::widgets::WidgetType::RandomScreenshot;
    };
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
    return icon;
}

void WiiUMenuApp::completeDisplayModelApply(
    std::vector<std::shared_ptr<GlossyIcon>> icons,
    std::vector<std::uint64_t> titleIds,
    std::uint64_t focusId, bool animate,
    const IconAppearOptions& appear, bool instantFocus, bool morphLayout) {
    if (!m_grid)
        return;

    const bool preferPoster = preferSingleRowPosters();
    m_iconStreamer.setIconDataLoader([this](uint64_t titleId) {
        return AppListLoader::loadIconData(titleId, preferSingleRowPosters());
    });
    m_grid->setLinePosterAspect(preferPoster);

    m_iconStreamer.reconcileTitleIds(titleIds);
    // After reconcile (which may keep prior compressed bytes / GPU slots): if
    // poster vs icon preference flipped, mark art stale so tiles keep showing
    // the old kind during the morph and swap in the correct kind as it loads.
    const bool artKindChanged = m_iconStreamer.setPreferPosterArt(preferPoster);
    if (artKindChanged) {
        // Covers can be poster vs icon; peeks are always square icons — keep
        // them so grid↔line does not re-decode every folder stack.
        m_folderCoverCache.clear();
        m_folderPeekBorrowed.clear();
    }
    int columns = std::clamp(m_config.gridColumns, 1, 8);
    int rows = std::clamp(m_config.gridRows, 1, 5);
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
    // the edge columns are free to flip the page instead. Single-row never
    // pages — leave edge actions off or Grid→line morph keeps stale DRight.
    const bool inFolder = (m_openFolderId != 0);
    m_grid->setEdgePaging(inFolder && m_appLayoutMode == AppLayoutMode::Grid);
    m_grid->setSlideTransition(m_config.pageTransitionMode == 0);
    m_grid->setLayoutMode(m_appLayoutMode, morphLayout);
    m_grid->setup(std::move(icons), columns, rows, metrics.cellW, metrics.cellH,
                  metrics.padX, metrics.padY);
    // setup() rebuilds line/page geometry but does not touch morph state —
    // if we snapped layout on boot, keep it snapped after the icon rebuild.
    if (!morphLayout)
        m_grid->setLayoutMode(m_appLayoutMode, false);
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
        int page = 0, pageSize = 1;
        if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
            page = std::max(0, m_grid->focusedGlobalIndex());
            pageSize = 1;
        } else {
            page = m_grid->currentPage();
            pageSize = std::max(1, m_grid->iconsPerPage());
        }
        m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                     m_grid->allIcons());
        if (auto* target = m_grid->focusManager().current())
            focusManager().setFocus(target);
        updateCursor();
    });
    if (!focusTitle(focusId, instantFocus)) {
        if (auto* first = m_grid->focusManager().current()) {
            // A rebuilt model can leave the single-row carousel at the offset
            // of the previous tree; snapping avoids scrolling the whole row.
            if (instantFocus && m_appLayoutMode == AppLayoutMode::DynamicLine)
                m_grid->focusGlobalIndex(m_grid->focusedGlobalIndex(), true);
            focusManager().setFocus(first);
        }
    }
    // Compact line ↔ padded grid remaps indices; setLayoutMode ran on the old
    // list and could pin morphPage to the wrong grid page (cursor flies right).
    if (morphLayout)
        m_grid->rebindLayoutMorphAnchor();
    if (artKindChanged)
        m_iconStreamer.startBurst(0.85f);
    {
        int page = 0, pageSize = 1;
        if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
            page = std::max(0, m_grid->focusedGlobalIndex());
            pageSize = 1;
        } else {
            page = m_grid->currentPage();
            pageSize = std::max(1, m_grid->iconsPerPage());
        }
        m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }
    m_widgetAssetPage = -1;
    if (animate) m_grid->startAppearAnimation(appear);
    else for (auto& icon : m_grid->allIcons()) icon->forceVisible();
    if (m_openFolderId != 0) {
        const nxui::Rect panel = folderPanelRect();
        if (m_folderZoom) m_folderZoom->retarget(panel);
        placeFolderHeader(panel);
    }
    // Safety net for any rebuild while moving: never leave an out-of-range
    // placement index (that pins the ghost at the origin).
    if (m_editMode && (m_editTargetIndex < 0 || m_editTargetIndex >= m_model.count()))
        syncEditPlacementAfterModelChange(m_openFolderId != 0);
    syncEditJiggle();
    syncSingleRowPosterPresentation(false);
    // Navigation wiring waits until sidebars/profile exist (buildGrid calls
    // configureDynamicLineNavigation after those are ready).
    updateCursor();
}

void WiiUMenuApp::applyDisplayModel(GridModel model, std::uint64_t focusId, bool animate,
                                    const IconAppearOptions& appear, bool instantFocus,
                                    bool morphLayout) {
    if (!m_grid)
        return;
    prepareRetainedImagePins(model);
    m_model = std::move(model);
    std::vector<std::shared_ptr<GlossyIcon>> icons;
    std::vector<std::uint64_t> titleIds;
    icons.reserve(static_cast<std::size_t>(m_model.count()));
    titleIds.reserve(static_cast<std::size_t>(m_model.count()));
    for (int i = 0; i < m_model.count(); ++i) {
        const auto& entry = m_model.at(i);
        icons.push_back(makeDisplayIcon(entry));
        titleIds.push_back(entry.isApplication() ? entry.titleId : 0);
    }
    completeDisplayModelApply(std::move(icons), std::move(titleIds), focusId,
                              animate, appear, instantFocus, morphLayout);
}

void WiiUMenuApp::createTextEntry() {
    if (m_textEntry) return;
    m_textEntry = std::make_shared<TextEntryScreen>();
    m_textEntry->setFont(&m_fontNormal);
    m_textEntry->setSmallFont(&m_fontSmall);
    m_textEntry->setTheme(&m_theme);
    m_textEntry->onKeySfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_textEntry->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_textEntry->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_textEntry->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    if (m_overlayLayer) m_overlayLayer->addChild(m_textEntry);
}

void WiiUMenuApp::requestTextEntry(
    const std::string& title, const std::string& guide,
    const std::string& initial, int maxLength, bool password,
    std::function<void(const std::string&)> onAccept) {
    createTextEntry();
    if (!m_textEntry || m_textEntry->isActive()) return;
    if (m_overlayLayer) {
        m_overlayLayer->removeChild(m_textEntry.get());
        m_overlayLayer->addChild(m_textEntry);
    }
    nxui::Widget* returnFocus = focusManager().current();
    auto restoreFocus = [this, returnFocus]() {
        nxui::Widget* target = isCurrentFocusableWidget(returnFocus)
            ? returnFocus : (m_grid ? m_grid->focusManager().current() : nullptr);
        if (target) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(target);
            if (m_cursor) m_cursor->moveTo(target->focusRect().expanded(4.f), 0.f);
        }
    };
    m_textEntry->onAccept([restoreFocus, onAccept = std::move(onAccept)](
                              const std::string& value) {
        restoreFocus();
        if (onAccept) onAccept(value);
    });
    m_textEntry->onCancel(restoreFocus);
    m_audio.playSfx(Sfx::ModalShow);
    m_textEntry->show({title, guide, initial, std::max(1, maxLength), password});
    focusManager().setFocus(m_textEntry.get());
    if (m_cursor) m_cursor->setVisible(false);
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
    requestTextEntry(i18n.tr("folder.create", "Create folder"),
        i18n.tr("folder.name_guide", "Enter a folder name"), "", 48, false,
        [this, targetSlot](const std::string& typed) {
            if (typed.empty()) return;
            DebugLog::log("[folders] create requested slot=%d name=%s",
                          targetSlot, typed.c_str());
            const std::uint32_t id = m_folderStore.create(typed);
            if (id == 0 || !saveFoldersOrReport("create")) return;
            if (targetSlot >= 0 && targetSlot < static_cast<int>(m_layoutSlots.size()) &&
                m_layoutSlots[static_cast<std::size_t>(targetSlot)] == 0) {
                m_layoutSlots[static_cast<std::size_t>(targetSlot)] = folderTitleId(id);
                m_layoutDirty = true;
                saveMenuLayout();
            }
            m_audio.playSfx(Sfx::ConfirmPositive);
            applyDisplayModel(buildRootFolderModel(), folderTitleId(id), true);
        });
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
    const int columns = std::clamp(m_config.gridColumns, 1, 8);
    const int rows = std::clamp(m_config.gridRows, 1, 5);
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
    const int columns = std::clamp(m_config.gridColumns, 1, 8);
    const int rows = std::clamp(m_config.gridRows, 1, 5);
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
        const std::string label = std::to_string(size.columns) + "├ù"
            + std::to_string(size.rows)
            + (available ? std::string() : " ΓÇö " + i18n.tr("widget.no_space", "No space"));
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
            + std::to_string(size.columns) + "├ù" + std::to_string(size.rows);
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
    auto& i18n = nxui::I18n::instance();
    requestTextEntry(i18n.tr("folder.rename", "Rename"),
        i18n.tr("folder.rename_guide", "Rename folder"), oldName, 48, false,
        [this, folderId, oldName](const std::string& name) {
            if (name.empty() || name == oldName) return;
            m_folderStore.rename(folderId, name);
            if (!saveFoldersOrReport("rename")) return;
            if (m_openFolderId == folderId && m_folderHeaderLabel)
                m_folderHeaderLabel->setText(name);
            else
                applyDisplayModel(buildRootFolderModel(), folderTitleId(folderId), false);
        });
}

void WiiUMenuApp::requestOpenFolder(std::uint32_t folderId, std::uint64_t focusTitleId) {
    if (!m_folderStore.find(folderId) || m_folderCaptureRequested) return;
    m_requestedFolderId = folderId;
    m_folderOpenFocusTitleId = focusTitleId;
    m_folderCaptureRequested = true;
    m_folderCaptureReady = false;
    m_folderZoomOriginRect = folderTileRect(folderId);
    if (m_cursor) m_cursor->setVisible(false);
}

void WiiUMenuApp::flipPageFromEdge(int dir) {
    if (!m_grid || m_grid->isTransitioning())
        return;
    // Single-row scrolls by focus index; grid edge-paging must not run.
    if (m_appLayoutMode == AppLayoutMode::DynamicLine)
        return;
    const int target = m_grid->currentPage() + dir;
    if (target < 0 || target >= m_grid->totalPages()) {
        m_grid->bumpEdge(dir);
        return;
    }

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

void WiiUMenuApp::snapCursorToFocus() {
    if (m_cursor && focusManager().current())
        m_cursor->moveTo(focusManager().current()->focusRect().expanded(4.f), 0.01f);
    updateCursor();
}

nxui::Rect WiiUMenuApp::folderTileRect(std::uint32_t folderId) const {
    if (!m_grid)
        return {};
    const std::uint64_t tid = folderTitleId(folderId);
    const auto& icons = m_grid->allIcons();
    // Single-row mode keeps every icon as a child regardless of the page index,
    // so the page window below would miss a folder outside the first page and
    // the open animation would be skipped. Scan the whole row there.
    if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
        for (const auto& icon : icons) {
            if (icon && icon->titleId() == tid)
                return icon->focusRect();
        }
        return {};
    }
    const int perPage = m_grid->iconsPerPage();
    const int start   = m_grid->currentPage() * perPage;
    const int end     = std::min(start + perPage,
                                 static_cast<int>(icons.size()));
    for (int i = start; i < end; ++i) {
        const auto& icon = icons[static_cast<std::size_t>(i)];
        if (icon && icon->titleId() == tid)
            return icon->focusRect();
    }
    return {};
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

std::string WiiUMenuApp::sortModeLabel() const {
    auto& i18n = nxui::I18n::instance();
    switch (m_config.sortMode) {
        case 1: return i18n.tr("hint.sort_alpha", "A-Z");
        case 2: return i18n.tr("hint.sort_recent", "Recent");
        default: return i18n.tr("hint.sort_custom", "My order");
    }
}

void WiiUMenuApp::cycleSortMode() {
    if (m_editMode || m_openFolderId != 0 ||
        m_appLayoutMode == AppLayoutMode::DynamicLine)
        return;
    m_config.sortMode = (m_config.sortMode + 1) % 3;
    m_config.save();
    m_audio.playSfx(Sfx::Navigate);
    reflowHomeGrid();
}

void WiiUMenuApp::configureDynamicLineNavigation() {
    const bool dynamicLine = m_appLayoutMode == AppLayoutMode::DynamicLine;
    m_sidebar.setDynamicLineLayout(dynamicLine);
    placeLayoutFlipButton();

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
        // Titles UP → Change View (fallback: user) in both layouts.
        nxui::Widget* chromeUp = (m_layoutFlip && m_layoutFlip->isVisible())
            ? static_cast<nxui::Widget*>(m_layoutFlip.get())
            : static_cast<nxui::Widget*>(m_profileLock.get());
        m_grid->setDynamicLineUpTarget(chromeUp);
        m_grid->setDynamicLineDownTarget(nullptr);
    }

    if (!dynamicLine) {
        m_sidebar.setDynamicLineDownAction({});
        wireProfileLockNavigation();
        return;
    }

    wireProfileLockNavigation();

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

bool WiiUMenuApp::preferPosterForMode(AppLayoutMode mode) const {
    return mode == AppLayoutMode::DynamicLine
        && m_config.steamGridDbEnabled
        && m_config.steamGridDbShowPosterInDynamicLine;
}

void WiiUMenuApp::layoutPrefetchWindow(int& page, int& pageSize) const {
    page = 0;
    pageSize = 1;
    if (!m_grid)
        return;

    if (m_layoutTransition.kind == LayoutTransitionKind::PrefetchThenMorph) {
        // Still displaying the previous arrangement: prefetch the tiles that
        // will be visible after the morph lands.
        if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
            // Going to single-row while still showing the grid page.
            pageSize = std::max(1, m_grid->iconsPerPage());
            page = m_grid->currentPage();
        } else {
            int cols = std::clamp(m_config.gridColumns, 1, 8);
            int rows = std::clamp(m_config.gridRows, 1, 5);
            if (m_openFolderId != 0)
                std::tie(cols, rows) = folderGridDimensions(m_openFolderId);
            pageSize = std::max(1, cols * rows);
            const int focus = std::max(0, m_grid->focusedGlobalIndex());
            page = focus / pageSize;
        }
        return;
    }

    if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
        pageSize = 1;
        page = std::max(0, m_grid->focusedGlobalIndex());
    } else {
        pageSize = std::max(1, m_grid->iconsPerPage());
        page = m_grid->currentPage();
    }
}

void WiiUMenuApp::beginLayoutArtPrefetch() {
    if (!m_grid)
        return;

    // Only the focused title must be ready before the morph — neighbours can
    // lazy-load afterward. That keeps the wait to a single decode/upload.
    std::uint64_t focusId = m_layoutTransition.focusId;
    if (focusId == 0) {
        const int focus = m_grid->focusedGlobalIndex();
        const auto& icons = m_grid->allIcons();
        if (focus >= 0 && focus < static_cast<int>(icons.size())
            && icons[static_cast<std::size_t>(focus)])
            focusId = icons[static_cast<std::size_t>(focus)]->titleId();
        m_layoutTransition.focusId = focusId;
    }

    const bool preferPoster = preferSingleRowPosters();
    const bool hasPoster = focusId != 0 && SteamGridDbManager::hasPoster(focusId);
    std::error_code fitEc;
    const std::string fitCache = (focusId != 0)
        ? (SteamGridDbManager::posterPath(focusId) + ".fit320x480.rgba-cache")
        : std::string();
    const bool hasFitCache = !fitCache.empty()
        && std::filesystem::is_regular_file(fitCache, fitEc);
    auto morphSoon = [this]() { m_layoutTransition.commitNextFrame = true; };

    DebugLog::log("[layout-prefetch] begin focus=%016lX preferPoster=%d hasPoster=%d "
                  "fitCache=%d path=%s",
                  static_cast<unsigned long>(focusId), preferPoster ? 1 : 0,
                  hasPoster ? 1 : 0, hasFitCache ? 1 : 0,
                  fitCache.empty() ? "(none)" : fitCache.c_str());

    // No poster on disk for this title: nothing to wait for — morph now.
    if (preferPoster && focusId != 0 && !hasPoster) {
        DebugLog::log("[layout-prefetch] branch=morphSoon (no poster file)");
        morphSoon();
        return;
    }
    if (focusId == 0) {
        DebugLog::log("[layout-prefetch] branch=morphSoon (no focus)");
        morphSoon();
        return;
    }

    // Fit cache already on SD: kick a background warm but don't stall the morph.
    if (preferPoster && hasFitCache) {
        DebugLog::log("[layout-prefetch] branch=morphSoon (fit cache hit) + shadow warm");
        m_iconStreamer.startShadowPrefetch(
            preferPoster, {focusId},
            [preferPoster](uint64_t titleId) {
                return AppListLoader::loadIconData(titleId, preferPoster);
            });
        morphSoon();
        return;
    }

    DebugLog::log("[layout-prefetch] branch=waitShadow (decode/upload before morph)");
    m_iconStreamer.startShadowPrefetch(
        preferPoster, {focusId},
        [preferPoster](uint64_t titleId) {
            return AppListLoader::loadIconData(titleId, preferPoster);
        });
}

void WiiUMenuApp::beginVisibleArtWait() {
    if (!m_grid)
        return;
    m_layoutTransition = {};
    m_layoutTransition.kind = LayoutTransitionKind::WaitForVisibleArt;
    m_layoutTransition.visualMode = m_grid->layoutMode();
    m_layoutTransition.focusId = 0;
    m_layoutTransition.editing = false;
    m_layoutTransition.elapsed = 0.f;
    m_iconStreamer.startBurst(kLayoutArtPrefetchTimeout);
    int page = 0, pageSize = 1;
    layoutPrefetchWindow(page, pageSize);
    m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                 m_grid->allIcons());
}

bool WiiUMenuApp::tryCommitInPlaceLayoutMorph(std::uint64_t focused) {
    if (!m_grid || m_editMode)
        return false;
    // Entering single-row: drop empties/continuations and morph. Returning to
    // grid needs those slots recreated — fall through to a full rebuild.
    if (m_appLayoutMode != AppLayoutMode::DynamicLine)
        return false;

    // Compact the live grid model — avoid buildRootFolderModel / open-folder
    // rebuild (normalizeWidgetPlacements, covers, etc.) on the morph frame.
    GridModel model = compactDynamicLineEntries(m_model);

    const auto& prev = m_grid->allIcons();
    std::vector<std::shared_ptr<GlossyIcon>> icons;
    std::vector<std::uint64_t> titleIds;
    icons.reserve(static_cast<std::size_t>(model.count()));
    titleIds.reserve(static_cast<std::size_t>(model.count()));

    std::size_t pi = 0;
    for (int i = 0; i < model.count(); ++i) {
        const auto& entry = model.at(i);
        std::shared_ptr<GlossyIcon> match;
        while (pi < prev.size()) {
            const auto& cand = prev[pi++];
            if (!cand)
                continue;
            const auto kind = cand->entryKind();
            if (kind == GridEntryKind::Empty
                || kind == GridEntryKind::WidgetContinuation)
                continue;
            if (kind != entry.kind)
                return false;
            if ((entry.kind == GridEntryKind::Application
                 || entry.kind == GridEntryKind::Folder
                 || entry.kind == GridEntryKind::Widget)
                && cand->titleId() != entry.titleId)
                return false;
            match = cand;
            break;
        }
        if (!match)
            return false;
        icons.push_back(std::move(match));
        titleIds.push_back(entry.isApplication() ? entry.titleId : 0);
    }
    while (pi < prev.size()) {
        const auto& leftover = prev[pi++];
        if (leftover
            && leftover->entryKind() != GridEntryKind::Empty
            && leftover->entryKind() != GridEntryKind::WidgetContinuation)
            return false;
    }

    // Cover morph (~0.4s) + settle so hero/logo neighbour prefetch stays off
    // the first interactive beat after the transition.
    m_deferBackdropPreloadSec = 0.90f;

    if (m_steamGridDbBackdrop) {
        m_steamGridDbBackdrop->setLayoutMode(m_appLayoutMode);
        showFocusedSteamGridDbArtwork(false);
    }

    m_model = std::move(model);

    const bool preferPoster = preferSingleRowPosters();
    m_iconStreamer.setIconDataLoader([this](uint64_t titleId) {
        return AppListLoader::loadIconData(titleId, preferSingleRowPosters());
    });
    m_iconStreamer.reconcileTitleIds(titleIds);
    // Soft art-kind flip; focused poster lands via installShadowPrefetch.
    // Do NOT startBurst / onPageChanged here — neighbours wait for morph end.
    m_iconStreamer.setPreferPosterArt(preferPoster);

    m_grid->setLinePosterAspect(preferPoster);
    for (const auto& icon : icons) {
        if (!icon)
            continue;
        bool posterTile = false;
        if (preferPoster && icon->titleId() != 0) {
            if (icon->entryKind() == GridEntryKind::Folder)
                posterTile = true;
            else if (icon->entryKind() == GridEntryKind::Application)
                posterTile = SteamGridDbManager::hasPoster(icon->titleId());
        }
        icon->setPosterFrame(posterTile);
    }

    int columns = std::clamp(m_config.gridColumns, 1, 8);
    int rows = std::clamp(m_config.gridRows, 1, 5);
    if (m_openFolderId != 0)
        std::tie(columns, rows) = folderGridDimensions(m_openFolderId);
    auto metrics = computeGridLayoutMetrics(columns, rows);
    if (m_openFolderId != 0) {
        metrics.cellW *= 0.92f;
        metrics.cellH *= 0.92f;
        metrics.padX *= 0.92f;
        metrics.padY *= 0.92f;
    }

    m_grid->setEdgePaging(false);
    m_grid->setLayoutMode(AppLayoutMode::DynamicLine, true);
    m_grid->setup(std::move(icons), columns, rows, metrics.cellW, metrics.cellH,
                  metrics.padX, metrics.padY);

    // Focus first so nearby folder covers match the visible carousel window.
    if (focused != 0) {
        focusTitle(focused, m_openFolderId != 0);
    } else if (auto* first = m_grid->focusManager().current()) {
        focusManager().setFocus(first);
    }

    // Nearby folder peeks/covers on the morph frame (full 2×3, not a single
    // cover placeholder). Far folders catch up after morph. Prefer GPU-resident
    // member icons (borrowed); promote to owned peeks during the morph.
    constexpr int kFolderCoverWarmRadius = 4;
    applyFolderCoversToIcons(m_grid->focusedGlobalIndex(), kFolderCoverWarmRadius);
    m_pendingFolderCoverSkipRadius = kFolderCoverWarmRadius;
    DebugLog::log("[layout-prefetch] folder peeks borrowed=%u owned=%u",
                  static_cast<unsigned>(m_folderPeekBorrowed.size()),
                  static_cast<unsigned>(m_folderPeekCache.size()));

    m_iconStreamer.installShadowPrefetch(m_grid->allIcons());
    // Keep neighbour poster decodes running through the morph (fit RAM hits are
    // cheap). Cancelling here only forced a cold re-schedule when the morph ended.
    m_pendingNeighborArtBurst = true;

    // In-place morph skips completeDisplayModelApply, which normally retargets
    // the folder glass + title to the single-row band. Without this the large
    // multi-row plate from grid mode sticks around.
    if (m_openFolderId != 0) {
        const nxui::Rect panel = folderPanelRect();
        if (m_folderZoom) m_folderZoom->retarget(panel);
        placeFolderHeader(panel);
    }

    syncSteamGridDbLogoArea();
    configureDynamicLineNavigation();
    syncPageIndicator();
    updateCursor();
    updateHeroAwareChrome(true);
    return true;
}

void WiiUMenuApp::commitPendingLayoutVisuals() {
    if (!m_grid)
        return;

    const std::uint64_t focused = m_layoutTransition.focusId;
    const bool editing = m_layoutTransition.editing;
    const float waitedSec = m_layoutTransition.elapsed;
    const bool armed = m_layoutTransition.commitNextFrame;
    const bool shadowReady = m_iconStreamer.shadowPrefetchReady();
    m_layoutTransition = {};

    DebugLog::log("[layout-prefetch] commit start focus=%016lX waited=%.0fms "
                  "armed=%d shadowReady=%d",
                  static_cast<unsigned long>(focused), waitedSec * 1000.f,
                  armed ? 1 : 0, shadowReady ? 1 : 0);

    const auto commitStarted = std::chrono::steady_clock::now();

    if (!editing && tryCommitInPlaceLayoutMorph(focused)) {
        const auto commitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - commitStarted).count();
        DebugLog::log("[layout-prefetch] commit done inplace=%ldms",
                      static_cast<long>(commitMs));
        // Focus restore waits until morph settles (see update loop).
        return;
    }

    // Brighten hero/logo before the rebuild hitch so the first post-freeze
    // frame already has single-row backdrop (reuse cached GPU art — no reload).
    m_deferBackdropPreloadSec = 0.90f;
    if (m_steamGridDbBackdrop) {
        m_steamGridDbBackdrop->setLayoutMode(m_appLayoutMode);
        showFocusedSteamGridDbArtwork(false);
    }

    if (editing)
        detachEditSourceIcon();
    const auto rebuildStarted = std::chrono::steady_clock::now();
    if (m_openFolderId == 0) {
        applyDisplayModel(buildRootFolderModel(), focused, false, {}, false, true);
    } else {
        applyDisplayModel(buildOpenFolderModel(m_openFolderId), focused,
                          false, {}, true, true);
    }
    const auto rebuildMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - rebuildStarted).count();
    // Attach the focused title's prefetched art; everything else streams in.
    m_iconStreamer.installShadowPrefetch(m_grid->allIcons());
    // applyDisplayModel may have already queued neighbour poster decodes —
    // drop those so they pop in after the morph instead of mid-transition.
    m_iconStreamer.cancelPendingExcept(focused);
    if (editing)
        reattachEditSourceIcon();

    // Neighbours stream in after the morph finishes so the focused tile is
    // the only art swap that lands with the transition.
    m_pendingNeighborArtBurst = true;

    syncSteamGridDbLogoArea();
    configureDynamicLineNavigation();
    syncPageIndicator();
    updateCursor();

    const auto commitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - commitStarted).count();
    DebugLog::log("[layout-prefetch] commit done rebuild=%ldms total=%ldms",
                  static_cast<long>(rebuildMs), static_cast<long>(commitMs));
    // Rebuild morph still in flight — restore after it settles.
}

void WiiUMenuApp::updateLayoutTransition(float dt) {
    if (m_layoutTransition.kind == LayoutTransitionKind::Idle)
        return;
    if (!m_grid) {
        m_iconStreamer.cancelShadowPrefetch();
        m_layoutTransition = {};
        return;
    }

    m_layoutTransition.elapsed += dt;

    if (m_layoutTransition.kind == LayoutTransitionKind::PrefetchThenMorph) {
        // Prefetch GPU work runs in onAfterPresent so HOME can paint first.
        const bool armed = m_layoutTransition.commitNextFrame;
        const bool shadowReady = m_iconStreamer.shadowPrefetchReady();
        const bool ready = armed || shadowReady;
        const bool timedOut = m_layoutTransition.elapsed >= kLayoutArtPrefetchTimeout;
        if (!ready && !timedOut)
            return;

        DebugLog::log("[layout-prefetch] releasing wait=%.0fms reason=%s "
                      "armed=%d shadowReady=%d",
                      m_layoutTransition.elapsed * 1000.f,
                      timedOut && !ready ? "timeout"
                          : (armed ? "armed" : "shadowReady"),
                      armed ? 1 : 0, shadowReady ? 1 : 0);

        commitPendingLayoutVisuals();
        return;
    }

    int page = 0, pageSize = 1;
    layoutPrefetchWindow(page, pageSize);
    m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                 m_grid->allIcons());
    const bool ready = !m_iconStreamer.needsVisibleLoads(page, pageSize);
    const bool timedOut = m_layoutTransition.elapsed >= kLayoutArtPrefetchTimeout;
    if (!ready && !timedOut)
        return;
    m_layoutTransition = {};
}

void WiiUMenuApp::setAppLayoutMode(AppLayoutMode mode) {
    if (m_layoutTransition.kind != LayoutTransitionKind::Idle)
        return;
    if (m_appLayoutMode == mode && m_grid && m_grid->layoutMode() == mode)
        return;

    const bool artWillChange = preferPosterForMode(mode) != m_iconStreamer.preferPosterArt();
    std::uint64_t focused = 0;
    if (m_grid) {
        if (auto* current = m_grid->focusManager().current();
            current && current->tag() == "glossy_icon")
            focused = static_cast<GlossyIcon*>(current)->titleId();
    }
    const bool editing = m_editMode;

    DebugLog::log("[layout-prefetch] toggle from=%d to=%d artWillChange=%d "
                  "streamerPoster=%d destPoster=%d focus=%016lX",
                  static_cast<int>(m_appLayoutMode), static_cast<int>(mode),
                  artWillChange ? 1 : 0,
                  m_iconStreamer.preferPosterArt() ? 1 : 0,
                  preferPosterForMode(mode) ? 1 : 0,
                  static_cast<unsigned long>(focused));

    m_appLayoutMode = mode;
    if (m_layoutFlip)
        m_layoutFlip->setSingleRowActive(mode == AppLayoutMode::DynamicLine);
    // Inside a folder, layout flips are session-only — HOME stays locked to
    // whatever view was active when the folder opened (config.appLayoutMode).
    if (m_openFolderId == 0) {
        m_config.appLayoutMode = mode;
        // Never stall the layout morph on a prior config write. Reclaim a finished
        // future, then either save now or note a deferred save when one is in flight.
        if (m_configSaveFuture.valid()
            && m_configSaveFuture.wait_for(std::chrono::seconds(0))
                == std::future_status::ready) {
            try { m_configSaveFuture.get(); } catch (...) {}
        }
        if (!m_configSaveFuture.valid()) {
            m_configSaveFuture = m_threadPool.submit([config = m_config]() {
                config.save();
            });
        } else {
            m_configSaveDeferred = true;
        }
    }

    m_audio.playSfx(Sfx::ThemeToggle);
    auto& i18n = nxui::I18n::instance();
    const std::string announcement = (m_appLayoutMode == AppLayoutMode::DynamicLine)
        ? i18n.tr("accessibility.layout.dynamic_line", "Dynamic line mode")
        : i18n.tr("accessibility.layout.grid", "Grid mode");
    m_accessibility.announce(announcement, true, true);

    // Prefetch destination art while the CURRENT grid (and its hero) stay
    // on-screen. Backdrop/hero flip happens in commit; enter-single-row tries
    // an in-place morph (reuse icons) before falling back to a rebuild.
    if (m_grid && artWillChange) {
        DebugLog::log("[layout-prefetch] path=PrefetchThenMorph");
        m_layoutTransition.kind = LayoutTransitionKind::PrefetchThenMorph;
        m_layoutTransition.visualMode = m_grid->layoutMode();
        m_layoutTransition.focusId = focused;
        m_layoutTransition.editing = editing;
        m_layoutTransition.elapsed = 0.f;
        m_layoutTransition.commitNextFrame = false;
        beginLayoutArtPrefetch();
        return;
    }

    DebugLog::log("[layout-prefetch] path=immediate (no art-kind change)");
    if (m_steamGridDbBackdrop) {
        m_steamGridDbBackdrop->setLayoutMode(m_appLayoutMode);
        showFocusedSteamGridDbArtwork(true);
    }

    if (m_grid) {
        if (editing)
            detachEditSourceIcon();
        if (m_openFolderId == 0) {
            applyDisplayModel(buildRootFolderModel(), focused, false, {}, false, true);
        } else {
            applyDisplayModel(buildOpenFolderModel(m_openFolderId), focused,
                              false, {}, true, true);
        }
        if (editing)
            reattachEditSourceIcon();
        syncSteamGridDbLogoArea();
    }
    configureDynamicLineNavigation();
    syncPageIndicator();
    updateCursor();
    restoreLayoutFlipFocusIfNeeded();
}

bool WiiUMenuApp::preferSingleRowPosters() const {
    return preferPosterForMode(m_appLayoutMode);
}

std::uint64_t WiiUMenuApp::resolveFolderCoverTitleId(
    const switchu::folders::Folder& folder, bool preferPoster) const {
    const std::uint64_t pseudo = folderTitleId(folder.id);
    if (preferPoster && SteamGridDbManager::hasPoster(pseudo))
        return pseudo;
    if (SteamGridDbManager::hasIcon(pseudo))
        return pseudo;
    return switchu::folders::firstCoverTitleId(folder);
}

void WiiUMenuApp::syncSingleRowPosterPresentation(bool forceReloadIcons) {
    const bool preferPoster = preferSingleRowPosters();

    m_iconStreamer.setIconDataLoader([this](uint64_t titleId) {
        return AppListLoader::loadIconData(titleId, preferSingleRowPosters());
    });
    // Soft art-kind flip: keep current textures on screen and swap in the
    // correct kind as decodes finish (no blank flash).
    const bool artKindChanged = m_iconStreamer.setPreferPosterArt(preferPoster);

    if (!m_grid)
        return;

    const bool wasPoster = m_grid->linePosterAspect();
    m_grid->setLinePosterAspect(preferPoster);

    // Apps: tall only when they have poster art. Folders: always tall in
    // poster mode so they match neighbouring title posters (styles adapt).
    for (const auto& icon : m_grid->allIcons()) {
        if (!icon) continue;
        bool posterTile = false;
        if (preferPoster && icon->titleId() != 0) {
            if (icon->entryKind() == GridEntryKind::Folder)
                posterTile = true;
            else if (icon->entryKind() == GridEntryKind::Application)
                posterTile = SteamGridDbManager::hasPoster(icon->titleId());
        }
        icon->setPosterFrame(posterTile);
    }
    m_grid->invalidateLineLayout();

    if (artKindChanged || preferPoster != wasPoster) {
        m_folderCoverCache.clear();
        m_folderPeekBorrowed.clear();
        m_folderPeekCompositeCache.clear();
        for (auto& pending : m_pendingFolderPeekComposites) {
            if (pending.state)
                pending.state->cancelled.store(true);
        }
        m_pendingFolderPeekComposites.clear();
        m_folderPeekCompositeBakeQueued.clear();
        m_folderCoverFillDone.clear();
        m_lastFolderPeekWarmFocus = -1;
    }
    // Single-row: same soft-landing as slim morph — warm nearby folder peeks,
    // spread the rest across frames. Full sync here was the HOME-return hitch
    // (~1s+ decoding every folder stack before input).
    if (preferPoster && m_appLayoutMode == AppLayoutMode::DynamicLine) {
        // Only defer while leave-splash still covers HOME. Sticky
        // m_fastReturnRequested alone used to skip warm after closing a
        // folder mid-session → blank folder albums until focus moved.
        if (m_fastReturnRequested && leaveSplashActive()) {
            // Applet return / cold boot: zero sync peeks on the critical path.
            // Splash keeps covering HOME; fill starts at handoff / on focus.
            m_pendingFolderCoverFill = false;
            m_armFolderCoverFillAfterSplash = true;
            m_pendingFolderCoverSkipRadius = -1;
            m_folderCoverFillDone.clear();
            m_lastFolderPeekWarmFocus = -1;
        } else {
            constexpr int kFolderCoverWarmRadius = 4;
            applyFolderCoversToIcons(m_grid->focusedGlobalIndex(),
                                    kFolderCoverWarmRadius);
            m_pendingFolderCoverFill = true;
            m_pendingFolderCoverNext = 0;
            m_pendingFolderCoverSkipRadius = kFolderCoverWarmRadius;
        }
    } else {
        applyFolderCoversToIcons();
    }

    if (m_openFolderId != 0 && (artKindChanged || preferPoster != wasPoster)) {
        const nxui::Rect panel = folderPanelRect();
        if (m_folderZoom) m_folderZoom->retarget(panel);
        placeFolderHeader(panel);
    }

    // Applet return: skip the poster burst here — it was the remaining HOME
    // hitch (same pattern as the old morph). Deferred uploads + post-splash
    // fill warm the focused tile and neighbours gently.
    if ((artKindChanged || forceReloadIcons) && !m_fastReturnRequested) {
        int page = 0, pageSize = 1;
        if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
            page = std::max(0, m_grid->focusedGlobalIndex());
            pageSize = 1;
        } else {
            page = m_grid->currentPage();
            pageSize = std::max(1, m_grid->iconsPerPage());
        }
        if (artKindChanged) {
            m_iconStreamer.startBurst(0.85f);
            m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                         m_grid->allIcons());
        } else {
            m_iconStreamer.forceReload(page, pageSize, app().gpu(), app().renderer(),
                                       m_grid->allIcons());
        }
    }
}

void WiiUMenuApp::applyFolderDefaultViewOnOpen() {
    // 0 = Auto (keep current HOME layout), 1 = Grid, 2 = Single-row.
    AppLayoutMode desired = m_appLayoutMode;
    switch (std::clamp(m_config.folderDefaultView, 0, 2)) {
        case 1: desired = AppLayoutMode::Grid; break;
        case 2: desired = AppLayoutMode::DynamicLine; break;
        default: return;
    }
    if (desired == m_appLayoutMode)
        return;
    // Temporary for this folder session — HOME stays in config.appLayoutMode.
    m_appLayoutMode = desired;
    if (m_layoutFlip)
        m_layoutFlip->setSingleRowActive(desired == AppLayoutMode::DynamicLine);
    if (m_steamGridDbBackdrop)
        m_steamGridDbBackdrop->setLayoutMode(m_appLayoutMode);
}

void WiiUMenuApp::restoreHomeLayoutAfterFolder() {
    // Always return to the view that was active when the folder opened
    // (locked in config while in-folder flips stay local).
    if (m_appLayoutMode == m_config.appLayoutMode)
        return;
    m_appLayoutMode = m_config.appLayoutMode;
    if (m_layoutFlip)
        m_layoutFlip->setSingleRowActive(
            m_appLayoutMode == AppLayoutMode::DynamicLine);
    if (m_steamGridDbBackdrop)
        m_steamGridDbBackdrop->setLayoutMode(m_appLayoutMode);
}

void WiiUMenuApp::openCapturedFolder() {
    const auto* folder = m_folderStore.find(m_requestedFolderId);
    if (!folder) return;
    m_openFolderId = m_requestedFolderId;
    m_requestedFolderId = 0;
    m_folderCaptureReady = false;
    const bool refocus = (m_folderOpenFocusTitleId != 0);
    if (m_topHud) m_topHud->setVisible(false);
    placeLayoutFlipButton();
    if (m_leftSidebar) m_leftSidebar->setVisible(false);
    if (m_rightSidebar) m_rightSidebar->setVisible(false);
    if (m_pageIndicator)
        m_pageIndicator->setActiveColor(switchu::folders::colorForIndex(folder->colorIndex));
    if (m_folderHeaderLabel) {
        m_folderHeaderLabel->setText(folder->name);
        m_folderHeaderLabel->setTextColor(m_homeChrome.textPrimary);
    }
    applyFolderDefaultViewOnOpen();
    syncHeroBackdropPresentation();
    m_grid->setRect(kFolderGridRect);
    // Don't inherit the root page number into the folder grid.
    m_grid->setPage(0);
    const bool zoom = !refocus && m_folderZoom && m_folderZoomOriginRect.width > 0.f;
    // A folder opened mid-move may reflow the root grid underneath it, so the
    // tile it grew from is no longer a safe target to fly back into.
    m_folderOriginStale = m_editMode;
    if (m_folderBackdrop)
        m_folderBackdrop->show(refocus, m_folderZoomOriginRect, FolderZoom::kOpenDur);
    IconAppearOptions appear;
    if (zoom) {
        appear.baseDelay = kFolderZoomCascadeDelay;
        appear.stagger   = kFolderZoomCascadeStagger;
        appear.fromTile  = true;
        appear.origin    = m_folderZoomOriginRect;
    }
    // FocusManager::setGrid preserves the previous row/col, so opening a folder
    // without an explicit title would land on whatever cell matched the folder
    // tile's home placement. Default to the first title (top-left reading order).
    std::uint64_t openFocus = m_folderOpenFocusTitleId;
    if (openFocus == 0) {
        for (std::uint64_t titleId : folder->titleIds) {
            if (titleId != 0) {
                openFocus = titleId;
                break;
            }
        }
    }
    applyDisplayModel(buildOpenFolderModel(m_openFolderId), openFocus,
                      zoom, appear, true);
    m_folderOpenFocusTitleId = 0;
    // Single-row mode scrolls by index instead of paging, so a freshly opened
    // folder must be pinned to its first item explicitly — otherwise the
    // carousel keeps the offset it had on the root grid.
    if (!refocus && m_appLayoutMode == AppLayoutMode::DynamicLine && m_grid) {
        const auto& icons = m_grid->allIcons();
        for (int i = 0; i < static_cast<int>(icons.size()); ++i) {
            if (icons[static_cast<std::size_t>(i)]
                && icons[static_cast<std::size_t>(i)]->isFocusable()) {
                m_grid->focusGlobalIndex(i, true);
                break;
            }
        }
        if (auto* cur = m_grid->focusManager().current())
            focusManager().setFocus(cur);
        updateCursor();
    }
    // The panel is sized from the laid-out folder grid, so place it afterwards.
    if (m_folderZoom) {
        const nxui::Color tint = switchu::folders::colorForIndex(folder->colorIndex);
        if (zoom) {
            m_folderZoom->open(m_folderZoomOriginRect, folderPanelRect(), tint,
                               [this]() { snapCursorToFocus(); });
            if (m_cursor) m_cursor->setVisible(false);
        } else
            m_folderZoom->showStatic(folderPanelRect(), tint);
    }
    // The title lands just after the panel has reached its top edge.
    placeFolderHeader(folderPanelRect());
    if (zoom) {
        m_folderHeaderAnim.setImmediate(0.f);
        m_folderHeaderAnim.set(1.f, 0.30f, nxui::Easing::outCubic, 0.16f);
    } else {
        m_folderHeaderAnim.setImmediate(1.f);
    }
    syncFolderHeader();
    syncPageIndicator();
    if (m_editMode) {
        // Always re-anchor: the previous target was a root-grid index.
        syncEditPlacementAfterModelChange(true);
        reattachEditSourceIcon();
    }
    if (!refocus)
        m_audio.playSfx(Sfx::ModalShow);
}

void WiiUMenuApp::refreshOpenFolderChrome() {
    if (m_openFolderId == 0)
        return;
    // Never retarget FolderZoom mid-close: showStatic() drops the close
    // onDone callback, which leaves m_folderClosing stuck (empty folder +
    // soft-lock until a non-animated Home close). Leave-splash handoff can
    // call here right as the user presses B.
    if (m_folderClosing) {
        DebugLog::log("[folders] chrome refresh skipped — close in progress id=%u",
                      m_openFolderId);
        return;
    }
    const auto* folder = m_folderStore.find(m_openFolderId);
    if (!folder)
        return;

    // Leave-restore can open the folder before the grid has its final content
    // rect. Re-anchor the glass panel + title pill once HOME is interactive so
    // the folder name does not stay missing / off-screen.
    const nxui::Rect panel = folderPanelRect();
    if (m_folderZoom) {
        const nxui::Color tint = switchu::folders::colorForIndex(folder->colorIndex);
        m_folderZoom->showStatic(panel, tint);
    }
    if (m_folderHeaderLabel) {
        m_folderHeaderLabel->setText(folder->name);
        m_folderHeaderLabel->setTextColor(m_homeChrome.textPrimary);
    }
    placeFolderHeader(panel);
    m_folderHeaderAnim.setImmediate(1.f);
    syncFolderHeader();
    DebugLog::log("[folders] chrome refreshed id=%u name=%s headerY=%.0f",
                  m_openFolderId, folder->name.c_str(), m_folderHeaderRest.y);
}

nxui::Rect WiiUMenuApp::folderPanelRect() const {
    if (!m_grid)
        return kFolderGridRect;

    // Single-row mode has no page-sized grid to wrap: contentRect() would be
    // the whole screen band (huge). Hug the carousel instead, using the same
    // geometry as IconGrid::dynamicIconRect so the plate tracks the row.
    if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
        // Keep the glass plate tight to the icon row. Focused Posters may rise
        // outside it — the plate stays mostly transparent so hero art shows.
        const nxui::Rect grid = m_grid->rect();
        const float centerY = grid.y + grid.height * 0.66f;
        constexpr float baseCellH = 150.f;
        constexpr float focusScale = 1.36f;
        constexpr float padY = 16.f;
        const float focusH = baseCellH * focusScale;
        const float top = centerY - focusH * 0.5f - padY;
        const float bottom = centerY + focusH * 0.5f + padY;
        const float x = grid.x + 48.f;
        const float w = std::max(1.f, grid.width - 96.f);
        return {x, top, w, bottom - top};
    }

    const nxui::Rect content = m_grid->contentRect();
    // Tighten the vertical margin (evenly) when the tallest folder grid would
    // push the panel into the title pill.
    const float padY = std::clamp(kFolderPanelMaxBottom - content.bottom(),
                                  4.f, kFolderPanelPadY);
    return {content.x - kFolderPanelPadX, content.y - padY,
            content.width + kFolderPanelPadX * 2.f, content.height + padY * 2.f};
}

void WiiUMenuApp::placeFolderHeader(const nxui::Rect& panel) {
    // Anchored to the panel rather than the screen, so the largest folder
    // grid pushes the title up instead of sliding the panel under it.
    m_folderHeaderRest.y = std::max(kFolderHeaderMinY,
        panel.y - kFolderHeaderGap - m_folderHeaderRest.height);
    // Single-row: park the title on the left so it does not cover a Poster.
    if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
        m_folderHeaderRest.x = panel.x + 12.f;
        m_folderHeaderRest.width = 300.f;
    } else {
        m_folderHeaderRest.x = 410.f;
        m_folderHeaderRest.width = 460.f;
    }
    syncFolderHeader();
    syncSteamGridDbLogoArea();
}

void WiiUMenuApp::syncSteamGridDbLogoArea() {
    if (!m_steamGridDbBackdrop)
        return;
    m_steamGridDbBackdrop->setShowLogo(m_config.steamGridDbEnabled
                                       && m_config.steamGridDbShowLogoInDynamicLine);
    // Single-row: left gutter only when the focused tile is a tall poster.
    // Square icons keep the original centred logo band above the carousel.
    const nxui::Rect kCenteredLogo{370.f, 149.f, 540.f, 150.f};
    if (presentationLayoutMode() == AppLayoutMode::DynamicLine && m_grid) {
        const int focusIdx = m_grid->focusedGlobalIndex();
        const auto& icons = m_grid->allIcons();
        const bool posterFocus = focusIdx >= 0
            && focusIdx < static_cast<int>(icons.size())
            && icons[static_cast<std::size_t>(focusIdx)]
            && icons[static_cast<std::size_t>(focusIdx)]->usesPosterFrame();
        if (posterFocus) {
            nxui::Rect focus = m_grid->focusedDisplayRect();
            if (focus.width < 8.f || focus.height < 8.f)
                focus = {520.f, 200.f, 240.f, 360.f};
            constexpr float kGap = 18.f;
            constexpr float kEdge = 28.f;
            constexpr float kHudTop = 88.f;
            constexpr float kQuadH = 220.f; // top-left quadrant band
            const float left = kEdge;
            const float right = focus.x - kGap;
            m_steamGridDbBackdrop->setLogoArea(
                {left, kHudTop, std::max(40.f, right - left), kQuadH});
            return;
        }
    }
    m_steamGridDbBackdrop->setLogoArea(kCenteredLogo);
}

void WiiUMenuApp::syncFolderHeader() {
    if (!m_folderHeader || !m_folderHeaderLabel)
        return;
    const float t = std::clamp(m_folderHeaderAnim.value(), 0.f, 1.f);
    const bool shown = t > 0.005f || m_folderHeaderAnim.target() > 0.f;
    m_folderHeader->setVisible(shown);
    if (!shown)
        return;

    // Rises out of the panel's top edge while fading and growing in.
    const nxui::Rect& rest = m_folderHeaderRest;
    const float s = 0.92f + 0.08f * t;
    const float w = rest.width * s, h = rest.height * s;
    const nxui::Rect r{rest.x + (rest.width - w) * 0.5f,
                       rest.y + (rest.height - h) * 0.5f + (1.f - t) * kFolderHeaderSlide,
                       w, h};
    m_folderHeader->setRect(r);
    m_folderHeader->setOpacity(t);
    m_folderHeaderLabel->setRect({r.x + 18.f * s, r.y + 6.f * s,
                                  r.width - 36.f * s, r.height - 12.f * s});
    m_folderHeaderLabel->setOpacity(t);
}

void WiiUMenuApp::closeFolder(bool preserveEditMode, bool animated) {
    if (m_openFolderId == 0) return;
    const std::uint32_t oldId = m_openFolderId;
    const auto* old = m_folderStore.find(oldId);
    const nxui::Color tint = switchu::folders::colorForIndex(old ? old->colorIndex : 0);

    if (m_folderClosing) {
        // A second B/tap waits for the running close; anything else needs the
        // root grid back right now.
        if (animated) return;
        m_folderClosing = false;
        if (m_folderZoom) m_folderZoom->hide();
        m_folderHeaderAnim.setImmediate(0.f);
        finishCloseFolder(oldId, false);
        snapCursorToFocus();
        return;
    }

    DebugLog::log("[folders] close id=%u animated=%d", oldId, animated ? 1 : 0);

    // Clear in-folder SGDB art as soon as close begins so a quick open of
    // another folder never inherits the previous title's hero.
    if (m_steamGridDbBackdrop)
        m_steamGridDbBackdrop->showTitle(0, true);

    if (m_folderBackdrop) m_folderBackdrop->hide(FolderZoom::kCloseDur);

    // Mirror of the open: the folder's icons fly back into its tile while the
    // panel shrinks into it, and only then is the root grid rebuilt. Skip the
    // fly-away in single-row — disappearing tall posters leaves HOME blank
    // while root art streams back in.
    if (animated && !preserveEditMode && !m_folderOriginStale && m_grid &&
        m_folderZoom && m_folderZoomOriginRect.width > 0.f &&
        m_appLayoutMode == AppLayoutMode::Grid) {
        m_folderClosing = true;
        m_folderHeaderAnim.set(0.f, 0.16f, nxui::Easing::inCubic);
        if (m_cursor) m_cursor->setVisible(false);
        IconAppearOptions back;
        back.stagger = kFolderCloseStagger;
        back.origin  = m_folderZoomOriginRect;
        m_grid->startDisappearAnimation(back, FolderZoom::kCloseDur - kFolderCloseStagger);
        m_folderZoom->close(m_folderZoomOriginRect, tint, [this, oldId]() {
            m_folderClosing = false;
            finishCloseFolder(oldId, false);
            snapCursorToFocus();
        });
        m_audio.playSfx(Sfx::ModalHide);
        return;
    }

    finishCloseFolder(oldId, preserveEditMode);
    if (m_folderZoom) {
        const nxui::Rect tile = folderTileRect(oldId);
        if (tile.width > 0.f) {
            m_folderZoomOriginRect = tile;
            if (m_cursor) m_cursor->setVisible(false);
            m_folderZoom->close(tile, tint, [this]() { snapCursorToFocus(); });
        } else {
            m_folderZoom->hide();
        }
    }
    m_audio.playSfx(Sfx::ModalHide);
}

void WiiUMenuApp::finishCloseFolder(std::uint32_t oldId, bool preserveEditMode) {
    if (preserveEditMode) {
        detachEditSourceIcon();
        unbindEditActions();
    }
    m_openFolderId = 0;
    restoreHomeLayoutAfterFolder();
    syncHeroBackdropPresentation();
    // Sink back into the panel as it shrinks away.
    if (m_folderHeaderAnim.target() > 0.f)
        m_folderHeaderAnim.set(0.f, 0.16f, nxui::Easing::inCubic);
    if (m_topHud) m_topHud->setVisible(true);
    placeLayoutFlipButton();
    wireLayoutFlipNavigation();
    if (m_leftSidebar) m_leftSidebar->setVisible(true);
    if (m_rightSidebar) m_rightSidebar->setVisible(true);
    if (m_pageIndicator)
        m_pageIndicator->clearActiveColor();
    m_grid->setRect({kGridRectX, kGridRectY, kGridRectW, kGridRectH});
    applyDisplayModel(buildRootFolderModel(), folderTitleId(oldId), false, {}, true);
    configureDynamicLineNavigation();
    syncPageIndicator();
    // Leaving Grid-default folders back to single-row HOME can clear poster
    // peeks during the layout restore — re-warm nearby albums immediately so
    // they are not blank until the user scrolls.
    if (preferSingleRowPosters() && m_grid && !leaveSplashActive()) {
        constexpr int kFolderCoverWarmRadius = 4;
        applyFolderCoversToIcons(m_grid->focusedGlobalIndex(),
                                kFolderCoverWarmRadius);
        m_pendingFolderCoverFill = true;
        m_pendingFolderCoverNext = 0;
        m_pendingFolderCoverSkipRadius = kFolderCoverWarmRadius;
        m_armFolderCoverFillAfterSplash = false;
    }
    // The folder title is gone again: put the single-row logo back in its
    // default band.
    syncSteamGridDbLogoArea();
    // Drop in-folder hero art immediately so the next folder open does not
    // briefly (or permanently) show the previous title's artwork.
    if (m_steamGridDbBackdrop)
        m_steamGridDbBackdrop->showTitle(0, true);
    showFocusedSteamGridDbArtwork(true);
    // Leaving a folder into single-row often has an empty texture pool for the
    // HOME row — hold the geometric loader until the focused neighbourhood is
    // streamed instead of flashing a blank carousel.
    if (preferSingleRowPosters() && m_grid) {
        int page = 0, pageSize = 1;
        layoutPrefetchWindow(page, pageSize);
        if (m_iconStreamer.needsVisibleLoads(page, pageSize))
            beginVisibleArtWait();
    }
    if (preserveEditMode) {
        // Focus is on the folder we just left; use that as the root placement target.
        m_editTargetIndex = findTitleIndex(folderTitleId(oldId));
        syncEditPlacementAfterModelChange(false);
        reattachEditSourceIcon();
        m_titlePill->setText(nxui::I18n::instance().tr("game.move_prefix", "Move: ") + m_editHeldTitle);
        m_titlePill->setVisible(true);
    }
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::resumeSuspendedApplication(std::uint64_t titleId,
                                             const std::string& launchTitle) {
    if (titleId == 0 || !m_launchAnim)
        return;
    scheduleLeaveCapture([this, titleId, launchTitle]() {
        m_audio.playSfx(Sfx::LaunchGame);
        m_launchAnim->startResume([this, titleId, launchTitle]() {
            m_config.noteOpened(titleId);
            m_config.save();
            m_widgetStore.recordLaunch(titleId, launchTitle,
                static_cast<std::int64_t>(std::time(nullptr)));
            m_widgetStore.save();
            m_launcher.resumeApplication();
        });
    }, titleId);
}

void WiiUMenuApp::activateApplication(GlossyIcon* source, AppEntry* entry,
                                      std::uint64_t titleId,
                                      const std::string& launchTitle) {
    if (!source || titleId == 0) return;
    if (m_launcher.isAppSuspended(titleId)) {
        resumeSuspendedApplication(titleId, launchTitle);
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
        scheduleLeaveCapture([this, frame, texture, radius, base, border,
                              titleId, launchTitle, uid]() {
            m_audio.playSfx(Sfx::LaunchGame);
            m_launchAnim->start(frame, texture, radius, base, border, titleId, uid,
                [this, launchTitle](std::uint64_t id, AccountUid selectedUid) {
                    m_config.noteOpened(id);
                    m_config.save();
                    m_widgetStore.recordLaunch(id, launchTitle,
                        static_cast<std::int64_t>(std::time(nullptr)));
                    m_widgetStore.save();
                    m_launcher.launchApplication(id, selectedUid);
                });
        }, titleId);
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
        // StartupUserAccount=2 specifically requires a Nintendo Account.
        // libnx only validates that condition through the network-aware silent
        // selector, so do not blindly inject a configured local default UID.
        if (m_config.defaultProfileEnabled && entry->startupUserAccount != 2) {
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

nxui::Texture* WiiUMenuApp::folderCoverTexture(std::uint64_t titleId,
                                               bool preferPoster) {
    if (titleId == 0)
        return nullptr;
    auto it = m_folderCoverCache.find(titleId);
    if (it != m_folderCoverCache.end()
        && it->second.texture
        && it->second.preferPoster == preferPoster)
        return it->second.texture.get();

    auto tex = std::make_unique<nxui::Texture>();
    const auto data = AppListLoader::loadIconData(titleId, preferPoster);
    // Posters are shown larger in single-row; decode sharper than grid icons.
    const int maxSide = preferPoster ? 512 : 192;
    if (data.empty() ||
        !tex->loadFromMemory(app().gpu(), app().renderer(), data.data(), data.size(),
                             maxSide) ||
        !tex->valid()) {
        // Icon data can be temporarily unavailable during an application-list
        // refresh. Do not negative-cache the failure or this folder would keep
        // its placeholder until the menu is restarted.
        return nullptr;
    }
    nxui::Texture* raw = tex.get();
    m_folderCoverCache[titleId] = FolderCoverCacheEntry{std::move(tex), preferPoster};
    return raw;
}

nxui::Texture* WiiUMenuApp::findResidentAppTexture(std::uint64_t titleId) const {
    if (!m_grid || titleId == 0)
        return nullptr;
    for (const auto& icon : m_grid->allIcons()) {
        if (!icon || icon->entryKind() != GridEntryKind::Application)
            continue;
        if (icon->titleId() != titleId)
            continue;
        if (auto* tex = icon->texture(); tex && tex->valid())
            return tex;
    }
    return nullptr;
}

nxui::Texture* WiiUMenuApp::folderPeekTexture(std::uint64_t titleId,
                                              bool allowBorrow) {
    if (titleId == 0)
        return nullptr;
    auto it = m_folderPeekCache.find(titleId);
    if (it != m_folderPeekCache.end() && it->second && it->second->valid()) {
        m_folderPeekBorrowed.erase(titleId);
        return it->second.get();
    }

    // Morph commit: reuse a tile already on the GPU instead of SD decode +
    // upload. Caller must promoteBorrowedFolderPeeks before streamer swaps
    // those slots to poster art.
    if (allowBorrow) {
        if (auto* resident = findResidentAppTexture(titleId)) {
            m_folderPeekBorrowed.insert(titleId);
            return resident;
        }
    }

    auto tex = std::make_unique<nxui::Texture>();
    // Icons only — keep peeks cheap so multi-title stacks stay snappy.
    const auto data = AppListLoader::loadIconData(titleId, false);
    if (data.empty() ||
        !tex->loadFromMemory(app().gpu(), app().renderer(), data.data(), data.size(),
                             128) ||
        !tex->valid()) {
        return nullptr;
    }
    nxui::Texture* raw = tex.get();
    m_folderPeekCache[titleId] = std::move(tex);
    m_folderPeekBorrowed.erase(titleId);
    return raw;
}

int WiiUMenuApp::promoteBorrowedFolderPeeks(int maxPromotions) {
    if (maxPromotions <= 0 || m_folderPeekBorrowed.empty())
        return 0;
    int promoted = 0;
    auto it = m_folderPeekBorrowed.begin();
    while (it != m_folderPeekBorrowed.end() && promoted < maxPromotions) {
        const std::uint64_t titleId = *it;
        it = m_folderPeekBorrowed.erase(it);
        // Force an owned decode into m_folderPeekCache (no resident borrow).
        if (folderPeekTexture(titleId, false))
            ++promoted;
    }
    return promoted;
}

void WiiUMenuApp::ensureFolderCoverFillDoneSize() {
    const int total = m_model.count();
    if (static_cast<int>(m_folderCoverFillDone.size()) != total)
        m_folderCoverFillDone.assign(static_cast<std::size_t>(std::max(0, total)), 0);
}

void WiiUMenuApp::markFolderCoverFilled(int index) {
    ensureFolderCoverFillDoneSize();
    if (index < 0 || index >= static_cast<int>(m_folderCoverFillDone.size()))
        return;
    m_folderCoverFillDone[static_cast<std::size_t>(index)] = 1;
}

bool WiiUMenuApp::folderTileNeedsArt(int index) const {
    if (!m_grid || index < 0 || index >= m_model.count())
        return false;
    if (!m_model.at(index).isFolder())
        return false;
    if (index < static_cast<int>(m_folderCoverFillDone.size())
        && m_folderCoverFillDone[static_cast<std::size_t>(index)])
        return false;
    const auto& icons = m_grid->allIcons();
    if (index >= static_cast<int>(icons.size()) || !icons[static_cast<std::size_t>(index)])
        return false;
    const auto& icon = icons[static_cast<std::size_t>(index)];
    if (icon->folderCoverIsPoster() && icon->folderCoverTexture())
        return false;
    if (icon->folderPeekComposite() && icon->folderCoverTexture())
        return false;
    if (icon->folderPeekCount() > 0)
        return false;
    return true;
}

void WiiUMenuApp::attachFolderPeekComposite(GlossyIcon& icon, std::uint32_t /*folderId*/,
                                            nxui::Texture* composite) {
    icon.setFolderShowCover(m_config.folderShowCover);
    icon.clearFolderPeeks();
    icon.setFolderCoverIsPoster(false);
    icon.setFolderPeekComposite(true);
    icon.setFolderCoverTexture(composite);
}

void WiiUMenuApp::invalidateFolderPeekComposite(std::uint32_t folderId) {
    if (folderId == 0)
        return;
    m_folderPeekCompositeCache.erase(folderId);
    m_folderPeekCompositeBakeQueued.erase(folderId);
    for (auto& pending : m_pendingFolderPeekComposites) {
        if (pending.state && pending.state->folderId == folderId)
            pending.state->cancelled.store(true);
    }
    switchu::folders::peek_composite::remove(folderId);
}

void WiiUMenuApp::invalidateFolderPeekCompositesForTitle(std::uint64_t titleId) {
    if (titleId == 0)
        return;
    for (const auto& folder : m_folderStore.all()) {
        for (const std::uint64_t member : folder.titleIds) {
            if (member == titleId) {
                refreshFolderPeekComposite(folder.id);
                break;
            }
        }
    }
}

void WiiUMenuApp::refreshFolderPeekComposite(std::uint32_t folderId) {
    if (folderId == 0)
        return;
    invalidateFolderPeekComposite(folderId);
    if (!m_grid)
        return;
    for (int i = 0; i < m_model.count(); ++i) {
        if (!m_model.at(i).isFolder() || m_model.at(i).folderId != folderId)
            continue;
        if (i < static_cast<int>(m_folderCoverFillDone.size()))
            m_folderCoverFillDone[static_cast<std::size_t>(i)] = 0;
        applyFolderCoversToIcons(i, 0);
    }
}

void WiiUMenuApp::requestFolderPeekCompositeBake(std::uint32_t folderId) {
    if (folderId == 0)
        return;
    if (m_folderPeekCompositeCache.count(folderId))
        return;
    if (m_folderPeekCompositeBakeQueued.count(folderId))
        return;
    const auto* folder = m_folderStore.find(folderId);
    if (!folder)
        return;

    auto state = std::make_shared<FolderPeekCompositeBakeState>();
    state->folderId = folderId;
    state->folderSnapshot = *folder;
    m_folderPeekCompositeBakeQueued.insert(folderId);
    PendingFolderPeekCompositeBake pending;
    pending.state = state;
    pending.future = m_threadPool.submit([state]() {
        if (state->cancelled.load())
            return;
        const auto started = std::chrono::steady_clock::now();
        // Prefer disk; bake only on miss.
        switchu::folders::peek_composite::BakedImage image;
        if (!switchu::folders::peek_composite::load(state->folderSnapshot, image)) {
            image = switchu::folders::peek_composite::bake(state->folderSnapshot);
            if (!state->cancelled.load() && !image.rgba.empty())
                switchu::folders::peek_composite::save(state->folderSnapshot, image);
        }
        state->elapsedMs = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());
        if (state->cancelled.load() || image.rgba.empty()) {
            state->failed = true;
            return;
        }
        state->image = std::move(image);
    });
    m_pendingFolderPeekComposites.push_back(std::move(pending));
}

nxui::Texture* WiiUMenuApp::folderPeekCompositeTexture(std::uint32_t folderId) {
    if (folderId == 0)
        return nullptr;
    auto found = m_folderPeekCompositeCache.find(folderId);
    if (found != m_folderPeekCompositeCache.end() && found->second && found->second->valid())
        return found->second.get();

    const auto* folder = m_folderStore.find(folderId);
    if (!folder)
        return nullptr;

    // Fast path: sync disk load + one GPU upload (no per-icon decode).
    switchu::folders::peek_composite::BakedImage image;
    if (switchu::folders::peek_composite::load(*folder, image) && !image.rgba.empty()) {
        auto tex = std::make_unique<nxui::Texture>();
        if (tex->loadFromPixels(app().gpu(), app().renderer(),
                                image.rgba.data(), image.width, image.height)
            && tex->valid()) {
            nxui::Texture* raw = tex.get();
            m_folderPeekCompositeCache[folderId] = std::move(tex);
            DebugLog::log("[folder-peek] composite disk hit folder=%u %dx%d",
                          folderId, image.width, image.height);
            return raw;
        }
    }

    requestFolderPeekCompositeBake(folderId);
    return nullptr;
}

void WiiUMenuApp::pumpFolderPeekComposites() {
    if (m_pendingFolderPeekComposites.empty())
        return;

    // One GPU upload per frame so a burst of finished bakes cannot hitch.
    constexpr int kUploadsPerFrame = 1;
    int uploads = 0;

    for (auto it = m_pendingFolderPeekComposites.begin();
         it != m_pendingFolderPeekComposites.end() && uploads < kUploadsPerFrame;) {
        if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        try {
            it->future.get();
        } catch (...) {
        }
        auto state = it->state;
        const std::uint32_t folderId = state ? state->folderId : 0;
        m_folderPeekCompositeBakeQueued.erase(folderId);
        it = m_pendingFolderPeekComposites.erase(it);
        if (!state || state->cancelled.load() || state->failed || state->image.rgba.empty())
            continue;

        auto tex = std::make_unique<nxui::Texture>();
        if (!tex->loadFromPixels(app().gpu(), app().renderer(),
                                 state->image.rgba.data(),
                                 state->image.width, state->image.height)
            || !tex->valid())
            continue;
        nxui::Texture* raw = tex.get();
        m_folderPeekCompositeCache[folderId] = std::move(tex);
        ++uploads;
        DebugLog::log("[folder-peek] composite ready folder=%u in %ldms",
                      folderId, state->elapsedMs);

        if (!m_grid)
            continue;
        const auto& icons = m_grid->allIcons();
        const int total = std::min(m_model.count(), static_cast<int>(icons.size()));
        for (int i = 0; i < total; ++i) {
            if (!m_model.at(i).isFolder() || m_model.at(i).folderId != folderId)
                continue;
            if (!icons[static_cast<std::size_t>(i)])
                continue;
            // Don't clobber a custom SteamGridDB folder poster.
            if (SteamGridDbManager::hasPoster(m_model.at(i).titleId))
                continue;
            attachFolderPeekComposite(*icons[static_cast<std::size_t>(i)], folderId, raw);
            markFolderCoverFilled(i);
        }
    }
}

int WiiUMenuApp::nextFolderCoverFillIndex(int focus) const {
    const int total = m_model.count();
    if (total <= 0)
        return -1;
    const int anchor = (focus >= 0 && focus < total) ? focus : 0;
    int best = -1;
    int bestDist = total + 1;
    for (int i = 0; i < total; ++i) {
        if (!folderTileNeedsArt(i))
            continue;
        const int dist = std::abs(i - anchor);
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

void WiiUMenuApp::warmFolderPeeksAroundFocus(int radius) {
    if (!m_grid || !preferSingleRowPosters())
        return;
    if (m_grid->isLayoutMorphing() || leaveSplashActive())
        return;
    const int focus = m_grid->focusedGlobalIndex();
    if (focus < 0 || focus >= m_model.count() || !m_model.at(focus).isFolder())
        return;

    // Composites are disk hit or async bake — safe during soft-land.
    if (folderTileNeedsArt(focus))
        applyFolderCoversToIcons(focus, 0);

    if (radius <= 0)
        return;
    for (int distance = 1; distance <= radius; ++distance) {
        for (const int index : {focus - distance, focus + distance}) {
            if (folderTileNeedsArt(index))
                applyFolderCoversToIcons(index, 0);
        }
    }
}

void WiiUMenuApp::warmFolderCompositesEarly() {
    if (!m_grid || !preferSingleRowPosters())
        return;
    ensureFolderCoverFillDoneSize();
    const int focus = m_grid->focusedGlobalIndex();
    const int total = m_model.count();
    int diskAttached = 0;
    int bakeQueued = 0;
    for (int i = 0; i < total; ++i) {
        if (!m_model.at(i).isFolder())
            continue;
        if (SteamGridDbManager::hasPoster(m_model.at(i).titleId))
            continue;
        const std::uint32_t folderId = m_model.at(i).folderId;
        const int dist = (focus >= 0) ? std::abs(i - focus) : i;
        if (dist <= 4) {
            // Nearby: try sync disk attach (cheap) or queue bake.
            applyFolderCoversToIcons(i, 0);
            if (m_folderPeekCompositeCache.count(folderId))
                ++diskAttached;
            else
                ++bakeQueued;
        } else {
            requestFolderPeekCompositeBake(folderId);
            markFolderCoverFilled(i);
            ++bakeQueued;
        }
    }
    m_pendingFolderCoverFill = true;
    DebugLog::log("[folder-peek] early warm focus=%d disk=%d queued=%d",
                  focus, diskAttached, bakeQueued);
}

void WiiUMenuApp::applyFolderCoversToIcons(int centerIndex, int radius) {
    if (!m_grid)
        return;
    ensureFolderCoverFillDoneSize();
    const bool preferPoster = preferSingleRowPosters();
    const bool styleAllowsCover = switchu::folders::folderShouldShowCover(
        m_config.folderStyle, m_config.folderShowCover);
    const auto& icons = m_grid->allIcons();
    const int total = std::min(m_model.count(), static_cast<int>(icons.size()));
    const int begin = (centerIndex >= 0 && radius >= 0)
        ? std::max(0, centerIndex - radius) : 0;
    const int end = (centerIndex >= 0 && radius >= 0)
        ? std::min(total, centerIndex + radius + 1) : total;
    for (int i = begin; i < end; ++i) {
        if (!m_model.at(i).isFolder() || !icons[static_cast<std::size_t>(i)])
            continue;
        auto& icon = icons[static_cast<std::size_t>(i)];
        icon->setFolderShowCover(m_config.folderShowCover);
        icon->clearFolderPeeks();
        icon->setFolderCoverIsPoster(false);

        const std::uint64_t coverId = m_model.at(i).folderCoverTitleId;
        const std::uint64_t pseudo = m_model.at(i).titleId;
        const std::uint32_t folderId = m_model.at(i).folderId;

        // Single-row: custom folder poster, else baked 2×3 composite (async).
        if (preferPoster) {
            if (SteamGridDbManager::hasPoster(pseudo)) {
                icon->setFolderPeekComposite(false);
                icon->clearFolderPeeks();
                icon->setFolderCoverTexture(folderCoverTexture(pseudo, true));
                icon->setFolderCoverIsPoster(true);
                markFolderCoverFilled(i);
                continue;
            }
            if (auto* composite = folderPeekCompositeTexture(folderId)) {
                attachFolderPeekComposite(*icon, folderId, composite);
                markFolderCoverFilled(i);
                continue;
            }
            // Bake in flight (or empty folder): keep the colour bar until ready.
            icon->setFolderPeekComposite(false);
            icon->setFolderCoverIsPoster(false);
            icon->setFolderCoverTexture(nullptr);
            icon->clearFolderPeeks();
            // Mark done so outward fill can move on; pump attaches when baked.
            markFolderCoverFilled(i);
            continue;
        }

        nxui::Texture* coverTex = nullptr;
        if (styleAllowsCover && coverId != 0)
            coverTex = folderCoverTexture(coverId, false);
        icon->setFolderCoverTexture(coverTex);
        markFolderCoverFilled(i);
    }
}

std::shared_ptr<GlossyIcon> WiiUMenuApp::makeIcon(const AppEntry& entry) {
    auto icon = std::make_shared<GlossyIcon>();
    icon->setEntryKind(entry.kind);
    icon->setFolderPreviewCount(entry.folderPreviewCount);
    icon->setFolderVisualSeed(entry.folderId);
    icon->setFolderColorIndex(entry.folderColorIndex);
    icon->setFolderStyleIndex(m_config.folderStyle);
    icon->setThemeMode(m_theme.mode);
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
        icon->setOnActivate([this]() {
            if (deletePageAvailable())
                showDeletePageDialog();
        });
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
        icon->setFolderCoverTitleId(entry.folderCoverTitleId);
        icon->setFolderShowCover(m_config.folderShowCover);
        icon->setPosterFrame(preferSingleRowPosters());
        // Cover texture is applied in applyFolderCoversToIcons (poster-aware).
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
    // Texture is set by IconStreamer::onPageChanged() ΓÇö not here.
    icon->setCornerRadius(m_theme.iconCornerRadius);
    icon->setLoadingColor(m_theme.cursorNormal);
    icon->setIsGameCard(entry.isGameCard());
    icon->setGameCardTexture(&m_gameCardTex);
    icon->setNotLaunchable(!entry.isLaunchable());
    icon->setPosterFrame(
        preferSingleRowPosters()
        && SteamGridDbManager::hasPoster(entry.titleId));
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
        AppEntry* appEntry = nullptr;
        const int entryIndex = findTitleIndex(raw->titleId());
        if (entryIndex >= 0)
            appEntry = &m_model.at(entryIndex);
        activateApplication(raw, appEntry, raw->titleId(), raw->title());
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
    applyAutoThemeConfig();

    m_activePresetName = m_config.themePreset;

    // If automatic day/night switching is enabled, let it pick the startup
    // preset before any widget is built, so the correct theme is applied in one
    // pass instead of rebuilding after the fact. evaluate() consumes the current
    // phase, so onUpdate won't re-trigger until the phase actually changes.
    {
        const auto snapshot = m_clockService.refresh();
        if (auto desired = m_autoTheme.evaluate(snapshot)) {
            if (findPresetPtr(*desired)) {
                m_activePresetName = *desired;
                m_config.themePreset = *desired;
            }
        }
    }

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

    GridLayoutMetrics gridMetrics = computeGridLayoutMetrics();

    // Compact empty pads before making icons when booting into single-row.
    // Do NOT call applyDisplayModel here — it wires focus callbacks that touch
    // titlePill/cursor/backdrop before those widgets exist (boot crash).
    if (!m_allApps.empty())
        m_model = buildRootFolderModel();

    const auto iconBuildStarted = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<GlossyIcon>> icons;
    std::vector<std::uint64_t> titleIds;
    icons.reserve(m_model.count());
    titleIds.reserve(m_model.count());
    for (int i = 0; i < m_model.count(); ++i) {
        icons.push_back(makeIcon(m_model.at(i)));
        titleIds.push_back(m_model.at(i).isApplication() ? m_model.at(i).titleId : 0);
    }
    m_iconStreamer.reconcileTitleIds(titleIds);
    DebugLog::log("[init] grid icon objects built count=%d in %ldms layout=%d",
                  m_model.count(),
                  static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - iconBuildStarted).count()),
                  static_cast<int>(m_appLayoutMode));

    m_grid = std::make_shared<IconGrid>();
    m_grid->setRect({kGridRectX, kGridRectY, kGridRectW, kGridRectH});
    // Snap to the saved layout immediately — morphing Grid→Line on HOME return
    // briefly showed the grid under/after the leave splash.
    m_grid->setLayoutMode(m_appLayoutMode, false);
    // The grid starts from IconGrid's own default (teleport), so the configured
    // mode has to be applied here too: without it the first page changes after
    // boot ignored the Translate setting until a rebuild happened to fix it.
    m_grid->setSlideTransition(m_config.pageTransitionMode == 0);
    m_grid->setup(std::move(icons),
                  std::clamp(m_config.gridColumns, 1, 8),
                  std::clamp(m_config.gridRows, 1, 5),
                  gridMetrics.cellW, gridMetrics.cellH,
                  gridMetrics.padX, gridMetrics.padY);
    m_grid->setLayoutMode(m_appLayoutMode, false);
    // Defer syncSingleRowPosterPresentation until after leave-session restore
    // so folder-peek warm uses the real focus (not index 0).

    m_cursor = std::make_shared<SelectionCursor>();
    m_cursor->setInstantMotion(m_config.cursorMotionMode == 1);

    m_clock = std::make_shared<DateTimeWidget>();
    m_clock->setSize(150, 62);
    m_clock->setMarginTop(14.f);
    m_clock->setFont(&m_fontNormal);
    m_clock->setSmallFont(&m_fontSmall);
    m_clock->setClockService(&m_clockService);
    m_clock->setUse12HourClock(m_config.clockUse12Hour);
    m_clock->setCornerRadius(m_theme.cellCornerRadius);
    m_clock->setForceLiquidGlass(true);
    m_clock->setBlurEnabled(false);

    m_layoutFlip = std::make_shared<LayoutFlipButton>();
    m_layoutFlip->setTheme(&m_theme);
    m_layoutFlip->setSingleRowActive(m_appLayoutMode == AppLayoutMode::DynamicLine);
    m_layoutFlip->ensureIcon(app().gpu(), app().renderer(),
                             std::string(SD_ASSETS) + "/icons/change_view.png");
    m_layoutFlip->setOnActivate([this]() {
        if (m_editMode || m_openFolderId != 0 || leaveSplashActive())
            return;
        if (m_layoutTransition.kind != LayoutTransitionKind::Idle)
            return;
        m_retainLayoutFlipFocus = true;
        m_audio.playSfx(Sfx::Navigate);
        toggleAppLayoutMode();
    });

    m_battery = std::make_shared<BatteryWidget>();
    m_battery->setMarginTop(14.f);
    m_battery->setMarginRight(24.f);
    m_battery->setSize(150, 62);
    m_battery->setFont(&m_fontSmall);
    m_battery->setCornerRadius(m_theme.cellCornerRadius);
    m_battery->setForceLiquidGlass(true);
    m_battery->setBlurEnabled(false);
    m_battery->setOnActivate([this]() {
        if (m_editMode || leaveSplashActive())
            return;
        m_audio.playSfx(Sfx::Activate);
        openQuickSettings();
    });

    m_profileLock = std::make_shared<ProfileLockButton>();
    m_profileLock->setSize(kProfileLockWidth, kProfileLockHeight);
    m_profileLock->setMinWidth(kProfileLockWidth);
    m_profileLock->setMinHeight(kProfileLockHeight);
    m_profileLock->setShrink(0.f);
    m_profileLock->setMarginTop(14.f);
    m_profileLock->setMarginLeft(24.f);
    m_profileLock->setCornerRadius(m_theme.cellCornerRadius);
    // The HUD row spreads its items evenly; balance the square tile against the
    // battery widget so the clock lands on the screen's centre line.
    m_profileLock->setMarginRight((150.f + 24.f) - (kProfileLockWidth + 24.f));
    m_profileLock->setTheme(&m_theme);
    m_profileLock->setOnActivate([this]() {
        m_audio.playSfx(Sfx::Activate);
        openProfileCarousel();
    });

    loadProfiles(!m_fastReturnRequested);

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

    m_folderZoom = std::make_shared<FolderZoom>();
    m_folderZoom->setRect({0, 0, 1280, 720});

    m_userSelect = std::make_shared<OverlayDialog>();
    m_userSelect->cursor().setInstantMotion(m_config.cursorMotionMode == 1);
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
    m_dialog->cursor().setInstantMotion(m_config.cursorMotionMode == 1);
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
        // Stream icon textures for the new page / focused single-row tile.
        int page = 0, pageSize = 1;
        if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
            page = std::max(0, m_grid->focusedGlobalIndex());
            pageSize = 1;
        } else {
            page = m_grid->currentPage();
            pageSize = std::max(1, m_grid->iconsPerPage());
        }
        m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                     m_grid->allIcons());
        auto* target = m_grid->focusManager().current();
        if (target)
            focusManager().setFocus(target);
        updateCursor();
    });

    // Prefer the leave-frame session from the last title launch/resume so the
    // live UI matches the splash. System applets do not rewrite that cache.
    bool restoredLeaveSession = false;
#ifdef SWITCHU_MENU
    if (m_leaveSession.valid) {
        restoreLeaveSession();
        restoredLeaveSession = true;
    } else if (m_launcher.suspendedTitleId() != 0) {
        int initialPage = 0;
        int suspendedIndex = findTitleIndex(m_launcher.suspendedTitleId());
        if (suspendedIndex >= 0 && m_grid->iconsPerPage() > 0)
            initialPage = suspendedIndex / m_grid->iconsPerPage();
        if (initialPage > 0)
            m_grid->setPage(initialPage);
    }
#endif

    // After focus/page restore: poster frames + budgeted folder peeks (±4).
    if (m_grid) {
        const auto posterSyncStarted = std::chrono::steady_clock::now();
        syncSingleRowPosterPresentation(false);
        if (preferSingleRowPosters()) {
            m_deferBackdropPreloadSec = 2.0f;
            // Under splash, keep full streamer radius so neighbours warm while
            // covered; soft-land arms at handoff. No-splash return arms here.
            if (m_fastReturnRequested && !leaveSplashActive()) {
                m_returnPreloadPaceSec = 4.0f;
                m_iconStreamer.startSoftLanding(2.0f, 2, 1.75f, 3);
                if (m_steamGridDbBackdrop)
                    m_steamGridDbBackdrop->setPreloadMinInterval(0.45f);
            }
        }
        // Fast-return without a leave-splash hold still needs the fill armed.
        if (m_armFolderCoverFillAfterSplash && !leaveSplashActive()) {
            m_armFolderCoverFillAfterSplash = false;
            m_pendingFolderCoverFill = false;
            m_folderCoverFillDelayFrames = 0;
            m_pendingFolderCoverNext = 0;
            m_pendingFolderCoverSkipRadius = -1;
            warmFolderCompositesEarly();
        }
        DebugLog::log("[init] single-row poster sync in %ldms focus=%d peeksOwned=%u pendingFill=%d armAfterSplash=%d",
                      static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - posterSyncStarted).count()),
                      m_grid->focusedGlobalIndex(),
                      static_cast<unsigned>(m_folderPeekCache.size()),
                      m_pendingFolderCoverFill ? 1 : 0,
                      m_armFolderCoverFillAfterSplash ? 1 : 0);
    }

    const bool fastReturn = m_fastReturnRequested;
    if (fastReturn) {
        // Application updates once before presenting its first frame. Two ticks
        // keep sidebar uploads off the pre-first-frame critical path.
        m_deferredInitialAssetFrames = 2;
        DebugLog::log("[init] return path: deferring initial icon/sidebar uploads");
    } else if (m_grid) {
        // Load textures for the initial visible page.
        int page = 0, pageSize = 1;
        if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
            page = std::max(0, m_grid->focusedGlobalIndex());
            pageSize = 1;
        } else {
            page = m_grid->currentPage();
            pageSize = std::max(1, m_grid->iconsPerPage());
        }
        m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }

    if (fastReturn) {
        for (auto& icon : m_grid->allIcons())
            icon->forceVisible();
        // Placeholders cover progressively loaded artwork, so do not keep an
        // already usable HOME scene hidden behind the old opaque fade.
        m_returnFadeTimer = 0.f;
    } else if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
        // Single-row hides the selection ring — an appear tween from scale 0
        // makes boot look unfocused / unresponsive. Show tiles immediately.
        for (auto& icon : m_grid->allIcons())
            icon->forceVisible();
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
    sidebarActions.onAlbum = [this]() {
        scheduleLeaveCapture([this]() { m_launcher.launchAlbum(); });
    };
    sidebarActions.onMiiEditor = [this]() {
        scheduleLeaveCapture([this]() { m_launcher.launchMiiEditor(); });
    };
    sidebarActions.onControllers = [this]() {
        scheduleLeaveCapture([this]() { m_launcher.launchControllerPairing(); });
    };
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
    syncSteamGridDbLogoArea();

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
    m_topHud->addChild(m_profileLock);
    m_topHud->addChild(m_clock);
    m_topHud->addChild(m_battery);
    m_topHud->layout();
    placeLayoutFlipButton();
    // Flip/battery are visible only after HUD layout — rewire the full strip.
    configureDynamicLineNavigation();

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
    m_contentLayer->addChild(m_folderZoom);
    m_contentLayer->addChild(m_grid);
    m_contentLayer->addChild(m_folderHeader);
    m_contentLayer->addChild(m_leftSidebar);
    m_contentLayer->addChild(m_rightSidebar);
    m_contentLayer->addChild(m_topHud);
    // Sibling of topHud (not nested) so clock layout stays unchanged and the
    // flip control remains independently focusable / tappable.
    if (m_layoutFlip)
        m_contentLayer->addChild(m_layoutFlip);
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
        else if (m_folderOptions && m_folderOptions->isActive())
            focusManager().setFocus(m_folderOptions.get());
        else if (m_settings && m_settings->isActive())
            focusManager().setFocus(m_settings.get());
    });
    m_steamGridDbPicker->onSearch([this]() { editSteamGridDbPickerQuery(); });
    m_steamGridDbPicker->onApply(
        [this](const SteamGridDbManager::BrowseResult& browse,
               const SteamGridDbManager::Candidate& candidate) {
            applySteamGridDbCandidate(browse, candidate);
        });
    m_steamGridDbPicker->onPickGame(
        [this](const SteamGridDbManager::BrowseResult& browse,
               const SteamGridDbManager::GameMatch& match) {
            if (browse.titleId == 0 || match.id <= 0) return;
            if (m_steamGridDbApplyFuture.valid() || m_steamGridDb.running())
                return;
            if (m_overlayLayer && m_steamGridDbPicker) {
                m_overlayLayer->removeChild(m_steamGridDbPicker.get());
                m_overlayLayer->addChild(m_steamGridDbPicker);
            }
            // Prefer the picker's durable search string over a possibly-empty
            // browse.query after list/artwork transitions.
            const std::string query = !m_steamGridDbPicker->query().empty()
                ? m_steamGridDbPicker->query()
                : (!browse.query.empty() ? browse.query : browse.title);
            m_steamGridDbPicker->showLoading(browse.titleId, browse.title, query,
                                             browse.kind, true);
            m_steamGridDbPicker->setMessage("Loading artwork for " + match.name + "...",
                                            true);
            focusManager().setFocus(m_steamGridDbPicker.get());
            ++m_steamGridDbBrowseGeneration;
            m_steamGridDbBrowseFutureGeneration = m_steamGridDbBrowseGeneration;
            if (m_steamGridDbBrowseFuture.valid())
                m_steamGridDbRetiredBrowses.push_back(std::move(m_steamGridDbBrowseFuture));
            m_steamGridDbBrowseFuture = std::async(std::launch::async,
                [apiKey = m_config.steamGridDbApiKey, browse, match, query]() {
                    return SteamGridDbManager::browseGame(
                        apiKey, browse.titleId, browse.title, query,
                        browse.kind, match.id, match.name);
                });
        });
    // Picker is added after folder/game options so it paints above them; show
    // path also re-parents it to the top of the overlay stack.
    createFolderOptions();
    m_overlayLayer->addChild(m_steamGridDbPicker);
    createControllerTest();

    m_overlayLayer->addChild(m_contextMenu);
    m_overlayLayer->addChild(m_dialog);
    m_overlayLayer->addChild(m_progressDialog);
    m_overlayLayer->addChild(m_launchAnim);

    root.addChild(m_bgLayer);
    root.addChild(m_contentLayer);
    root.addChild(m_overlayLayer);

#ifdef SWITCHU_MENU
    // Folder leave-restore must run after HUD/header/sidebars exist so
    // requestOpenFolder can capture the frosted root backdrop correctly.
    if (m_leaveRestoreFolderId != 0) {
        const std::uint32_t folderId = m_leaveRestoreFolderId;
        const std::uint64_t focusId = m_leaveRestoreFolderFocus;
        m_leaveRestoreFolderId = 0;
        m_leaveRestoreFolderFocus = 0;
        requestOpenFolder(folderId, focusId);
        DebugLog::log("[leave] folder restore capture armed id=%u", folderId);
    }

    // Prefer suspended → leave-restored title → last opened → first tile.
    // Always re-pin after the HUD/nav wire so chrome strip setup cannot leave
    // D-pad on a spatial orphan.
    {
        std::uint64_t pin = 0;
#ifdef SWITCHU_MENU
        pin = m_launcher.suspendedTitleId();
#endif
        if (pin == 0 && restoredLeaveSession && m_grid) {
            if (auto* cur = m_grid->focusManager().current();
                cur && cur->tag() == "glossy_icon")
                pin = static_cast<GlossyIcon*>(cur)->titleId();
        }
        ensureHomeTitleFocus(pin);
    }
#else
    ensureHomeTitleFocus(0);
#endif
    if (m_appLayoutMode == AppLayoutMode::DynamicLine && m_grid) {
        for (auto& icon : m_grid->allIcons()) {
            if (icon)
                icon->forceVisible();
        }
    }
    updateCursor();
    {
        std::uint64_t focusedTid = 0;
        if (auto* cur = focusManager().current();
            cur && cur->tag() == "glossy_icon")
            focusedTid = static_cast<GlossyIcon*>(cur)->titleId();
        DebugLog::log("[focus] boot focus ready layout=%d cur=%s gridIdx=%d tid=%016lX",
                      static_cast<int>(m_appLayoutMode),
                      focusManager().current()
                          ? focusManager().current()->tag().c_str() : "<none>",
                      m_grid ? m_grid->focusedGlobalIndex() : -1,
                      static_cast<unsigned long>(focusedTid));
    }
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
        std::vector<std::string> rootTracks;
        std::vector<std::string> albumFolders;
        ec.clear();
        for (const auto& entry : std::filesystem::directory_iterator(musicDir, ec)) {
            if (ec)
                break;
            const auto name = entry.path().filename().string();
            if (name.empty() || name[0] == '.')
                continue;
            std::error_code entryEc;
            if (entry.is_directory(entryEc)) {
                albumFolders.push_back(name);
            } else if (entry.is_regular_file(entryEc) && isMp3Filename(name)) {
                rootTracks.push_back(name);
            }
        }
        sortMusicTrackNames(rootTracks);
        std::sort(albumFolders.begin(), albumFolders.end());

        size_t loaded = 0;
        for (const auto& t : rootTracks) {
            if (m_audio.loadTrack(musicDir + "/" + t))
                ++loaded;
        }
        for (const auto& folder : albumFolders) {
            const std::string albumDir = musicDir + "/" + folder;
            std::vector<std::string> albumTracks;
            ec.clear();
            for (const auto& entry : std::filesystem::directory_iterator(albumDir, ec)) {
                if (ec)
                    break;
                std::error_code entryEc;
                if (!entry.is_regular_file(entryEc))
                    continue;
                const auto name = entry.path().filename().string();
                if (isMp3Filename(name))
                    albumTracks.push_back(name);
            }
            if (albumTracks.empty())
                continue;
            sortMusicTrackNames(albumTracks);
            auto cover = loadAlbumFolderCover(albumDir);
            if (!cover.empty())
                m_audio.setFolderCover(folder, std::move(cover));
            for (const auto& t : albumTracks) {
                if (m_audio.loadTrack(albumDir + "/" + t, folder))
                    ++loaded;
            }
            // If no folder.jpg, reuse the first track APIC as the album thumb.
            if (!m_audio.folderCoverArt(folder)) {
                for (const auto& track : m_audio.tracks()) {
                    if (track.albumFolder == folder && !track.coverArt.empty()) {
                        m_audio.setFolderCover(folder, track.coverArt);
                        break;
                    }
                }
            }
        }

        if (!m_config.musicPlaylistOrder.empty())
            m_audio.applyPlaylistOrder(m_config.musicPlaylistOrder);
        m_audio.setShuffle(m_config.musicShuffle);
        m_audio.setRepeatMode(static_cast<MusicRepeatMode>(
            std::clamp(m_config.musicRepeatMode, 0, 2)));
        DebugLog::log("[audio] Loaded %zu music tracks (%zu albums)",
                      loaded, albumFolders.size());
    } else {
        DebugLog::log("[audio] No music directory for preset '%s'", effectivePreset.c_str());
    }
}

void WiiUMenuApp::persistMusicPlaybackState(bool updateEnabledFlag) {
    m_config.musicTrackIndex = m_audio.currentIndex();
    m_config.musicTrackKey = m_audio.currentTrackKey();
    m_config.musicPositionSeconds = m_audio.positionSeconds();
    m_config.musicShuffle = m_audio.shuffle();
    m_config.musicRepeatMode = static_cast<int>(m_audio.repeatMode());
    m_config.musicPlaylistOrder = m_audio.playlistOrderKeys();
    if (updateEnabledFlag)
        m_config.musicEnabled = m_audio.isPlaying();
}

void WiiUMenuApp::resumeMenuMusicAfterReturn() {
    m_audio.setMusicFade(1.f);
    m_musicFadeActive = false;
    // Already-playing BGM must not Mix_PlayMusic again — that restarts the
    // decoder and sounds like a skip when HOME only dismisses an overlay.
    if (m_config.musicEnabled && m_audio.trackCount() > 0 && !m_audio.isPlaying())
        m_audio.play();
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
    m_refreshPrevFocusTitleId = 0;
    if (m_grid) {
        // finalizeRefresh() rebuilds every icon object, so the grid's focus
        // manager falls back to the first slot. Remember what was focused and
        // put it back, otherwise the cursor visibly jumps to slot 1 shortly
        // after boot while the user is already browsing.
        if (auto* focused = m_grid->focusManager().current())
            if (focused->tag() == "glossy_icon")
                m_refreshPrevFocusTitleId =
                    static_cast<GlossyIcon*>(focused)->titleId();
    }
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

    // Drop focus pointers into icons that applyDisplayModel is about to replace.
    for (const auto& icon : m_grid->allIcons()) {
        focusManager().invalidateWidget(icon.get());
        m_grid->focusManager().invalidateWidget(icon.get());
    }
    m_editBoundIcon = nullptr;
    m_editSourceIcon = nullptr;

    const std::uint64_t focusId = m_refreshPrevFocusTitleId;
    m_refreshPrevFocusTitleId = 0;
    if (m_openFolderId != 0) {
        applyDisplayModel(buildOpenFolderModel(m_openFolderId), focusId, false,
                          {}, true);
        m_refreshCooldownFrames = 20;
        if (m_layoutDirty) saveMenuLayout();
        DebugLog::log("[refresh] folder view restored folder=%u", m_openFolderId);
        return;
    }

    // Layout-aware rebuild (compacts empty pads in single-row).
    applyDisplayModel(buildRootFolderModel(), focusId, false, {}, true);
    if (m_appLayoutMode == AppLayoutMode::Grid && m_refreshPrevPage > 0)
        m_grid->setPage(m_refreshPrevPage);

    int page = 0, pageSize = 1;
    if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
        page = std::max(0, m_grid->focusedGlobalIndex());
        pageSize = 1;
    } else {
        page = m_grid->currentPage();
        pageSize = std::max(1, m_grid->iconsPerPage());
    }
    m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                 m_grid->allIcons());

    for (auto& icon : m_grid->allIcons())
        icon->forceVisible();
    focusManager().setFocus(m_grid->focusManager().current());

    m_refreshCooldownFrames = 20;
    applyTheme();
    if (m_layoutDirty)
        saveMenuLayout();
    DebugLog::log("[refresh] done, %d icons page=%d layout=%d",
                  m_model.count(), page, static_cast<int>(m_appLayoutMode));
}

#endif

void WiiUMenuApp::onUpdate(float dt) {
    if (m_systemAsleep) {
        // Console is asleep. Application stopped presenting frames, so the only
        // work left is reading the wake notification: the frame work in
        // onUpdateAwake() kept the console warm for the whole sleep otherwise.
        pollSleepNotifications();
        return;
    }

    onUpdateAwake(dt);
}

void WiiUMenuApp::enterSystemSleep() {
    if (m_systemAsleep)
        return;
    m_systemAsleep = true;
    DebugLog::log("[power] console sleeping: frames stopped, music paused");

    // Music keeps the mixer feeding the audio path for the whole sleep. The
    // wake path restores exactly what was audible before.
    m_musicWasPlayingBeforeSleep = m_audio.isPlaying();
    if (m_musicWasPlayingBeforeSleep) {
        persistMusicPlaybackState();
        m_audio.pause();
    }

    // Stop presenting. A loop that keeps submitting frames never lets the
    // console settle into its low-power sleep state.
    app().setSleeping(true);
}

void WiiUMenuApp::leaveSystemSleep() {
    if (!m_systemAsleep)
        return;
    m_systemAsleep = false;
    app().setSleeping(false);

    // The vi layer/swapchain goes stale across sleep: PresentQueue::acquireImage
    // would block forever and freeze the menu on the last presented frame.
    // Rebuild the swapchain, then re-apply the TV underscan size (the managed
    // layer size can be reset by the OS during sleep) before presenting again.
    app().gpu().recreateSwapchain();
    switchu::menu::applyScreenUnderscan();

    if (m_musicWasPlayingBeforeSleep && m_config.musicEnabled)
        m_audio.play();
    m_musicWasPlayingBeforeSleep = false;

    // Notifications pushed during sleep were only inspected for the wake
    // message, so re-read the catalogue and realign the screen state.
    m_refreshQueued = true;
    m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 3);
    updateCursor();
    DebugLog::log("[power] console awake: presentation resumed");
}

void WiiUMenuApp::pollSleepNotifications() {
#ifdef SWITCHU_MENU
    AppletStorage notifySt;
    while (R_SUCCEEDED(appletPopInteractiveInData(&notifySt))) {
        switchu::smi::DaemonNotification notif{};
        s64 sz = 0;
        appletStorageGetSize(&notifySt, &sz);
        if (sz >= (s64)sizeof(notif))
            appletStorageRead(&notifySt, 0, &notif, sizeof(notif));
        appletStorageClose(&notifySt);

        if (notif.magic != switchu::smi::kNotifyMagic)
            continue;
        DebugLog::log("[power] sleep-time notify msg=%u", (unsigned)notif.msg);
        if (notif.msg == switchu::smi::MenuMessage::WakeUp)
            leaveSystemSleep();
    }
#endif
}

void WiiUMenuApp::onUpdateAwake(float dt) {
    updateLeaveSplashHandoff(dt);
    pollDeferredLeaveCapture();
    const float motionDt = m_leaveMotionFrozen ? 0.f : dt;

    // Widget-owned images are intentionally managed before recording the next
    // frame. This is the only safe point to retire Deko textures from pages
    // that have left the screen and to warm the next page.
    syncWidgetPageAssets();
    // Reading and decoding happens on a worker. Transfer at most one recent
    // widget texture to the GPU per frame so returning HOME remains smooth.
    pollRecentWidgetAssets();
    pollGameArtworkAssets();
    syncFolderHeader();

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
    // Skip during prefetch — presentationLayoutMode() still guards, but avoid
    // any chance of enabling single-row hero while the grid is on screen.
    if (m_layoutTransition.kind != LayoutTransitionKind::PrefetchThenMorph
        && m_navigator.route() == switchu::navigation::Route::Home
        && !(m_dialog && m_dialog->isActive())
        && !(m_quickSettings && m_quickSettings->isActive())
        && !(m_textEntry && m_textEntry->isActive())
        && !(m_settings && m_settings->isActive())
        && !(m_themeShop && m_themeShop->isActive())
        && !(m_gameOptions && m_gameOptions->isActive())
        && !(m_folderOptions && m_folderOptions->isActive())
        && !(m_userSelect && m_userSelect->isActive())) {
        showFocusedSteamGridDbArtwork();
        // Dynamic-line focus can move without a global focus callback — warm
        // folder peeks from the grid's source of truth the same way.
        if (m_grid && preferSingleRowPosters()
            && presentationLayoutMode() == AppLayoutMode::DynamicLine
            && !leaveSplashActive()) {
            const int focus = m_grid->focusedGlobalIndex();
            if (focus != m_lastFolderPeekWarmFocus
                && focus >= 0 && focus < m_model.count()
                && m_model.at(focus).isFolder()) {
                m_lastFolderPeekWarmFocus = focus;
                warmFolderPeeksAroundFocus(1);
            }
        }
    }

    pumpFolderPeekComposites();

    if (m_deferBackdropPreloadSec > 0.f)
        m_deferBackdropPreloadSec = std::max(0.f, m_deferBackdropPreloadSec - dt);
    if (m_returnPreloadPaceSec > 0.f) {
        m_returnPreloadPaceSec = std::max(0.f, m_returnPreloadPaceSec - dt);
        if (m_returnPreloadPaceSec <= 0.f && m_steamGridDbBackdrop)
            m_steamGridDbBackdrop->setPreloadMinInterval(0.f);
    }
    if (m_steamGridDbBackdrop) {
        // Pause neighbour hero warm while the user is navigating; focused art
        // still updates via showTitle above.
        const bool paceWindow = m_returnPreloadPaceSec > 0.f
            || m_deferBackdropPreloadSec > 0.f
            || m_iconStreamer.softLandingActive();
        m_steamGridDbBackdrop->setPreloadPaused(
            paceWindow && app().input().hasUserActivity());
        // Hero decode finishes async — re-check contrast when art lands/clears.
        updateHeroAwareChrome();
    }

    // Own borrowed folder peeks before IconStreamer overwrites those GPU slots
    // with posters. Prefer spreading decode across the morph; drain any
    // remainder before the post-morph neighbour burst.
    if (m_grid && !m_folderPeekBorrowed.empty() && preferSingleRowPosters()) {
        const bool morphing = m_grid->isLayoutMorphing();
        const int budget = morphing ? 2 : (m_pendingNeighborArtBurst ? 8 : 1);
        if (promoteBorrowedFolderPeeks(budget) > 0) {
            applyFolderCoversToIcons(m_grid->focusedGlobalIndex(), 4);
        }
    }

    if (m_retainLayoutFlipFocus)
        restoreLayoutFlipFocusIfNeeded();

    if (m_pendingNeighborArtBurst && m_grid && !m_grid->isLayoutMorphing()) {
        // Last chance: do not let onPageChanged recycle textures still aliased
        // by folder peek stacks.
        if (!m_folderPeekBorrowed.empty() && preferSingleRowPosters()) {
            if (promoteBorrowedFolderPeeks(16) > 0)
                applyFolderCoversToIcons(m_grid->focusedGlobalIndex(), 4);
        }
        m_pendingNeighborArtBurst = false;
        // Far folder peeks only — nearby warm radius already applied on commit.
        m_pendingFolderCoverFill = true;
        m_pendingFolderCoverNext = 0;
        // Keep whatever skip radius the commit/sync path armed.
        // No startBurst — neighbours often already warmed during the morph.
        int page = 0, pageSize = 1;
        if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
            page = std::max(0, m_grid->focusedGlobalIndex());
            pageSize = 1;
        } else {
            page = m_grid->currentPage();
            pageSize = std::max(1, m_grid->iconsPerPage());
        }
        m_iconStreamer.onPageChanged(page, pageSize, app().gpu(), app().renderer(),
                                     m_grid->allIcons());
    }

    // Composites: disk/async — do not wait on soft-land / hero preload.
    if (m_folderCoverFillDelayFrames > 0
        && !leaveSplashActive()
        && m_deferredInitialAssetFrames <= 0) {
        --m_folderCoverFillDelayFrames;
        if (m_folderCoverFillDelayFrames == 0) {
            m_pendingFolderCoverFill = true;
            m_pendingFolderCoverNext = 0;
            ensureFolderCoverFillDoneSize();
            const int focus = m_grid ? m_grid->focusedGlobalIndex() : -1;
            const int skipRadius = m_pendingFolderCoverSkipRadius;
            if (skipRadius >= 0 && focus >= 0) {
                const int total = m_model.count();
                for (int i = std::max(0, focus - skipRadius);
                     i < std::min(total, focus + skipRadius + 1); ++i) {
                    if (m_model.at(i).isFolder())
                        markFolderCoverFilled(i);
                }
            }
            DebugLog::log("[leave] folder peek fill armed after delay (outward from focus)");
        }
    }

    // One folder/frame from live focus; composites attach via pump when baked.
    if (m_pendingFolderCoverFill && m_grid
        && m_deferredInitialAssetFrames <= 0
        && m_folderCoverFillDelayFrames <= 0
        && !leaveSplashActive()) {
        const int focus = m_grid->focusedGlobalIndex();
        const int index = nextFolderCoverFillIndex(focus);
        if (index < 0)
            m_pendingFolderCoverFill = false;
        else
            applyFolderCoversToIcons(index, 0);
    }

    if (m_folderCaptureReady)
        openCapturedFolder();

    // Periodically re-check whether the automatic day/night theme should switch.
    // Cheap: ClockService::refresh() is cached and shared with the clock widget.
    if (m_autoTheme.mode() != switchu::services::AutoThemeMode::Off) {
        pollGeoLocationFetch();
        maybeFetchGeoLocation();
        m_autoThemeCheckTimer += dt;
        if (m_autoThemeCheckTimer >= 30.f) {
            m_autoThemeCheckTimer = 0.f;
            evaluateAutoTheme(false);
        }
    }

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

        {
            const float d = dt / kPageArrowFade;
            const bool showDelete = deletePageAvailable();
            m_deletePageShow = std::clamp(
                m_deletePageShow + (showDelete ? d : -d), 0.f, 1.f);
            if (!showDelete) {
                m_touchDeletePage = false;
                m_deletePageHold = 0.f;
                m_deletePageMinusArmed = false;
            }
        }

        if (m_addPageMode) {
            const bool holding = m_addPageTouchHold
                              || app().input().isHeld(nxui::Button::ZR);
            if (holding) {
                m_addPageHold = std::min(1.f, m_addPageHold + dt / kAddPageHoldDur);
                if (m_addPageHold >= 1.f) {
                    m_addPageHold = 0.f;
                    m_addPageTouchHold = false;
                    createPage();
                }
            } else {
                m_addPageHold = std::max(0.f, m_addPageHold - dt / (kAddPageHoldDur * 0.4f));
            }
        } else {
            m_addPageHold = 0.f;
            m_addPageTouchHold = false;
        }

        // Hold Minus (Select) on an empty page to open the delete-page menu —
        // matches the on-screen − control. Short Minus still switches view.
        if (deletePageAvailable()) {
            if (app().input().isDown(nxui::Button::Minus)
                && !app().input().isDown(nxui::Button::Plus))
                m_deletePageMinusArmed = true;
            const bool holdingMinus = m_deletePageMinusArmed
                && app().input().isHeld(nxui::Button::Minus)
                && !app().input().isDown(nxui::Button::Plus);
            if (holdingMinus) {
                m_deletePageHold = std::min(1.f, m_deletePageHold + dt / kAddPageHoldDur);
                if (m_deletePageHold >= 1.f) {
                    m_deletePageHold = 0.f;
                    m_deletePageMinusArmed = false;
                    showDeletePageDialog();
                }
            } else {
                if (m_deletePageMinusArmed && app().input().isUp(nxui::Button::Minus)
                    && m_deletePageHold < 0.35f) {
                    // Short tap: same as normal Minus — switch grid view.
                    toggleAppLayoutMode();
                }
                m_deletePageHold = std::max(0.f,
                    m_deletePageHold - dt / (kAddPageHoldDur * 0.4f));
                if (!app().input().isHeld(nxui::Button::Minus))
                    m_deletePageMinusArmed = false;
            }
        } else {
            m_deletePageHold = 0.f;
            m_deletePageMinusArmed = false;
        }
    }

    const bool sliding = m_grid && m_grid->isTransitioning();
    if (sliding != m_gridSliding) {
        m_gridSliding = sliding;
        if (sliding) {
            if (m_cursor) m_cursor->setVisible(false);
        } else {
            snapCursorToFocus();
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

    updateLayoutTransition(dt);

    int streamPage = 0;
    int streamPageSize = 1;
    if (m_layoutTransition.kind != LayoutTransitionKind::Idle)
        layoutPrefetchWindow(streamPage, streamPageSize);
    else if (m_grid && m_appLayoutMode == AppLayoutMode::DynamicLine) {
        streamPage = std::max(0, m_grid->focusedGlobalIndex());
        streamPageSize = 1;
    } else if (m_grid) {
        streamPage = m_grid->currentPage();
        streamPageSize = m_grid->iconsPerPage();
    }
    if (m_deferredInitialAssetFrames == 0 && m_grid && m_iconStreamer.needsVisibleLoads(
            streamPage, streamPageSize)
        && m_layoutTransition.kind == LayoutTransitionKind::Idle) {
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
                    finishProfileLoading();
            }
        } catch (const std::exception& ex) {
            DebugLog::log("[profiles] deferred enumeration failed: %s", ex.what());
            finishProfileLoading();
        } catch (...) {
            DebugLog::log("[profiles] deferred enumeration failed: unknown exception");
            finishProfileLoading();
        }
        m_deferredProfileList.reset();
    }

    if (m_configSaveDeferred && m_configSaveFuture.valid()
        && m_configSaveFuture.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        try { m_configSaveFuture.get(); } catch (...) {}
        m_configSaveDeferred = false;
        m_configSaveFuture = m_threadPool.submit([config = m_config]() {
            config.save();
        });
    }

    if (m_deferredProfileFrames > 0) {
        --m_deferredProfileFrames;
    } else if (m_pendingProfileIndex < m_pendingProfileUids.size()) {
        const bool visibleIconsBusy = m_grid && m_iconStreamer.needsVisibleLoads(
            streamPage, streamPageSize);
        if (!visibleIconsBusy)
            loadNextProfile();
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
        if (t >= 0.98f && m_audio.isPlaying()) {
            persistMusicPlaybackState();
            m_audio.pause();
        }
    } else if (m_musicFadeActive) { // cancel fade if the animation was interrupted
        m_musicFadeActive = false;
        m_audio.setMusicFade(1.f);
    }

    if (m_audioStarted)
        m_audio.update(dt);

    if (m_quickSettings && m_quickSettings->isActive())
        m_quickSettings->refreshMusicState();

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
                int page = 0, pageSize = 1;
                if (m_appLayoutMode == AppLayoutMode::DynamicLine) {
                    page = std::max(0, m_grid->focusedGlobalIndex());
                    pageSize = 1;
                } else {
                    page = m_grid->currentPage();
                    pageSize = std::max(1, m_grid->iconsPerPage());
                }
                m_iconStreamer.onPageChanged(page, pageSize,
                                             app().gpu(), app().renderer(),
                                             m_grid->allIcons());
            }
            m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS,
                                     resolveThemeAssetPath(m_effectivePreset,
                                                           m_effectivePreset.icons.basePath));
            DebugLog::log("[init] deferred initial icon/sidebar uploads done");
        }
    }

    // Public HTTPS clock sync once per boot (failure toast only; retry until online).
    if (m_config.webClockSyncEnabled && !m_webClockBootSyncStarted) {
        if (leaveSplashActive() || m_deferredInitialAssetFrames > 0) {
            m_webClockBootSyncDelayFrames =
                std::max(m_webClockBootSyncDelayFrames, 90);
        } else if (m_webClockBootSyncDelayFrames > 0) {
            --m_webClockBootSyncDelayFrames;
        } else if (!themeshop::http::isInternetReadyForHttp()) {
            m_webClockBootSyncDelayFrames = 45;
        } else {
            m_webClockBootSyncStarted = true;
            createSettings();
            if (m_settings) {
                m_settings->setWebClockSyncState(true);
                DebugLog::log("[boot] starting public web clock sync");
                m_settings->startWebClockSync(true);
            }
        }
    }
    if (m_settings)
        m_settings->pollClockSync();

    // Finish music start only after leave-splash + deferred assets. Doing
    // Mix_PlayMusic/seek in the same burst as sidebar/icon GPU uploads was
    // freezing AppletReturn (menu.log stopped right after deferred uploads).
    if (m_audioPlaybackRestorePending
        && m_audioStarted
        && !leaveSplashActive()
        && m_deferredInitialAssetFrames == 0) {
        if (m_audioPlaybackRestoreDelayFrames > 0)
            --m_audioPlaybackRestoreDelayFrames;
        if (m_audioPlaybackRestoreDelayFrames == 0) {
            DebugLog::log("[boot] restoring music playback");
            m_audioPlaybackRestorePending = false;
            if (m_audio.reconcileResumeConfig(m_config.musicTrackIndex,
                                              m_config.musicPositionSeconds,
                                              m_config.musicTrackKey,
                                              m_config.musicPlaylistOrder)) {
                DebugLog::log(
                    "[boot] music resume config reconciled key=%s index=%d pos=%.2f order=%zu",
                    m_config.musicTrackKey.c_str(),
                    m_config.musicTrackIndex,
                    m_config.musicPositionSeconds,
                    m_config.musicPlaylistOrder.size());
                if (!m_config.musicPlaylistOrder.empty())
                    m_audio.applyPlaylistOrder(m_config.musicPlaylistOrder);
                m_config.save();
            }
            DebugLog::log(
                "[boot] music restore target key=%s index=%d pos=%.2f enabled=%d",
                m_config.musicTrackKey.c_str(),
                m_config.musicTrackIndex,
                m_config.musicPositionSeconds,
                m_config.musicEnabled ? 1 : 0);
            m_audio.restorePlaybackState(m_config.musicTrackIndex,
                                         m_config.musicPositionSeconds,
                                         m_config.musicEnabled,
                                         m_config.musicTrackKey);
            // Hard seeks run from AudioManager::update after a short warm-up so
            // Mix_SetMusicPosition is not on the splash/GPU-upload frame (deep
            // MP3 seeks have frozen AppletReturn). Unsafe seeks are dropped
            // inside sanitizeResumeSeekUnlocked — resume still picks the track.
            m_audioSeekAfterHoldoff = m_audio.hasDeferredSeek();
            DebugLog::log("[boot] music playback restore returned deferredSeek=%d",
                          m_audio.hasDeferredSeek() ? 1 : 0);
        }
    } else if (m_audioSeekAfterHoldoff
               && !leaveSplashActive()
               && !m_audio.hasDeferredSeek()) {
        DebugLog::log("[boot] deferred music seek settled");
        m_audioSeekAfterHoldoff = false;
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
        DebugLog::log("[boot] audio future ready — applying volume/playlist (playback delayed)");
        m_audioFuture.get();
        m_audio.setVolume(m_config.musicVolume);
        m_audio.setSfxVolume(m_config.sfxVolume);
        m_audio.setShuffle(m_config.musicShuffle);
        m_audio.setRepeatMode(static_cast<MusicRepeatMode>(
            std::clamp(m_config.musicRepeatMode, 0, 2)));
        if (!m_config.musicPlaylistOrder.empty())
            m_audio.applyPlaylistOrder(m_config.musicPlaylistOrder);
        m_loadedSoundPreset = resolveSoundPresetId(m_config.soundPreset);
        m_audioStarted = true;
        m_audioPlaybackRestorePending = true;
        // Music was never the deferred spike — resume as soon as splash clears.
        m_audioPlaybackRestoreDelayFrames = 0;
        DebugLog::log("[init] Audio ready (deferred); playback restore armed delay=%d",
                      m_audioPlaybackRestoreDelayFrames);
    }

    if (m_presetChangePending && m_audioFuture.valid() &&
        m_audioFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_audioFuture.get();
        m_audio.setVolume(m_config.musicVolume);
        m_audio.setSfxVolume(m_config.sfxVolume);
        m_audio.setShuffle(m_config.musicShuffle);
        m_audio.setRepeatMode(static_cast<MusicRepeatMode>(
            std::clamp(m_config.musicRepeatMode, 0, 2)));
        if (!m_config.musicPlaylistOrder.empty())
            m_audio.applyPlaylistOrder(m_config.musicPlaylistOrder);
        // A preset change swaps the whole playlist, so the saved index/position
        // can point past the new tracks. Start the new theme's music from its
        // first track, audibly: restorePlaybackState mutes until its deferred
        // seek lands, and nothing applies that seek outside the boot path —
        // which is why changing theme used to leave the menu silent until the
        // console was restarted.
        m_audio.restorePlaybackState(0, 0.f, m_config.musicEnabled);
        m_config.musicTrackIndex = 0;
        m_config.musicPositionSeconds = 0.f;
        m_config.musicTrackKey = m_audio.currentTrackKey();
        m_audioSeekAfterHoldoff = false;
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
        scheduleLeaveCapture([this]() { m_launcher.launchNetConnect(); });
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
                // Already on the menu — dismiss open folders/overlays. Do not
                // reuse HomeButton: that path re-focuses a title and can reopen
                // the folder (and re-capture a dimmed frosted backdrop).
                m_sysMsg.pushAction(SysAction::HomeDismiss);
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
                    if (m_quickSettings)
                        m_quickSettings->setBatteryStatus(
                            static_cast<int>(percent), charging);
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
            case switchu::smi::MenuMessage::SleepSequence:
                // The daemon announces the sleep while the applet can still
                // react: stop presenting, pause the music and park the frame
                // work before the console stops responding.
                enterSystemSleep();
                break;
            case switchu::smi::MenuMessage::WakeUp:
                leaveSystemSleep();
                m_clockService.invalidate();
                // Time may have jumped across the day/night boundary while
                // asleep; force an immediate re-evaluation on the next check.
                m_autoThemeCheckTimer = 30.f;
                evaluateAutoTheme(true);
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

    if (!app().input().isHeld(nxui::Button::Plus) || !app().input().isHeld(nxui::Button::Minus))
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
        !(m_quickSettings && m_quickSettings->isActive()) &&
        !(m_textEntry && m_textEntry->isActive()) &&
        !(m_settings && m_settings->isActive()) &&
        !(m_themeShop && m_themeShop->isActive()) &&
        !(m_gameOptions && m_gameOptions->isActive()) &&
        !(m_folderOptions && m_folderOptions->isActive()) &&
        !(m_controllerTest && m_controllerTest->isActive()) &&
        !(m_profileCarousel && m_profileCarousel->isActive()) &&
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

    // When an empty-page delete control is up, Minus is armed for hold-to-delete
    // (handled above). Skip the instant view toggle so a hold is not also a
    // layout switch; short tap still toggles on release.
    if (app().input().isDown(nxui::Button::Minus) &&
        !app().input().isDown(nxui::Button::Plus) &&
        !deletePageAvailable() &&
        m_navigator.route() == switchu::navigation::Route::Home &&
        !m_editMode &&
        !(m_contextMenu && m_contextMenu->isActive()) &&
        !(m_dialog && m_dialog->isActive()) &&
        !(m_quickSettings && m_quickSettings->isActive()) &&
        !(m_settings && m_settings->isActive()) &&
        !(m_themeShop && m_themeShop->isActive()) &&
        !(m_gameOptions && m_gameOptions->isActive()) &&
        !(m_folderOptions && m_folderOptions->isActive()) &&
        !(m_controllerTest && m_controllerTest->isActive()) &&
        !(m_profileCarousel && m_profileCarousel->isActive()) &&
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
        && !m_folderClosing
        && !(m_contextMenu && m_contextMenu->isActive())
        && !(m_dialog && m_dialog->isActive())
        && !(m_quickSettings && m_quickSettings->isActive())
        && !(m_textEntry && m_textEntry->isActive())
        && !(m_themeShop && m_themeShop->isActive())
        && !(m_settings && m_settings->isActive())
        && !(m_gameOptions && m_gameOptions->isActive())
        && !(m_folderOptions && m_folderOptions->isActive())
        && !(m_controllerTest && m_controllerTest->isActive())
        && !(m_profileCarousel && m_profileCarousel->isActive())
        && !(m_userSelect && m_userSelect->isActive())
        && !(m_progressDialog && m_progressDialog->isActive()))
    {
        handleTouch();
    }

    bool dialogActiveNow = (m_dialog && m_dialog->isActive());
    const bool textEntryActive = m_textEntry && m_textEntry->isActive();
    if (!debugTouchBlocked && !textEntryActive &&
        m_contextMenu && m_contextMenu->isActive())
        m_contextMenu->handleTouch(app().input());
    if (!debugTouchBlocked && !textEntryActive && dialogActiveNow)
        m_dialog->handleTouch(app().input());

    if (!debugTouchBlocked && !textEntryActive &&
        m_themeShop && m_themeShop->isActive())
        m_themeShop->handleTouch(app().input());

    if (!debugTouchBlocked && !textEntryActive && m_settings && m_settings->isActive()
        && !(m_controllerTest && m_controllerTest->isActive())) {
        m_settings->handleTouch(app().input());
    }

    if (!debugTouchBlocked && !textEntryActive && m_gameOptions && m_gameOptions->isActive()
        && !(m_steamGridDbPicker && m_steamGridDbPicker->isActive()))
        m_gameOptions->handleTouch(app().input());

    if (!debugTouchBlocked && !textEntryActive &&
        m_steamGridDbPicker && m_steamGridDbPicker->isActive())
        m_steamGridDbPicker->handleTouch(app().input());

    if (!debugTouchBlocked && !textEntryActive &&
        m_folderOptions && m_folderOptions->isActive()
        && !(m_steamGridDbPicker && m_steamGridDbPicker->isActive()))
        m_folderOptions->handleTouch(app().input());

    if (!debugTouchBlocked && !textEntryActive &&
        m_controllerTest && m_controllerTest->isActive())
        m_controllerTest->handleTouch(app().input());

    if (!debugTouchBlocked && !textEntryActive &&
        m_profileCarousel && m_profileCarousel->isActive())
        m_profileCarousel->handleTouch(app().input());

    if (!debugTouchBlocked && m_textEntry && m_textEntry->isActive())
        m_textEntry->handleTouch(app().input());

    if (!debugTouchBlocked && m_progressDialog && m_progressDialog->isActive())
        m_progressDialog->handleTouch(app().input());

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
        && !(m_quickSettings && m_quickSettings->isActive())
        && !(m_textEntry && m_textEntry->isActive())
        && !m_launchAnim->isPlaying())
    {
        auto* cur = focusManager().current();
        if (!cur || !cur->isFocusable()) {
            if (m_themeShop && m_themeShop->isActive()) {
                focusManager().setFocus(m_themeShop.get());
            } else if (m_controllerTest && m_controllerTest->isActive()) {
                focusManager().setFocus(m_controllerTest.get());
            } else if (m_profileCarousel && m_profileCarousel->isOpen()) {
                focusManager().setFocus(m_profileCarousel.get());
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

    nxui::AnimationManager::instance().update(motionDt);

    // In dynamic-line mode, and while a layout morph is in flight, the focused
    // widget itself is moving. Sample its interpolated display rectangle every
    // frame so the focus ring remains attached to the app throughout.
    if (m_grid && (m_grid->isDynamicLine() || m_grid->isLayoutMorphing())) {
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
        add(buttonGlyph(nxui::Button::LStick), i18n.tr("hint.repeat", "Repeat"));
        add(buttonGlyph(nxui::Button::Plus) + buttonGlyph(nxui::Button::Minus),
            i18n.tr("hint.voice", "Voice"));
    };

    if (m_launchAnim && m_launchAnim->isPlaying())
        return hints;

    if (m_quickSettings && m_quickSettings->isActive()) {
        if (m_quickSettings->isCoverFullscreen()) {
            add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
            add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
            add(buttonGlyph(nxui::Button::ZL), i18n.tr("quicksettings.prev", "Prev"));
            add(buttonGlyph(nxui::Button::ZR), i18n.tr("quicksettings.next", "Next"));
            add(buttonGlyph(nxui::Button::R), i18n.tr("quicksettings.play_pause", "Play/Pause"));
            add(buttonGlyph(nxui::Button::LStick), i18n.tr("quicksettings.repeat", "Repeat"));
            add(buttonGlyph(nxui::Button::RStick), i18n.tr("quicksettings.shuffle", "Shuffle"));
            add(buttonGlyph(nxui::Button::Plus),
                i18n.tr("quicksettings.playlist", "Playlist"));
            add(buttonGlyph(nxui::Button::B) + buttonGlyph(nxui::Button::Minus),
                i18n.tr("hint.close", "Close"));
            return hints;
        }
        if (m_quickSettings->isPlaylistReorderMode()) {
            add(dpadGlyph(), i18n.tr("hint.move", "Move"));
            add(buttonGlyph(nxui::Button::A), i18n.tr("hint.place", "Place"));
            add(buttonGlyph(nxui::Button::B), i18n.tr("hint.cancel", "Cancel"));
        } else if (m_quickSettings->isPlaylistOpen()) {
            add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
            add(buttonGlyph(nxui::Button::A), i18n.tr("hint.select", "Select"));
            if (!m_quickSettings->hasAlbumFolders())
                add(buttonGlyph(nxui::Button::Y), i18n.tr("hint.move", "Move"));
            add(buttonGlyph(nxui::Button::X), i18n.tr("hint.artwork", "Art"));
            add(buttonGlyph(nxui::Button::Plus), i18n.tr("quicksettings.playlist", "Playlist"));
            if (m_quickSettings->isCoverFullscreen() == false)
                add(buttonGlyph(nxui::Button::Minus), i18n.tr("hint.fullscreen", "Fullscreen"));
            add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        } else {
            // Lean legend: Navigate / Select / Close are obvious here. L3/R3
            // cycle repeat + toggle shuffle ("Modes").
            add(buttonGlyph(nxui::Button::ZL) + buttonGlyph(nxui::Button::ZR),
                i18n.tr("hint.skip", "Skip"));
            add(buttonGlyph(nxui::Button::R), i18n.tr("quicksettings.play_pause", "Play/Pause"));
            add(buttonGlyph(nxui::Button::LStick) + buttonGlyph(nxui::Button::RStick),
                i18n.tr("hint.modes", "Modes"));
            add(buttonGlyph(nxui::Button::Plus), i18n.tr("quicksettings.playlist", "Playlist"));
            add(buttonGlyph(nxui::Button::Minus), i18n.tr("hint.fullscreen", "Fullscreen"));
        }
        return hints;
    }

    if (m_textEntry && m_textEntry->isActive())
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

    if (m_progressDialog && m_progressDialog->isActive()) {
        if (m_progressDialog->isCancellable())
            add(buttonGlyph(nxui::Button::B), i18n.tr("hint.cancel", "Cancel"));
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

    if (m_profileCarousel && m_profileCarousel->isActive()) {
        using Kind = ProfileCarouselScreen::CardKind;
        const Kind kind = m_profileCarousel->selectedKind();
        add(dpadGlyph(), i18n.tr("hint.navigate", "Navigate"));
        if (kind == Kind::Profile)
            add(buttonGlyph(nxui::Button::A), i18n.tr("profilelock.open_profile", "View profile"));
        else if (kind == Kind::AddUser)
            add(buttonGlyph(nxui::Button::A), i18n.tr("profilelock.create_user", "Create"));
        if (kind != Kind::AddUser && !m_profileCarousel->selectedIsLocked())
            add(buttonGlyph(nxui::Button::X), kind == Kind::Nobody
                ? i18n.tr("profilelock.always_ask", "Always ask")
                : i18n.tr("profilelock.set_default", "Set as default"));
        add(buttonGlyph(nxui::Button::B), i18n.tr("hint.back", "Back"));
        addVoiceControls();
        return hints;
    }

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
    else
        add(buttonGlyph(nxui::Button::L),
            i18n.tr("quicksettings.hint_shortcut", "Quick Settings"));

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
                if (m_openFolderId == 0 &&
                    m_appLayoutMode != AppLayoutMode::DynamicLine)
                    add(buttonGlyph(nxui::Button::R), sortModeLabel());
                if (m_openFolderId == 0)
                    add(buttonGlyph(nxui::Button::Y), i18n.tr("hint.move", "Move"));
                else
                    add(buttonGlyph(nxui::Button::Y), i18n.tr("folder.move", "Move"));
            }
        } else if (m_openFolderId == 0 && deletePageAvailable()) {
#ifdef SWITCHU_MENU
            add(buttonGlyph(nxui::Button::A), i18n.tr("page.delete", "Delete page"));
            add(buttonGlyph(nxui::Button::Plus), i18n.tr("add.title", "Add"));
#else
            add(buttonGlyph(nxui::Button::A), i18n.tr("page.delete", "Delete page"));
#endif
        } else if (m_openFolderId != 0 && deletePageAvailable()) {
            add(buttonGlyph(nxui::Button::A), i18n.tr("page.delete", "Delete page"));
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
        if (m_profileLock && m_profileLock.get() == cur)
            add(buttonGlyph(nxui::Button::A), i18n.tr("profilelock.choose", "Change profile"));
        if (m_layoutFlip && m_layoutFlip.get() == cur)
            add(buttonGlyph(nxui::Button::A), i18n.tr("hint.switch_layout", "Switch view"));
        if (m_battery && m_battery.get() == cur)
            add(buttonGlyph(nxui::Button::A), i18n.tr("quick_settings.title", "Quick Settings"));
    }

    if (m_navigator.route() == switchu::navigation::Route::Home && !m_editMode) {
        if (deletePageAvailable())
            add(buttonGlyph(nxui::Button::Minus),
                i18n.tr("page.minus_hint", "View · hold delete"));
        else
            add(buttonGlyph(nxui::Button::Minus),
                i18n.tr("hint.switch_layout", "Switch view"));
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

    // Find the smallest possible number of rows, then choose the contiguous
    // split whose widest row is the narrowest. With only eight hints there are
    // at most 128 layouts to inspect, and this avoids an awkward 5+1 split.
    // QS-only (no playlist sheet): always one row — a second row sits on the
    // power buttons. Playlist-open already packs into one row under kHintRowMaxW.
    const bool qsForceOneRow = m_quickSettings && m_quickSettings->isActive()
        && !m_quickSettings->isCoverFullscreen()
        && !m_quickSettings->isPlaylistOpen();
    const float rowMaxW = qsForceOneRow ? 4096.f : kHintRowMaxW;
    std::vector<std::pair<int, int>> rows;
    int bestRowCount = count + 1;
    float bestScore = std::numeric_limits<float>::max();
    const unsigned splitLayouts = 1u << std::max(0, count - 1);
    for (unsigned mask = 0; mask < splitLayouts; ++mask) {
        std::vector<std::pair<int, int>> candidate;
        int first = 0;
        for (int i = 0; i < count; ++i) {
            if (i == count - 1 || (mask & (1u << i))) {
                candidate.emplace_back(first, i + 1);
                first = i + 1;
            }
        }

        const int rowCount = static_cast<int>(candidate.size());
        if (qsForceOneRow && rowCount != 1)
            continue;
        if (rowCount > bestRowCount)
            continue;

        bool valid = true;
        float widest = 0.f;
        float narrowest = std::numeric_limits<float>::max();
        for (const auto& [rowFirst, rowLast] : candidate) {
            float width = 0.f;
            for (int i = rowFirst; i < rowLast; ++i)
                width += m_hintCapsules[(size_t)i].width
                       + (i > rowFirst ? kHintCapGap : 0.f);
            if (width > rowMaxW && rowLast - rowFirst > 1) {
                valid = false;
                break;
            }
            widest = std::max(widest, width);
            narrowest = std::min(narrowest, width);
        }
        if (!valid)
            continue;

        const float score = widest + (widest - narrowest) * 0.15f;
        if (rowCount < bestRowCount || score < bestScore) {
            bestRowCount = rowCount;
            bestScore = score;
            rows = std::move(candidate);
        }
    }
    // Safety: if packing found nothing (should not), fall back to a single row.
    if (rows.empty())
        rows.push_back({0, count});

    const float reveal = std::clamp(m_hintContentReveal.value(), 0.f, 1.f);
    const float blockH = rows.size() * kHintCapH + (rows.size() - 1) * kHintRowGap;
    float y = 720.f - kHintEdgeY - blockH + (1.f - reveal) * 4.f;

    const nxui::Color tint = m_homeChrome.panelBase.withAlpha(
        m_homeChrome.mode == nxui::ThemeMode::Dark ? 0.30f : 0.24f);

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
                                 m_homeChrome.panelBorder.withAlpha(0.24f * reveal),
                                 m_homeChrome.panelHighlight.withAlpha(0.08f * reveal),
                                 radius, 0.86f);

            const nxui::Vec2 iconSize = m_fontIcons.measure(c.icon);
            const float iconW = iconSize.x * kHintIconScale;
            ren.drawText(c.icon,
                         {cap.x + kHintCapPadX,
                          cap.y + (kHintCapH - iconSize.y * kHintIconScale) * 0.5f},
                         &m_fontIcons, m_homeChrome.textPrimary.withAlpha(0.94f * reveal), kHintIconScale);

            const float textX = cap.x + kHintCapPadX + iconW + kHintIconGap;
            const float swap  = nxui::Easing::outCubic(std::clamp(c.swapT, 0.f, 1.f));

            ren.pushClipRect(cap);
            if (!c.outgoing.empty()) {
                const nxui::Vec2 os = m_fontSmall.measure(c.outgoing);
                ren.drawText(c.outgoing,
                             {textX - 7.f * swap,
                              cap.y + (kHintCapH - os.y * kHintTextScale) * 0.5f},
                             &m_fontSmall,
                             m_homeChrome.textSecondary.withAlpha(0.90f * reveal * (1.f - swap)),
                             kHintTextScale);
            }
            const nxui::Vec2 ls = m_fontSmall.measure(c.label);
            ren.drawText(c.label,
                         {textX + 7.f * (1.f - swap),
                          cap.y + (kHintCapH - ls.y * kHintTextScale) * 0.5f},
                         &m_fontSmall,
                         m_homeChrome.textSecondary.withAlpha(0.90f * reveal * swap),
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

    constexpr float kIconScale = 0.62f;
    constexpr float kTextScale = 0.50f;
    constexpr float kRowH = 20.f;
    constexpr float kRowGap = 3.f;
    constexpr float kPadX = 9.f;
    constexpr float kPadY = 7.f;
    constexpr float kIconTextGap = 5.f;
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

    float panelW = std::clamp(contentW + kPadX * 2.f, 98.f, 196.f);
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
    constexpr float kRadius = 14.f;
    const float reveal = std::clamp(m_hintContentReveal.value(), 0.f, 1.f);

    ren.drawRoundedRect({panel.x, panel.y + 4.f, panel.width, panel.height},
                        nxui::Color(0.f, 0.f, 0.f, 0.12f), kRadius);
    const nxui::Color tint = m_homeChrome.panelBase.withAlpha(
        m_homeChrome.mode == nxui::ThemeMode::Dark ? 0.22f : 0.18f);
    ren.drawFrostedInset(panel, tint,
                        m_homeChrome.panelBorder.withAlpha(0.26f),
                        m_homeChrome.panelHighlight.withAlpha(0.09f),
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
                     m_homeChrome.textPrimary.withAlpha(0.88f * reveal), kIconScale);
        ren.drawText(hint.label, {labelX, labelY}, &m_fontSmall,
                     m_homeChrome.textSecondary.withAlpha(0.82f * reveal), kTextScale);
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
    if (!m_grid || m_editMode)
        return false;
    if (m_navigator.route() != switchu::navigation::Route::Home || focusRoot() != &rootBox())
        return false;
    if (m_grid->currentPage() < m_grid->totalPages() - 1)
        return false;

    if (m_openFolderId != 0) {
        const auto* folder = m_folderStore.find(m_openFolderId);
        if (!folder || folder->pageCount >= switchu::folders::kMaxFolderPages)
            return false;
        return true;
    }

    return m_grid->totalPages() < kMaxHomePages;
}

bool WiiUMenuApp::currentPageEmpty() const {
    if (!m_grid || m_editMode)
        return false;
    if (m_appLayoutMode == AppLayoutMode::DynamicLine)
        return false;
    if (m_navigator.route() != switchu::navigation::Route::Home)
        return false;
    for (auto* icon : m_grid->pageIcons()) {
        if (icon && icon->titleId() != 0)
            return false;
    }
    return true;
}

bool WiiUMenuApp::deletePageAvailable() const {
    if (!currentPageEmpty() || !m_grid)
        return false;
    return m_grid->totalPages() > 1;
}

nxui::Rect WiiUMenuApp::deletePageButtonRect() const {
    constexpr float kSize = 64.f;
    if (!m_grid)
        return {640.f - kSize * 0.5f, 360.f - kSize * 0.5f, kSize, kSize};
    const nxui::Rect content = m_grid->contentRect();
    return {
        content.x + content.width * 0.5f - kSize * 0.5f,
        content.y + content.height * 0.5f - kSize * 0.5f,
        kSize,
        kSize
    };
}

void WiiUMenuApp::createPage() {
    if (!addPageAvailable() || !m_grid)
        return;

    if (m_openFolderId != 0) {
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
        // Keep titleIds padded to the declared page count so empty trailing
        // pages are real slots (delete-all can see and remove them).
        if (auto* folderMut = m_folderStore.find(m_openFolderId))
            folderMut->titleIds.resize(
                static_cast<std::size_t>((pages + 1) * perPage), 0);
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
        return;
    }

    const int cols = std::clamp(m_config.gridColumns, 1, 8);
    const int rows = std::clamp(m_config.gridRows, 1, 5);
    const int perPage = std::max(1, cols * rows);
    const int pages = m_grid->totalPages();
    if (pages >= kMaxHomePages)
        return;

    m_layoutSlots.resize(static_cast<std::size_t>((pages + 1) * perPage), 0);
    m_layoutDirty = true;
    saveMenuLayout();

    applyDisplayModel(buildRootFolderModel(), 0, false);
    syncPageIndicator();

    const int target = pages;
    m_grid->setPage(std::max(0, target - 1));
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

namespace {

bool pageSlotsEmpty(const std::vector<std::uint64_t>& slots, int page, int perPage) {
    if (perPage <= 0 || page < 0)
        return true;
    const int start = page * perPage;
    const int end = start + perPage;
    for (int index = start; index < end; ++index) {
        if (index < static_cast<int>(slots.size()) &&
            slots[static_cast<std::size_t>(index)] != 0)
            return false;
    }
    return true;
}

bool removeSlotPage(std::vector<std::uint64_t>& slots, int page, int perPage) {
    if (perPage <= 0 || page < 0)
        return false;
    const int pages = std::max(1,
        (static_cast<int>(slots.size()) + perPage - 1) / perPage);
    if (page >= pages || pages <= 1)
        return false;
    slots.resize(static_cast<std::size_t>(pages * perPage), 0);
    slots.erase(slots.begin() + page * perPage,
                slots.begin() + (page + 1) * perPage);
    return true;
}

int removeEmptySlotPages(std::vector<std::uint64_t>& slots, int perPage) {
    if (perPage <= 0)
        return 0;
    int pages = std::max(1,
        (static_cast<int>(slots.size()) + perPage - 1) / perPage);
    slots.resize(static_cast<std::size_t>(pages * perPage), 0);
    int removed = 0;
    for (int page = pages - 1; page >= 0 && pages - removed > 1; --page) {
        if (!pageSlotsEmpty(slots, page, perPage))
            continue;
        slots.erase(slots.begin() + page * perPage,
                    slots.begin() + (page + 1) * perPage);
        ++removed;
    }
    return removed;
}

} // namespace

void WiiUMenuApp::showDeletePageDialog() {
    if (!deletePageAvailable() || !m_dialog)
        return;

    auto& i18n = nxui::I18n::instance();
    m_audio.playSfx(Sfx::ModalShow);
    m_dialogReturnFocus = focusManager().current();
    m_dialog->show(
        i18n.tr("page.delete_title", "Delete page"),
        i18n.tr("page.delete_message", "This page is empty. What would you like to remove?"),
        {
            {i18n.tr("page.delete_this", "Delete this page"), [this]() {
                m_audio.playSfx(Sfx::ConfirmPositive);
                deleteCurrentPage();
            }, true},
            {i18n.tr("page.delete_unused", "Delete all unused pages"), [this]() {
                m_audio.playSfx(Sfx::ConfirmPositive);
                deleteAllUnusedPages();
            }, true},
            {i18n.tr("button.cancel", "Cancel"), [this]() {
                m_audio.playSfx(Sfx::ModalHide);
                if (m_dialogReturnFocus)
                    focusManager().setFocus(m_dialogReturnFocus);
                m_dialogReturnFocus = nullptr;
            }, true},
        },
        0,
        [this]() {
            if (m_dialogReturnFocus)
                focusManager().setFocus(m_dialogReturnFocus);
            m_dialogReturnFocus = nullptr;
        });
    focusManager().setFocus(m_dialog.get());
}

void WiiUMenuApp::deleteCurrentPage() {
    if (!m_grid || m_grid->totalPages() <= 1)
        return;
    m_dialogReturnFocus = nullptr;

    const int page = m_grid->currentPage();
    const int perPage = std::max(1, m_grid->iconsPerPage());

    if (m_openFolderId != 0) {
        auto* folder = m_folderStore.find(m_openFolderId);
        if (!folder)
            return;
        const int pages = std::max(folder->pageCount, m_grid->totalPages());
        folder->titleIds.resize(static_cast<std::size_t>(pages * perPage), 0);
        if (!removeSlotPage(folder->titleIds, page, perPage))
            return;
        folder->pageCount = std::max(1,
            (static_cast<int>(folder->titleIds.size()) + perPage - 1) / perPage);
        if (!saveFoldersOrReport("delete_folder_page"))
            return;

        const int focusPage = std::min(page, folder->pageCount - 1);
        applyDisplayModel(buildOpenFolderModel(m_openFolderId), 0, false);
        syncPageIndicator();
        m_grid->setPage(focusPage);
        if (m_grid->focusGlobalIndex(focusPage * perPage)) {
            if (auto* cur = m_grid->focusManager().current())
                focusManager().setFocus(cur);
        }
        m_accessibility.announce(nxui::I18n::instance().tr(
            "page.deleted", "Page deleted"), true, true);
        updateCursor();
        return;
    }

    if (!removeSlotPage(m_layoutSlots, page, perPage))
        return;
    m_layoutDirty = true;
    saveMenuLayout();

    const int newPages = std::max(1,
        (static_cast<int>(m_layoutSlots.size()) + perPage - 1) / perPage);
    const int focusPage = std::min(page, newPages - 1);
    applyDisplayModel(buildRootFolderModel(), 0, false);
    syncPageIndicator();
    m_grid->setPage(focusPage);
    if (m_grid->focusGlobalIndex(focusPage * perPage)) {
        if (auto* cur = m_grid->focusManager().current())
            focusManager().setFocus(cur);
    }
    m_accessibility.announce(nxui::I18n::instance().tr(
        "page.deleted", "Page deleted"), true, true);
    updateCursor();
}

void WiiUMenuApp::deleteAllUnusedPages() {
    if (!m_grid || m_grid->totalPages() <= 1)
        return;
    m_dialogReturnFocus = nullptr;

    const int perPage = std::max(1, m_grid->iconsPerPage());
    const int stayPage = m_grid->currentPage();

    if (m_openFolderId != 0) {
        auto* folder = m_folderStore.find(m_openFolderId);
        if (!folder)
            return;
        // Uninstalled titles still occupy slots in folders.json, so the page
        // looks empty in the UI while pageSlotsEmpty() saw it as occupied —
        // "delete all unused" then no-op'd until a second try. Drop missing
        // titles first so empty pages are actually empty.
        bool purgedMissing = false;
        for (auto& titleId : folder->titleIds) {
            if (titleId == 0)
                continue;
            const bool known = std::any_of(
                m_allApps.begin(), m_allApps.end(),
                [titleId](const AppEntry& app) { return app.titleId == titleId; });
            if (!known) {
                titleId = 0;
                purgedMissing = true;
            }
        }
        const int pages = std::max({
            folder->pageCount,
            m_grid->totalPages(),
            std::max(1, (static_cast<int>(folder->titleIds.size()) + perPage - 1) / perPage)
        });
        folder->titleIds.resize(static_cast<std::size_t>(pages * perPage), 0);

        // Count how many empty pages before stayPage so focus can shift left.
        int removedBefore = 0;
        for (int page = 0; page < stayPage; ++page) {
            if (pageSlotsEmpty(folder->titleIds, page, perPage))
                ++removedBefore;
        }
        const int removed = removeEmptySlotPages(folder->titleIds, perPage);
        const int newPageCount = std::max(1,
            (static_cast<int>(folder->titleIds.size()) + perPage - 1) / perPage);
        // pageCount alone can keep phantom empty pages alive even when titleIds
        // was already compact — always sync it. Also persist purged missing
        // titles even when no page was removed.
        const bool changed = removed > 0 || purgedMissing
            || folder->pageCount != newPageCount;
        folder->pageCount = newPageCount;
        if (!changed)
            return;
        if (!saveFoldersOrReport("delete_unused_folder_pages"))
            return;

        const int focusPage = std::clamp(stayPage - removedBefore, 0, folder->pageCount - 1);
        applyDisplayModel(buildOpenFolderModel(m_openFolderId), 0, false);
        syncPageIndicator();
        m_grid->setPage(focusPage);
        if (m_grid->focusGlobalIndex(focusPage * perPage)) {
            if (auto* cur = m_grid->focusManager().current())
                focusManager().setFocus(cur);
        }
        m_accessibility.announce(nxui::I18n::instance().tr(
            "page.unused_deleted", "Unused pages deleted"), true, true);
        updateCursor();
        return;
    }

    int removedBefore = 0;
    {
        const int pages = std::max(1,
            (static_cast<int>(m_layoutSlots.size()) + perPage - 1) / perPage);
        m_layoutSlots.resize(static_cast<std::size_t>(pages * perPage), 0);
        for (int page = 0; page < stayPage; ++page) {
            if (pageSlotsEmpty(m_layoutSlots, page, perPage))
                ++removedBefore;
        }
    }
    const int removed = removeEmptySlotPages(m_layoutSlots, perPage);
    if (removed <= 0)
        return;
    m_layoutDirty = true;
    saveMenuLayout();

    const int newPages = std::max(1,
        (static_cast<int>(m_layoutSlots.size()) + perPage - 1) / perPage);
    const int focusPage = std::clamp(stayPage - removedBefore, 0, newPages - 1);
    applyDisplayModel(buildRootFolderModel(), 0, false);
    syncPageIndicator();
    m_grid->setPage(focusPage);
    if (m_grid->focusGlobalIndex(focusPage * perPage)) {
        if (auto* cur = m_grid->focusManager().current())
            focusManager().setFocus(cur);
    }
    m_accessibility.announce(nxui::I18n::instance().tr(
        "page.unused_deleted", "Unused pages deleted"), true, true);
    updateCursor();
}

bool WiiUMenuApp::flipDynamicLinePage(int dir) {
    if (!m_grid || dir == 0 || m_layoutTransition.kind != LayoutTransitionKind::Idle)
        return false;

    const int perPage = std::max(1, m_grid->iconsPerPage());
    const int focus = m_grid->focusedGlobalIndex();
    if (focus < 0)
        return false;

    const auto& icons = m_grid->allIcons();
    const int totalIcons = static_cast<int>(icons.size());
    if (totalIcons <= 0)
        return false;

    auto nearestFocusable = [&](int prefer) -> int {
        if (prefer < 0) prefer = 0;
        if (prefer >= totalIcons) prefer = totalIcons - 1;
        if (icons[static_cast<std::size_t>(prefer)]
            && icons[static_cast<std::size_t>(prefer)]->isFocusable())
            return prefer;
        for (int dist = 1; dist < totalIcons; ++dist) {
            const int right = prefer + dist;
            if (right < totalIcons && icons[static_cast<std::size_t>(right)]
                && icons[static_cast<std::size_t>(right)]->isFocusable())
                return right;
            const int left = prefer - dist;
            if (left >= 0 && icons[static_cast<std::size_t>(left)]
                && icons[static_cast<std::size_t>(left)]->isFocusable())
                return left;
        }
        return -1;
    };

    // Predictable half-page landmarks: page start, page mid, … last title.
    // Shorter hops than a full page, and always landing on the same spots.
    std::vector<int> landmarks;
    landmarks.reserve(static_cast<std::size_t>(
        (totalIcons + perPage - 1) / perPage * 2 + 1));
    const int totalPages = std::max(1, (totalIcons + perPage - 1) / perPage);
    for (int page = 0; page < totalPages; ++page) {
        const int pageStart = page * perPage;
        const int pageEnd = std::min(totalIcons, pageStart + perPage);
        if (pageStart >= pageEnd)
            break;
        const int start = nearestFocusable(pageStart);
        if (start >= 0
            && (landmarks.empty() || landmarks.back() != start))
            landmarks.push_back(start);
        const int midPrefer = std::min(pageStart + perPage / 2, pageEnd - 1);
        const int mid = nearestFocusable(midPrefer);
        if (mid >= 0 && mid != start
            && (landmarks.empty() || landmarks.back() != mid))
            landmarks.push_back(mid);
    }
    const int last = nearestFocusable(totalIcons - 1);
    if (last >= 0
        && (landmarks.empty() || landmarks.back() != last))
        landmarks.push_back(last);
    if (landmarks.empty())
        return false;

    int best = -1;
    if (dir < 0) {
        for (int i = static_cast<int>(landmarks.size()) - 1; i >= 0; --i) {
            if (landmarks[static_cast<std::size_t>(i)] < focus) {
                best = landmarks[static_cast<std::size_t>(i)];
                break;
            }
        }
    } else {
        for (int landmark : landmarks) {
            if (landmark > focus) {
                best = landmark;
                break;
            }
        }
    }
    if (best < 0 || best == focus)
        return false;

    // Slightly slower than D-pad steps so the half-page glide reads clearly.
    constexpr float kTriggerScrollDuration = 0.48f;
    if (!m_grid->focusGlobalIndex(best, false, kTriggerScrollDuration))
        return false;
    if (auto* focused = m_grid->focusManager().current())
        focusManager().setFocus(focused);

    m_iconStreamer.onPageChanged(best, 1, app().gpu(), app().renderer(),
                                 m_grid->allIcons());
    m_audio.playSfx(Sfx::PageChange);
    kickPageArrow(dir);
    updateCursor();
    syncSteamGridDbLogoArea();
    showFocusedSteamGridDbArtwork(false);
    return true;
}

bool WiiUMenuApp::snapGridRowEdge(int dir) {
    if (!m_grid || dir == 0 || m_appLayoutMode == AppLayoutMode::DynamicLine)
        return false;
    const int cols = std::max(1, m_grid->columns());
    const int perPage = std::max(1, m_grid->iconsPerPage());
    const int page = m_grid->currentPage();
    const int focus = m_grid->focusedGlobalIndex();
    if (focus < 0)
        return false;

    const auto& icons = m_grid->allIcons();
    const int totalIcons = static_cast<int>(icons.size());
    if (totalIcons <= 0)
        return false;

    const int local = focus - page * perPage;
    const int row = std::clamp(local / cols, 0, std::max(0, m_grid->rowsPerPage() - 1));
    const int rowStart = page * perPage + row * cols;
    const int rowEnd = std::min(totalIcons - 1, rowStart + cols - 1);

    auto nearestFocusableInRow = [&](int prefer, int lo, int hi) -> int {
        prefer = std::clamp(prefer, lo, hi);
        if (prefer >= 0 && prefer < totalIcons
            && icons[static_cast<std::size_t>(prefer)]
            && icons[static_cast<std::size_t>(prefer)]->isFocusable())
            return prefer;
        for (int dist = 1; dist <= (hi - lo); ++dist) {
            const int right = prefer + dist;
            if (right <= hi && right < totalIcons
                && icons[static_cast<std::size_t>(right)]
                && icons[static_cast<std::size_t>(right)]->isFocusable())
                return right;
            const int left = prefer - dist;
            if (left >= lo
                && icons[static_cast<std::size_t>(left)]
                && icons[static_cast<std::size_t>(left)]->isFocusable())
                return left;
        }
        return -1;
    };

    const int target = (dir < 0)
        ? nearestFocusableInRow(rowStart, rowStart, rowEnd)
        : nearestFocusableInRow(rowEnd, rowStart, rowEnd);
    if (target < 0 || target == focus)
        return false;

    if (!m_grid->focusGlobalIndex(target, true))
        return false;
    if (auto* focused = m_grid->focusManager().current())
        focusManager().setFocus(focused);
    if (m_editMode)
        m_editTargetIndex = target;

    m_iconStreamer.onPageChanged(page, perPage, app().gpu(), app().renderer(),
                                 m_grid->allIcons());
    m_audio.playSfx(Sfx::PageChange);
    kickPageArrow(dir);
    updateCursor();
    return true;
}

bool WiiUMenuApp::flipPage(int dir) {
    if (!m_grid || m_grid->isTransitioning())
        return false;
    if (m_layoutTransition.kind != LayoutTransitionKind::Idle)
        return false;
    if (m_appLayoutMode == AppLayoutMode::DynamicLine)
        return flipDynamicLinePage(dir);

    const int p = m_grid->currentPage() + dir;
    if (p < 0 || p >= m_grid->totalPages()) {
        // Already on first/last page: ZL → row start, ZR → row end.
        return snapGridRowEdge(dir);
    }
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

void WiiUMenuApp::renderDeletePageButton(nxui::Renderer& ren) {
    if (m_deletePageShow <= 0.002f)
        return;
    if ((m_dialog && m_dialog->isActive()) ||
        (m_progressDialog && m_progressDialog->isActive()) ||
        (m_contextMenu && m_contextMenu->isActive()) ||
        (m_textEntry && m_textEntry->isActive()) ||
        (m_userSelect && m_userSelect->isActive()) ||
        (m_settings && m_settings->isActive()) ||
        (m_themeShop && m_themeShop->isActive()) ||
        (m_gameOptions && m_gameOptions->isActive()) ||
        (m_folderOptions && m_folderOptions->isActive()) ||
        (m_quickSettings && m_quickSettings->isActive()) ||
        (m_steamGridDbPicker && m_steamGridDbPicker->isActive()) ||
        (m_profileCarousel && m_profileCarousel->isActive()))
        return;

    const float e = m_deletePageShow * m_deletePageShow * (3.f - 2.f * m_deletePageShow);
    const nxui::Rect base = deletePageButtonRect();
    const float grow = 0.10f * m_deletePageHold;
    const float scale = 1.f + grow;
    const float cx = base.x + base.width * 0.5f;
    const float cy = base.y + base.height * 0.5f;
    const float ring = std::min(base.width, base.height) * 0.42f * scale;

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

    if (m_deletePageHold > 0.002f) {
        constexpr int kSegments = 44;
        const float rr = ring + 3.5f;
        const int lit = std::max(1, (int)std::ceil(kSegments * m_deletePageHold));
        const nxui::Color arc = m_theme.cursorNormal.withAlpha(0.95f * e);
        for (int i = 0; i < lit; ++i) {
            const float a0 = -1.5707963f + 6.2831853f * (float)i / kSegments;
            const float a1 = -1.5707963f + 6.2831853f * (float)(i + 1) / kSegments;
            ren.drawLine({cx + std::cos(a0) * rr, cy + std::sin(a0) * rr},
                         {cx + std::cos(a1) * rr, cy + std::sin(a1) * rr},
                         arc, 3.f);
        }
    }

    // Match the on-screen −: Minus glyph sits under the circle (hold to delete).
    constexpr float kGlyphScale = 0.70f;
    const std::string glyph = buttonGlyph(nxui::Button::Minus);
    const nxui::Vec2 gs = m_fontIcons.measure(glyph);
    ren.drawText(glyph, {cx - gs.x * kGlyphScale * 0.5f, cy + ring + 6.f},
                 &m_fontIcons, m_theme.textPrimary.withAlpha(0.9f * e), kGlyphScale);
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
    if (leaveSplashActive() && m_leaveSplashTex.valid()) {
        const float alpha = (m_leaveSplashPhase == LeaveSplashPhase::Hold)
            ? 1.f
            : m_leaveSplashFade;
        ren.drawTexture(&m_leaveSplashTex, {0.f, 0.f, 1280.f, 720.f},
                        nxui::Color::white().withAlpha(alpha));
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
    if (m_editMode && m_editGhostIcon) {
        const nxui::Rect gr = m_editGhostIcon->rect();
        ren.drawRoundedRect({gr.x + 2.f, gr.y + 12.f, gr.width, gr.height},
                            nxui::Color(0.02f, 0.04f, 0.06f, 0.32f),
                            m_editGhostIcon->cornerRadius() + 2.f);
        m_editGhostIcon->render(ren);
    }

    renderPageArrows(ren);
    renderDeletePageButton(ren);
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
