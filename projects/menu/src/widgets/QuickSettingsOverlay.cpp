#include "QuickSettingsOverlay.hpp"
#include "core/DebugLog.hpp"

#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Animation.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
#include <switch/services/ts.h>
#include <switch/services/tc.h>
#endif

namespace {

#ifdef __SWITCH__
class SettingsNifmSystemSession {
public:
    ~SettingsNifmSystemSession() {
        serviceClose(&m_general);
        serviceClose(&m_static);
    }

    Result open() {
        Result rc = smGetService(&m_static, "nifm:s");
        if (R_SUCCEEDED(rc))
            rc = serviceConvertToDomain(&m_static);
        if (R_SUCCEEDED(rc)) {
            const u64 reserved = 0;
            serviceAssumeDomain(&m_static);
            rc = serviceDispatchIn(&m_static, 5, reserved,
                .in_send_pid = true,
                .out_num_objects = 1,
                .out_objects = &m_general,
            );
        }
        return rc;
    }

    Result getWirelessCommunicationEnabled(bool* out) {
        u8 value = 0;
        serviceAssumeDomain(&m_general);
        Result rc = serviceDispatchOut(&m_general, 17, value);
        if (R_SUCCEEDED(rc) && out)
            *out = (value & 1) != 0;
        return rc;
    }

    Result setWirelessCommunicationEnabled(bool enabled) {
        const u8 value = enabled ? 1 : 0;
        serviceAssumeDomain(&m_general);
        return serviceDispatchIn(&m_general, 16, value);
    }

private:
    Service m_static{};
    Service m_general{};
};

static Result queryAirplaneMode(bool* outAirplaneMode) {
    SettingsNifmSystemSession session;
    Result rc = session.open();
    if (R_SUCCEEDED(rc)) {
        bool wirelessEnabled = true;
        rc = session.getWirelessCommunicationEnabled(&wirelessEnabled);
        if (R_SUCCEEDED(rc) && outAirplaneMode) {
            *outAirplaneMode = !wirelessEnabled;
        }
    }
    return rc;
}

static Result setAirplaneModeSetting(bool enableAirplaneMode) {
    SettingsNifmSystemSession session;
    Result rc = session.open();
    if (R_SUCCEEDED(rc)) {
        rc = session.setWirelessCommunicationEnabled(!enableAirplaneMode);
    }
    return rc;
}
#endif

static constexpr float kPanelWidth  = 440.f;
static constexpr float kPanelHeight = 652.f;
static constexpr float kPanelTop    = 16.f;
static constexpr float kPanelMarginRight = 20.f;
static constexpr float kPlaylistWidth = 300.f;
static constexpr float kAnimDuration = 0.25f;

// Uniform vertical rhythm: title breath, then the same gap between every widget.
static constexpr float kTitleY = 14.f;
static constexpr float kTitleLineH = 26.f;
static constexpr float kTitleBreath = 14.f;
static constexpr float kWidgetGap = 10.f;
static constexpr float kStatusH = 58.f;
static constexpr float kSliderItemH = 54.f;
static constexpr float kToggleH = 44.f;
static constexpr float kPowerBtnH = 52.f;
static constexpr float kPowerBtnGap = 8.f;
static constexpr float kPowerTextScale = 0.84f;
static constexpr float kPowerIconScale = 0.96f;
static constexpr float kPowerMoonScale = 1.18f;
static constexpr float kPowerIconGap = 7.f;

static constexpr float kStatusY = kTitleY + kTitleLineH + kTitleBreath;
static constexpr float kMusicY = kStatusY + kStatusH + kWidgetGap;
static constexpr float kMusicH = 136.f;
static constexpr float kBgmY = kMusicY + kMusicH + kWidgetGap;
static constexpr float kSfxY = kBgmY + kSliderItemH + kWidgetGap;
static constexpr float kBrightnessY = kSfxY + kSliderItemH + kWidgetGap;
static constexpr float kAirplaneY = kBrightnessY + kSliderItemH + kWidgetGap;
static constexpr float kWifiY = kAirplaneY + kToggleH + kWidgetGap;
// Power row sits where the old section title was — same gap as every other widget.
static constexpr float kPowerBtnY = kWifiY + kToggleH + kWidgetGap;

static constexpr float kMusicControlsY = 30.f;
static constexpr float kMusicControlsH = 44.f;
// Player card: title + transport + scrub only (BGM volume is its own row).
static constexpr float kMusicProgressLabelY = 84.f;
static constexpr float kMusicProgressTrackY = 106.f;
// Fullscreen cover overlay player.
static constexpr float kCoverFsCardW = 560.f;
static constexpr float kCoverFsCardH = 168.f;
static constexpr float kCoverFsCardBottomPad = 36.f;
static constexpr float kCoverFsTitleScale = 1.22f;
static constexpr float kCoverFsControlsY = 46.f;
static constexpr float kCoverFsControlsH = 48.f;
static constexpr float kCoverFsProgressLabelY = 108.f;
static constexpr float kCoverFsProgressTrackY = 130.f;
static constexpr int kCoverFsControlCount = 6; // Prev..Playlist
static constexpr float kCoverFsPlaylistHeaderH = 40.f;
static constexpr float kCoverFsPlaylistListPad = 10.f;
static constexpr float kPlaylistRowH = 40.f;
static constexpr float kPlaylistHeaderBaseH = 42.f;
static constexpr float kPlaylistArtPad = 12.f;
// Match playlist entry gutter (list inset); a touch wider than the old full-bleed.
static constexpr float kPlaylistArtSidePad = 16.f;
static constexpr float kMusicCardLabelSidePad = 12.f;
static constexpr float kPlaylistEyeSize = 28.f;
static constexpr float kPlaylistHandleW = 28.f;
static constexpr float kPlaylistThumbSize = 28.f;
static constexpr float kPlaylistThumbGap = 8.f;
static constexpr float kPlaylistTreeIndent = 20.f;
static constexpr int kPlaylistThumbCacheMax = 20;
static constexpr float kPlaylistListBottomPad = 12.f;
static constexpr float kSliderTrackH = 14.f;
static constexpr float kSliderLabelScale = 0.82f;

// Bandcamp-style title marquee (see title_marquee.py).
static constexpr float kMarqueeStartDelay = 0.40f;
static constexpr float kMarqueeSpeedPx = 50.f;
static constexpr float kMarqueeGapPx = 24.f;

static std::string utf8Codepoint(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

static void drawWifiIcon(nxui::Renderer& ren, nxui::Vec2 center, const nxui::Color& color, float scale = 1.0f) {
    ren.drawCircle(center, 1.8f * scale, color);

    const float radii[] = { 5.5f * scale, 10.0f * scale, 14.5f * scale };
    for (float r : radii) {
        constexpr int steps = 8;
        constexpr float degStart = 225.0f;
        constexpr float degEnd = 315.0f;
        constexpr float degStep = (degEnd - degStart) / steps;
        nxui::Vec2 prev;
        for (int i = 0; i <= steps; ++i) {
            float deg = degStart + i * degStep;
            float rad = deg * (3.14159265358979323846f / 180.0f);
            nxui::Vec2 pt = { center.x + r * std::cos(rad), center.y + r * std::sin(rad) };
            if (i > 0) {
                ren.drawLine(prev, pt, color, 1.8f * scale);
            }
            prev = pt;
        }
    }
}

static void drawMoonIcon(nxui::Renderer& ren, nxui::Vec2 center, const nxui::Color& color, float scale = 1.0f) {
    const int R = static_cast<int>(std::round(7.0f * scale));
    const float r = 6.0f * scale;
    const float dx = 2.5f * scale;

    for (int y_off = -R; y_off <= R; ++y_off) {
        float val_out = static_cast<float>(R * R - y_off * y_off);
        if (val_out < 0.0f) continue;
        float x_out_left = -std::sqrt(val_out);
        float x_out_right = std::sqrt(val_out);

        float val_in = r * r - static_cast<float>(y_off * y_off);
        float x_in_left = (val_in >= 0.0f) ? (dx - std::sqrt(val_in)) : x_out_right;

        float x1 = center.x + x_out_left;
        float x2 = center.x + std::min(x_out_right, x_in_left);
        if (x2 > x1) {
            ren.drawLine({x1, center.y + static_cast<float>(y_off)},
                         {x2, center.y + static_cast<float>(y_off)}, color, 1.0f);
        }
    }
}

static std::string ellipsize(nxui::Font* font, const std::string& text, float maxWidth, float scale) {
    if (!font || text.empty()) return text;
    if (font->measure(text).x * scale <= maxWidth) return text;
    const std::string ellipsis = "...";
    std::string out = text;
    while (!out.empty() && font->measure(out + ellipsis).x * scale > maxWidth)
        out.pop_back();
    return out + ellipsis;
}

static void drawFilledTriangle(nxui::Renderer& ren,
                               const nxui::Vec2& a, const nxui::Vec2& b, const nxui::Vec2& c,
                               const nxui::Color& color) {
    ren.drawTriangle(a, b, c, color);
}

static void drawRoundedBar(nxui::Renderer& ren, float x, float y, float w, float h,
                           const nxui::Color& color) {
    const float r = std::min(w, h) * 0.5f;
    ren.drawRoundedRect({x, y, w, h}, color, r);
}

static void drawArcStroke(nxui::Renderer& ren, nxui::Vec2 center, float radius,
                          float degStart, float degEnd, const nxui::Color& color,
                          float thickness, int steps = 16) {
    nxui::Vec2 prev;
    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        float deg = degStart + (degEnd - degStart) * t;
        float rad = deg * (3.14159265358979323846f / 180.f);
        nxui::Vec2 pt = {center.x + radius * std::cos(rad), center.y + radius * std::sin(rad)};
        if (i > 0)
            ren.drawLine(prev, pt, color, thickness);
        prev = pt;
    }
}

static void drawChevronHead(nxui::Renderer& ren, nxui::Vec2 tip, float dirX, float size,
                            const nxui::Color& color) {
    // Filled arrowhead pointing along +dirX (1 = right, -1 = left).
    const float dx = dirX;
    drawFilledTriangle(ren,
        tip,
        {tip.x - dx * size, tip.y - size * 0.72f},
        {tip.x - dx * size, tip.y + size * 0.72f},
        color);
}

static void drawMusicControlIconFallback(nxui::Renderer& ren,
                                         QuickSettingsOverlay::MusicControl control,
                                         const QuickSettingsOverlay::MusicUiState& music,
                                         const nxui::Rect& btn,
                                         const nxui::Color& color) {
    // Vector fallback if PNG assets are missing.
    const float cx = btn.x + btn.width * 0.5f;
    const float cy = btn.y + btn.height * 0.5f;
    const float s = std::min(btn.width, btn.height) * 0.24f;

    switch (control) {
        case QuickSettingsOverlay::MusicControl::Prev: {
            drawRoundedBar(ren, cx - s * 1.45f, cy - s * 1.15f, s * 0.42f, s * 2.3f, color);
            drawFilledTriangle(ren,
                {cx - s * 0.85f, cy},
                {cx + s * 1.25f, cy - s * 1.2f},
                {cx + s * 1.25f, cy + s * 1.2f},
                color);
            break;
        }
        case QuickSettingsOverlay::MusicControl::PlayPause: {
            if (music.playing) {
                const float barW = s * 0.58f;
                const float gap = s * 0.42f;
                drawRoundedBar(ren, cx - gap - barW, cy - s * 1.25f, barW, s * 2.5f, color);
                drawRoundedBar(ren, cx + gap, cy - s * 1.25f, barW, s * 2.5f, color);
            } else {
                drawFilledTriangle(ren,
                    {cx - s * 0.85f, cy - s * 1.3f},
                    {cx - s * 0.85f, cy + s * 1.3f},
                    {cx + s * 1.4f, cy},
                    color);
            }
            break;
        }
        case QuickSettingsOverlay::MusicControl::Next: {
            drawFilledTriangle(ren,
                {cx - s * 1.25f, cy - s * 1.2f},
                {cx - s * 1.25f, cy + s * 1.2f},
                {cx + s * 0.85f, cy},
                color);
            drawRoundedBar(ren, cx + s * 1.0f, cy - s * 1.15f, s * 0.42f, s * 2.3f, color);
            break;
        }
        case QuickSettingsOverlay::MusicControl::Repeat: {
            const float r = s * 1.2f;
            drawArcStroke(ren, {cx, cy}, r, 205.f, 355.f, color, 2.6f, 16);
            drawArcStroke(ren, {cx, cy}, r, 25.f, 175.f, color, 2.6f, 16);
            drawChevronHead(ren, {cx + r * 0.98f, cy - r * 0.52f}, 1.f, s * 0.58f, color);
            drawChevronHead(ren, {cx - r * 0.98f, cy + r * 0.52f}, -1.f, s * 0.58f, color);
            if (music.repeat == MusicRepeatMode::One) {
                drawRoundedBar(ren, cx - s * 0.1f, cy - s * 0.55f, s * 0.3f, s * 1.15f, color);
                drawRoundedBar(ren, cx - s * 0.4f, cy - s * 0.55f, s * 0.3f, s * 0.3f, color);
            }
            break;
        }
        case QuickSettingsOverlay::MusicControl::Shuffle: {
            const float x0 = cx - s * 1.4f;
            const float x1 = cx - s * 0.1f;
            const float x2 = cx + s * 1.4f;
            const float yTop = cy - s * 0.9f;
            const float yBot = cy + s * 0.9f;
            ren.drawLine({x0, yTop}, {x1, yTop}, color, 2.5f);
            ren.drawLine({x1, yTop}, {x2 - s * 0.4f, yBot}, color, 2.5f);
            ren.drawLine({x0, yBot}, {x1, yBot}, color, 2.5f);
            ren.drawLine({x1, yBot}, {x2 - s * 0.4f, yTop}, color, 2.5f);
            drawChevronHead(ren, {x2, yBot}, 1.f, s * 0.52f, color);
            drawChevronHead(ren, {x2, yTop}, 1.f, s * 0.52f, color);
            break;
        }
        case QuickSettingsOverlay::MusicControl::Playlist: {
            const float boxW = s * 2.4f;
            const float boxH = s * 2.2f;
            const nxui::Rect box{cx - boxW * 0.5f, cy - boxH * 0.5f, boxW, boxH};
            ren.drawRoundedRectOutline(box, color, s * 0.35f, 2.0f);
            const float lineL = box.x + s * 0.45f;
            const float lineW = boxW - s * 0.9f;
            for (int i = -1; i <= 1; ++i) {
                float y = cy + static_cast<float>(i) * s * 0.55f;
                drawRoundedBar(ren, lineL, y - 1.1f, lineW, 2.2f, color);
            }
            break;
        }
        default:
            break;
    }
}

static void drawPlaylistHandle(nxui::Renderer& ren, const nxui::Rect& handle,
                               const nxui::Color& color) {
    const float cx = handle.x + handle.width * 0.5f;
    const float cy = handle.y + handle.height * 0.5f;
    const float lineW = 12.f;
    const float gap = 4.f;
    for (int i = -1; i <= 1; ++i) {
        float y = cy + static_cast<float>(i) * gap;
        ren.drawLine({cx - lineW * 0.5f, y}, {cx + lineW * 0.5f, y}, color, 2.0f);
    }
}

} // namespace

void QuickSettingsOverlay::ensureMusicControlIcons(nxui::Renderer& ren) {
    if (m_musicIconsLoadAttempted)
        return;
    m_musicIconsLoadAttempted = true;

    constexpr const char* kBase = "sdmc:/switch/SwitchU/icons/music/";
    auto load = [&](nxui::Texture& tex, const char* file) {
        tex.loadFromFile(ren.gpu(), ren, std::string(kBase) + file, 128);
    };
    load(m_iconPrev, "prev.png");
    load(m_iconNext, "next.png");
    load(m_iconPlay, "play.png");
    load(m_iconPause, "pause.png");
    load(m_iconRepeat, "repeat.png");
    load(m_iconShuffle, "shuffle.png");
    load(m_iconPlaylist, "playlist.png");
}

const nxui::Texture* QuickSettingsOverlay::musicControlIcon(MusicControl control,
                                                            bool playing) const {
    switch (control) {
        case MusicControl::Prev: return m_iconPrev.valid() ? &m_iconPrev : nullptr;
        case MusicControl::Next: return m_iconNext.valid() ? &m_iconNext : nullptr;
        case MusicControl::PlayPause:
            if (playing)
                return m_iconPause.valid() ? &m_iconPause : nullptr;
            return m_iconPlay.valid() ? &m_iconPlay : nullptr;
        case MusicControl::Repeat: return m_iconRepeat.valid() ? &m_iconRepeat : nullptr;
        case MusicControl::Shuffle: return m_iconShuffle.valid() ? &m_iconShuffle : nullptr;
        case MusicControl::Playlist: return m_iconPlaylist.valid() ? &m_iconPlaylist : nullptr;
        default: return nullptr;
    }
}

void QuickSettingsOverlay::drawMusicControlIcon(nxui::Renderer& ren,
                                                MusicControl control,
                                                const MusicUiState& music,
                                                const nxui::Rect& btn,
                                                const nxui::Color& color) {
    ensureMusicControlIcons(ren);
    const nxui::Texture* tex = musicControlIcon(control, music.playing);
    if (!tex) {
        drawMusicControlIconFallback(ren, control, music, btn, color);
        return;
    }

    // White+alpha masks tint cleanly: dark themes → light icons, light → dark.
    const float edge = std::min(btn.width, btn.height) * 0.52f;
    const float cx = btn.x + btn.width * 0.5f;
    const float cy = btn.y + btn.height * 0.5f;
    // Play triangles read optically left-heavy; nudge a hair to the right.
    const float biasX = (control == MusicControl::PlayPause && !music.playing) ? edge * 0.04f : 0.f;
    ren.drawTexture(tex, {cx - edge * 0.5f + biasX, cy - edge * 0.5f, edge, edge}, color);

    // Repeat-one: clean white "1" over the glyph (no badge/shadow).
    if (control == MusicControl::Repeat && music.repeat == MusicRepeatMode::One && m_smallFont) {
        const char* one = "1";
        const float scale = 0.78f;
        nxui::Vec2 sz = m_smallFont->measure(one);
        const float tw = sz.x * scale;
        const float th = sz.y * scale;
        ren.drawText(one,
                     {cx - tw * 0.5f, cy - th * 0.55f},
                     m_smallFont,
                     nxui::Color(1.f, 1.f, 1.f, color.a),
                     scale);
    }
}

QuickSettingsOverlay::QuickSettingsOverlay() {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setVisible(false);
    setFocusable(true);
    setFrameworkTouchEnabled(false);

    m_cursor.setBorderWidth(2.6f);
    m_cursor.setColor(nxui::Color(0.12f, 0.76f, 0.98f, 1.f));

    setupNavigationActions();
}

QuickSettingsOverlay::~QuickSettingsOverlay() = default;

void QuickSettingsOverlay::setTheme(const nxui::Theme* t) {
    m_theme = t;
    if (m_theme) {
        m_cursor.setColor(m_theme->cursorNormal);
    }
}

void QuickSettingsOverlay::syncMusicFromCallbacks() {
    if (!m_callbacks.onMusicQueryState)
        return;

    MusicUiState next = m_callbacks.onMusicQueryState();
    const bool listChanged =
        next.playlistOrderKeys != m_music.playlistOrderKeys
        || next.playlistTitles.size() != m_music.playlistTitles.size()
        || next.hasAlbumFolders != m_music.hasAlbumFolders;

    // Always refresh live playback fields; only swap heavy list data when needed.
    m_music.nowPlaying = std::move(next.nowPlaying);
    m_music.playing = next.playing;
    m_music.shuffle = next.shuffle;
    m_music.repeat = next.repeat;
    m_music.volume = next.volume;
    m_music.positionSeconds = next.positionSeconds;
    m_music.durationSeconds = next.durationSeconds;
    m_music.currentIndex = next.currentIndex;
    if (listChanged) {
        m_music.hasAlbumFolders = next.hasAlbumFolders;
        m_music.playlistTitles = std::move(next.playlistTitles);
        m_music.playlistFilenames = std::move(next.playlistFilenames);
        m_music.playlistOrderKeys = std::move(next.playlistOrderKeys);
        m_music.playlistAlbumFolders = std::move(next.playlistAlbumFolders);
        if (m_music.hasAlbumFolders && m_playlistReorderMode)
            cancelPlaylistReorder();
        rebuildPlaylistRows();
    }

    m_bgmVolume = std::clamp(m_music.volume, 0.0f, 1.0f);
    if (m_music.currentIndex != m_coverTrackIndex) {
        m_coverTrackIndex = m_music.currentIndex;
        m_coverLoadAttempted = false;
        m_coverFsBlurCached = false;
        m_coverTex = nxui::Texture{};
        m_coverBytes.clear();
        if (m_callbacks.onMusicQueryCoverArt)
            m_coverBytes = m_callbacks.onMusicQueryCoverArt();
    }

    syncFolderExpandToPlaying();
}

bool QuickSettingsOverlay::playlistReorderAllowed() const {
    return !m_music.hasAlbumFolders;
}

int QuickSettingsOverlay::playlistVisibleCount() const {
    return static_cast<int>(m_playlistRows.size());
}

int QuickSettingsOverlay::playlistRowForTrack(int trackIndex) const {
    for (int i = 0; i < static_cast<int>(m_playlistRows.size()); ++i) {
        const auto& row = m_playlistRows[static_cast<size_t>(i)];
        if (row.kind == PlaylistRowKind::Track && row.trackIndex == trackIndex)
            return i;
    }
    return -1;
}

void QuickSettingsOverlay::rebuildPlaylistRows() {
    std::string focusFolder;
    int focusTrack = -1;
    if (m_playlistFocus >= 0 && m_playlistFocus < static_cast<int>(m_playlistRows.size())) {
        const auto& fr = m_playlistRows[static_cast<size_t>(m_playlistFocus)];
        if (fr.kind == PlaylistRowKind::Folder)
            focusFolder = fr.folder;
        else
            focusTrack = fr.trackIndex;
    }

    m_playlistRows.clear();
    const int count = static_cast<int>(m_music.playlistTitles.size());
    if (count <= 0) {
        m_playlistFocus = 0;
        return;
    }

    std::unordered_set<std::string> emittedFolders;
    for (int i = 0; i < count; ++i) {
        const std::string& folder =
            (i < static_cast<int>(m_music.playlistAlbumFolders.size()))
                ? m_music.playlistAlbumFolders[static_cast<size_t>(i)]
                : std::string();
        if (folder.empty()) {
            PlaylistRow row;
            row.kind = PlaylistRowKind::Track;
            row.trackIndex = i;
            row.label = m_music.playlistTitles[static_cast<size_t>(i)];
            row.indented = false;
            m_playlistRows.push_back(std::move(row));
            continue;
        }
        if (!emittedFolders.insert(folder).second)
            continue;

        int first = i;
        int n = 0;
        for (int j = 0; j < count; ++j) {
            if (j < static_cast<int>(m_music.playlistAlbumFolders.size())
                && m_music.playlistAlbumFolders[static_cast<size_t>(j)] == folder) {
                if (n == 0) first = j;
                ++n;
            }
        }
        PlaylistRow header;
        header.kind = PlaylistRowKind::Folder;
        header.folder = folder;
        header.trackCount = n;
        header.trackIndex = first;
        header.label = folder;
        header.indented = false;
        m_playlistRows.push_back(std::move(header));

        if (m_playlistExpanded.count(folder) == 0)
            continue;
        for (int j = 0; j < count; ++j) {
            if (j >= static_cast<int>(m_music.playlistAlbumFolders.size())
                || m_music.playlistAlbumFolders[static_cast<size_t>(j)] != folder)
                continue;
            PlaylistRow row;
            row.kind = PlaylistRowKind::Track;
            row.trackIndex = j;
            row.folder = folder;
            row.label = m_music.playlistTitles[static_cast<size_t>(j)];
            row.indented = true;
            m_playlistRows.push_back(std::move(row));
        }
    }

    // Restore focus to the same folder/track when possible.
    int restored = -1;
    if (focusTrack >= 0)
        restored = playlistRowForTrack(focusTrack);
    if (restored < 0 && !focusFolder.empty()) {
        for (int i = 0; i < static_cast<int>(m_playlistRows.size()); ++i) {
            const auto& row = m_playlistRows[static_cast<size_t>(i)];
            if (row.kind == PlaylistRowKind::Folder && row.folder == focusFolder) {
                restored = i;
                break;
            }
        }
    }
    if (restored < 0)
        restored = playlistRowForTrack(m_music.currentIndex);
    if (restored < 0)
        restored = 0;
    m_playlistFocus = std::clamp(restored, 0, std::max(0, playlistVisibleCount() - 1));
}

void QuickSettingsOverlay::syncFolderExpandToPlaying() {
    if (!m_music.hasAlbumFolders) {
        if (!m_playlistExpanded.empty() || !m_playlistUserExpanded.empty()) {
            m_playlistExpanded.clear();
            m_playlistUserExpanded.clear();
            m_playlistExpandPlayingIndex = -2;
            rebuildPlaylistRows();
        }
        return;
    }

    const int cur = m_music.currentIndex;
    if (cur == m_playlistExpandPlayingIndex)
        return;
    m_playlistExpandPlayingIndex = cur;

    std::string playingFolder;
    if (cur >= 0 && cur < static_cast<int>(m_music.playlistAlbumFolders.size()))
        playingFolder = m_music.playlistAlbumFolders[static_cast<size_t>(cur)];

    // On play change (prev/next/shuffle/select): only the playing album stays open.
    // Manually opened folders can be browsed freely until playback leaves them.
    std::unordered_set<std::string> nextExpanded;
    if (!playingFolder.empty())
        nextExpanded.insert(playingFolder);

    const bool changed = (nextExpanded != m_playlistExpanded);
    m_playlistExpanded = std::move(nextExpanded);
    // Drop pins for folders that are no longer relevant to playback.
    for (auto it = m_playlistUserExpanded.begin(); it != m_playlistUserExpanded.end(); ) {
        if (m_playlistExpanded.count(*it) == 0)
            it = m_playlistUserExpanded.erase(it);
        else
            ++it;
    }

    if (changed || m_playlistRows.empty())
        rebuildPlaylistRows();
}

void QuickSettingsOverlay::togglePlaylistFolder(const std::string& folder) {
    if (folder.empty())
        return;
    if (m_playlistExpanded.count(folder)) {
        m_playlistExpanded.erase(folder);
        m_playlistUserExpanded.erase(folder);
    } else {
        m_playlistExpanded.insert(folder);
        m_playlistUserExpanded.insert(folder);
    }
    rebuildPlaylistRows();
    ensurePlaylistFocusVisible();
    resetPlaylistMarquee();
    if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
    updateCursorTarget(true);
}

void QuickSettingsOverlay::activatePlaylistRow() {
    if (m_playlistFocus < 0 || m_playlistFocus >= playlistVisibleCount())
        return;
    const auto& row = m_playlistRows[static_cast<size_t>(m_playlistFocus)];
    if (row.kind == PlaylistRowKind::Folder) {
        togglePlaylistFolder(row.folder);
        return;
    }
    if (m_callbacks.onMusicSelectTrack)
        m_callbacks.onMusicSelectTrack(row.trackIndex);
    syncMusicFromCallbacks();
    // Prefer focusing the playing track row after expand sync.
    const int rowIdx = playlistRowForTrack(m_music.currentIndex);
    if (rowIdx >= 0)
        m_playlistFocus = rowIdx;
    centerPlaylistFocus();
    if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
    updateCursorTarget(true);
}

const nxui::Texture* QuickSettingsOverlay::playlistThumbTexture(
    nxui::Renderer& ren, const std::string& key, const std::vector<uint8_t>* bytes) {
    if (key.empty() || !bytes || bytes->empty())
        return nullptr;

    ++m_playlistThumbClock;
    for (auto& entry : m_playlistThumbs) {
        if (entry.key == key) {
            entry.lastUse = m_playlistThumbClock;
            return entry.tex.valid() ? &entry.tex : nullptr;
        }
    }

    PlaylistThumb entry;
    entry.key = key;
    entry.lastUse = m_playlistThumbClock;
    if (!entry.tex.loadFromMemory(ren.gpu(), ren, bytes->data(), bytes->size(),
                                  static_cast<int>(kPlaylistThumbSize * 2.f)))
        return nullptr;

    if (static_cast<int>(m_playlistThumbs.size()) >= kPlaylistThumbCacheMax) {
        auto victim = std::min_element(m_playlistThumbs.begin(), m_playlistThumbs.end(),
            [](const PlaylistThumb& a, const PlaylistThumb& b) {
                return a.lastUse < b.lastUse;
            });
        if (victim != m_playlistThumbs.end()) {
            *victim = std::move(entry);
            return victim->tex.valid() ? &victim->tex : nullptr;
        }
    }
    m_playlistThumbs.push_back(std::move(entry));
    return m_playlistThumbs.back().tex.valid() ? &m_playlistThumbs.back().tex : nullptr;
}

void QuickSettingsOverlay::refreshCoverTexture(nxui::Renderer& ren) {
    if (m_coverLoadAttempted)
        return;
    m_coverLoadAttempted = true;
    m_coverTex = nxui::Texture{};
    m_coverFsBlurCached = false;
    if (m_coverBytes.empty()) {
        // Track has no art — drop fullscreen rather than leaving a black void.
        if (m_coverFullscreen) {
            m_coverFullscreen = false;
            m_draggingProgress = false;
            m_playlistArtFocused = false;
        }
        return;
    }
    // Decode at roughly 2x panel art width so full-bleed covers stay sharp.
    const int decodeEdge = static_cast<int>((kPlaylistWidth - kPlaylistArtSidePad * 2.f) * 2.f);
    m_coverTex.loadFromMemory(ren.gpu(), ren,
                              m_coverBytes.data(),
                              m_coverBytes.size(),
                              std::max(192, decodeEdge));
    // Cover changes header height — keep the current track centered and
    // re-snap the cursor (scroll Y shifted when art appears/disappears).
    if (m_playlistOpen && m_coverTex.valid()) {
        centerPlaylistFocus();
        if (!m_coverFullscreen)
            updateCursorTarget(true);
    }
}

void QuickSettingsOverlay::seekFromProgressX(float x) {
    seekFromProgressX(x, computeProgressTrackRect());
}

void QuickSettingsOverlay::seekFromProgressX(float x, const nxui::Rect& track) {
    if (track.width <= 1.f || m_music.durationSeconds <= 0.05f)
        return;
    float t = std::clamp((x - track.x) / track.width, 0.f, 1.f);
    float seconds = t * m_music.durationSeconds;
    m_music.positionSeconds = seconds;
    if (m_callbacks.onMusicSeek)
        m_callbacks.onMusicSeek(seconds);
}

void QuickSettingsOverlay::togglePlaylistArtVisible() {
    m_playlistArtVisible = !m_playlistArtVisible;
    if (!m_playlistArtVisible)
        m_playlistArtFocused = false;
    if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
    if (!m_playlistOpen)
        return;
    // Header height changed — keep the list sensible and snap the cursor so it
    // doesn't linger at the old row Y (especially after touch-toggling the eye).
    m_playlistScrollY = std::clamp(m_playlistScrollY, 0.f, playlistMaxScroll());
    if (!m_playlistEyeFocused && !m_playlistArtFocused)
        centerPlaylistFocus();
    updateCursorTarget(true);
}

bool QuickSettingsOverlay::hasPlaylistCover() const {
    return m_coverTex.valid() || !m_coverBytes.empty();
}

bool QuickSettingsOverlay::playlistArtFocusable() const {
    return m_playlistArtVisible && m_coverTex.valid();
}

void QuickSettingsOverlay::openCoverFullscreen() {
    if (!m_coverTex.valid())
        return;
    m_coverFullscreen = true;
    m_coverFsPlaylistOpen = false;
    m_coverFsPlaylistFocused = false;
    m_coverFsPlaylistAnim = 0.f;
    m_draggingProgress = false;
    m_selectedItem = ItemIndex::MusicTransport;
    m_selectedMusicControl = MusicControl::PlayPause;
    m_cursor.setVisible(false);
    if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
}

void QuickSettingsOverlay::closeCoverFullscreen() {
    if (!m_coverFullscreen)
        return;
    m_coverFullscreen = false;
    m_coverFullscreenPending = false;
    m_coverFsPlaylistOpen = false;
    m_coverFsPlaylistFocused = false;
    m_coverFsPlaylistAnim = 0.f;
    m_draggingProgress = false;
    updateCursorTarget(true);
    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
}

void QuickSettingsOverlay::cycleCoverFsControl(int delta) {
    int c = static_cast<int>(m_selectedMusicControl) + delta;
    // Fullscreen transport omits Playlist — wrap within Prev..Shuffle.
    if (c < 0)
        c = kCoverFsControlCount - 1;
    else if (c >= kCoverFsControlCount)
        c = 0;
    m_selectedMusicControl = static_cast<MusicControl>(c);
}

void QuickSettingsOverlay::activateCoverFsControl() {
    if (m_selectedMusicControl == MusicControl::Playlist) {
        toggleCoverFsPlaylist();
        return;
    }
    activateMusicControl();
}

void QuickSettingsOverlay::toggleCoverFsPlaylist() {
    if (!m_coverFullscreen)
        return;
    m_coverFsPlaylistOpen = !m_coverFsPlaylistOpen;
    if (m_coverFsPlaylistOpen) {
        m_coverFsPlaylistFocused = true;
        syncFolderExpandToPlaying();
        if (m_playlistRows.empty())
            rebuildPlaylistRows();
        const int row = playlistRowForTrack(m_music.currentIndex);
        m_playlistFocus = row >= 0 ? row : 0;
        centerPlaylistFocus();
        resetPlaylistMarquee();
    } else {
        m_coverFsPlaylistFocused = false;
        m_selectedItem = ItemIndex::MusicTransport;
        m_selectedMusicControl = MusicControl::Playlist;
    }
    if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
}

void QuickSettingsOverlay::syncPlaylistToPlayingTrack(bool center) {
    if ((!m_playlistOpen && m_coverFsPlaylistAnim <= 0.001f) || m_music.playlistTitles.empty())
        return;
    syncFolderExpandToPlaying();
    const int row = playlistRowForTrack(m_music.currentIndex);
    if (row >= 0)
        m_playlistFocus = row;
    else
        m_playlistFocus = std::clamp(m_playlistFocus, 0, std::max(0, playlistVisibleCount() - 1));
    // Keep art focus while fullscreen so B returns to the cover selection.
    if (!m_coverFullscreen) {
        m_playlistEyeFocused = false;
        m_playlistArtFocused = false;
        m_playlistFocusParked = false;
    }
    if (center)
        centerPlaylistFocus();
    else
        ensurePlaylistFocusVisible();
    resetPlaylistMarquee();
    if (!m_coverFullscreen)
        updateCursorTarget(true);
}

static std::string formatTrackTime(float seconds) {
    if (seconds < 0.f) seconds = 0.f;
    int total = static_cast<int>(seconds + 0.5f);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

void QuickSettingsOverlay::refreshMusicState() {
    syncMusicFromCallbacks();
}

void QuickSettingsOverlay::openPlaylist() {
    syncMusicFromCallbacks();
    m_playlistOpen = true;
    m_playlistFocusParked = false;
    m_playlistEyeFocused = false;
    m_playlistArtFocused = false;
    m_coverFullscreen = false;
    if (m_playlistRows.empty())
        rebuildPlaylistRows();
    syncFolderExpandToPlaying();
    const int row = playlistRowForTrack(m_music.currentIndex);
    m_playlistFocus = row >= 0 ? row : 0;
    m_playlistScrollY = 0.f;
    m_playlistDragging = false;
    m_playlistScrolling = false;
    m_playlistDragIndex = -1;
    m_playlistTouchIndex = -1;
    m_playlistDragOffsetY = 0.f;
    m_playlistTouchOnHandle = false;
    m_playlistReorderMode = false;
    m_playlistReorderOrigin = -1;
    m_playlistReorderCurrent = -1;
    m_playlistCenterPending = true;
    centerPlaylistFocus();
    resetPlaylistMarquee();
}

void QuickSettingsOverlay::closePlaylist() {
    if (m_playlistReorderMode)
        cancelPlaylistReorder();
    closeCoverFullscreen();
    m_playlistOpen = false;
    m_playlistFocusParked = false;
    m_playlistEyeFocused = false;
    m_playlistArtFocused = false;
    m_playlistDragging = false;
    m_playlistScrolling = false;
    m_playlistDragIndex = -1;
    m_playlistTouchIndex = -1;
    m_playlistDragOffsetY = 0.f;
    m_playlistTouchOnHandle = false;
    updateCursorTarget();
}

void QuickSettingsOverlay::ensurePlaylistFocusVisible() {
    const int count = playlistVisibleCount();
    if (count <= 0) {
        m_playlistScrollY = 0.f;
        return;
    }
    m_playlistFocus = std::clamp(m_playlistFocus, 0, count - 1);
    const float listH = activePlaylistListRect().height;
    const float focusTop = static_cast<float>(m_playlistFocus) * kPlaylistRowH;
    const float focusBottom = focusTop + kPlaylistRowH;
    if (focusTop < m_playlistScrollY)
        m_playlistScrollY = focusTop;
    else if (focusBottom - m_playlistScrollY > listH)
        m_playlistScrollY = std::min(playlistMaxScroll(), focusBottom - listH);
    m_playlistScrollY = std::clamp(m_playlistScrollY, 0.f, playlistMaxScroll());
}

void QuickSettingsOverlay::centerPlaylistFocus() {
    const int count = playlistVisibleCount();
    if (count <= 0) {
        m_playlistScrollY = 0.f;
        return;
    }
    m_playlistFocus = std::clamp(m_playlistFocus, 0, count - 1);
    const float listH = activePlaylistListRect().height;
    if (listH <= 1.f) {
        m_playlistScrollY = 0.f;
        return;
    }
    // Vertically center the focused row in the visible list viewport.
    const float focusMid = (static_cast<float>(m_playlistFocus) + 0.5f) * kPlaylistRowH;
    m_playlistScrollY = std::clamp(focusMid - listH * 0.5f, 0.f, playlistMaxScroll());
}

void QuickSettingsOverlay::beginPlaylistReorder() {
    if (!m_playlistOpen || m_playlistReorderMode)
        return;
    if (!playlistReorderAllowed())
        return;
    if (m_music.playlistTitles.size() < 2)
        return;
    // Reorder still uses flat track indices — only when no album folders.
    if (m_playlistFocus < 0 || m_playlistFocus >= playlistVisibleCount())
        return;
    const auto& row = m_playlistRows[static_cast<size_t>(m_playlistFocus)];
    if (row.kind != PlaylistRowKind::Track)
        return;
    m_playlistReorderMode = true;
    m_playlistReorderOrigin = row.trackIndex;
    m_playlistReorderCurrent = row.trackIndex;
    if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
    updateCursorTarget();
}

void QuickSettingsOverlay::confirmPlaylistReorder() {
    if (!m_playlistReorderMode)
        return;
    m_playlistReorderMode = false;
    m_playlistReorderOrigin = -1;
    m_playlistReorderCurrent = -1;
    if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
    updateCursorTarget();
}

void QuickSettingsOverlay::cancelPlaylistReorder() {
    if (!m_playlistReorderMode)
        return;
    if (m_playlistReorderCurrent != m_playlistReorderOrigin
        && m_playlistReorderCurrent >= 0 && m_playlistReorderOrigin >= 0
        && m_callbacks.onMusicMoveTrack) {
        m_callbacks.onMusicMoveTrack(m_playlistReorderCurrent, m_playlistReorderOrigin);
        syncMusicFromCallbacks();
    }
    m_playlistFocus = m_playlistReorderOrigin;
    m_playlistReorderMode = false;
    m_playlistReorderOrigin = -1;
    m_playlistReorderCurrent = -1;
    ensurePlaylistFocusVisible();
    if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
    updateCursorTarget();
}

void QuickSettingsOverlay::movePlaylistReorder(int delta) {
    if (!m_playlistReorderMode || delta == 0)
        return;
    const int count = static_cast<int>(m_music.playlistTitles.size());
    if (count < 2) return;
    const int from = m_playlistReorderCurrent;
    const int to = std::clamp(from + delta, 0, count - 1);
    if (to == from) return;
    if (m_callbacks.onMusicMoveTrack)
        m_callbacks.onMusicMoveTrack(from, to);
    syncMusicFromCallbacks();
    m_playlistReorderCurrent = to;
    const int row = playlistRowForTrack(to);
    m_playlistFocus = row >= 0 ? row : to;
    ensurePlaylistFocusVisible();
    resetPlaylistMarquee();
    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
    updateCursorTarget();
}

void QuickSettingsOverlay::resetPlaylistMarquee() {
    m_playlistMarqueeIndex = m_playlistFocus;
    m_playlistMarqueeOffset = 0.f;
    m_playlistMarqueeHold = kMarqueeStartDelay;
}

void QuickSettingsOverlay::updatePlaylistMarquee(float dt) {
    if ((!m_playlistOpen && m_coverFsPlaylistAnim <= 0.001f)
        || playlistVisibleCount() <= 0 || !m_smallFont) {
        m_playlistMarqueeOffset = 0.f;
        return;
    }

    if (m_playlistMarqueeIndex != m_playlistFocus)
        resetPlaylistMarquee();

    if (m_playlistFocus < 0 || m_playlistFocus >= playlistVisibleCount())
        return;

    const auto& row = m_playlistRows[static_cast<size_t>(m_playlistFocus)];
    const float textScale = 0.78f;
    const bool showHandle = playlistReorderAllowed();
    const float gutter = (showHandle ? kPlaylistHandleW : 0.f)
        + kPlaylistThumbSize + kPlaylistThumbGap
        + (row.indented ? kPlaylistTreeIndent : 0.f) + 10.f;
    const float maxW = activePlaylistListRect().width - gutter;
    const float textW = m_smallFont->measure(row.label).x * textScale;
    if (textW <= maxW + 1.f) {
        m_playlistMarqueeOffset = 0.f;
        m_playlistMarqueeHold = kMarqueeStartDelay;
        return;
    }

    if (m_playlistMarqueeHold > 0.f) {
        m_playlistMarqueeHold -= dt;
        return;
    }

    m_playlistMarqueeOffset += kMarqueeSpeedPx * dt;
    const float cycle = textW + kMarqueeGapPx;
    if (cycle > 1.f && m_playlistMarqueeOffset >= cycle)
        m_playlistMarqueeOffset = std::fmod(m_playlistMarqueeOffset, cycle);
}

void QuickSettingsOverlay::cycleMusicControl(int delta) {
    int c = static_cast<int>(m_selectedMusicControl) + delta;
    const int count = static_cast<int>(MusicControl::Count);
    while (c < 0) c += count;
    while (c >= count) c -= count;
    m_selectedMusicControl = static_cast<MusicControl>(c);
    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
}

void QuickSettingsOverlay::activateMusicControl() {
    const bool trackStep = m_selectedMusicControl == MusicControl::Prev
        || m_selectedMusicControl == MusicControl::Next;
    switch (m_selectedMusicControl) {
        case MusicControl::Prev:
            if (m_callbacks.onMusicPrev) m_callbacks.onMusicPrev();
            break;
        case MusicControl::PlayPause:
            if (m_callbacks.onMusicPlayPause) m_callbacks.onMusicPlayPause();
            break;
        case MusicControl::Next:
            if (m_callbacks.onMusicNext) m_callbacks.onMusicNext();
            break;
        case MusicControl::Repeat:
            if (m_callbacks.onMusicCycleRepeat) m_callbacks.onMusicCycleRepeat();
            break;
        case MusicControl::Shuffle:
            if (m_callbacks.onMusicToggleShuffle) m_callbacks.onMusicToggleShuffle();
            break;
        case MusicControl::Playlist:
            if (m_coverFullscreen) {
                toggleCoverFsPlaylist();
            } else if (m_playlistOpen) {
                closePlaylist();
            } else {
                openPlaylist();
            }
            break;
        default:
            break;
    }
    syncMusicFromCallbacks();
    if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
    if (trackStep && m_playlistOpen)
        syncPlaylistToPlayingTrack(true);
    else
        updateCursorTarget();
}

void QuickSettingsOverlay::setupNavigationActions() {
    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            if (m_coverFsPlaylistOpen && m_coverFsPlaylistFocused) {
                if (playlistVisibleCount() <= 0) return;
                if (m_playlistFocus > 0) {
                    m_playlistFocus -= 1;
                    ensurePlaylistFocusVisible();
                    resetPlaylistMarquee();
                    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                } else {
                    // Top of list → back to player controls.
                    m_coverFsPlaylistFocused = false;
                    m_selectedItem = ItemIndex::MusicTransport;
                    m_selectedMusicControl = MusicControl::Playlist;
                    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                }
                return;
            }
            if (m_selectedItem == ItemIndex::MusicProgress) {
                m_selectedItem = ItemIndex::MusicTransport;
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            } else if (m_coverFsPlaylistOpen) {
                m_coverFsPlaylistFocused = true;
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            }
            return;
        }
        if (m_playlistOpen && !m_playlistFocusParked) {
            if (m_playlistReorderMode) {
                movePlaylistReorder(-1);
                return;
            }
            if (m_playlistEyeFocused)
                return; // already on the eye
            if (m_playlistArtFocused) {
                m_playlistArtFocused = false;
                m_playlistEyeFocused = true;
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            if (m_music.playlistTitles.empty()) {
                if (playlistArtFocusable())
                    m_playlistArtFocused = true;
                else
                    m_playlistEyeFocused = true;
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            if (m_playlistFocus <= 0) {
                // Top of list → art (if shown) else eye.
                if (playlistArtFocusable())
                    m_playlistArtFocused = true;
                else
                    m_playlistEyeFocused = true;
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            m_playlistFocus -= 1;
            ensurePlaylistFocusVisible();
            resetPlaylistMarquee();
            updateCursorTarget();
            if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            return;
        }
        int next = static_cast<int>(m_selectedItem) - 1;
        if (next < 0)
            next = static_cast<int>(ItemIndex::Count) - 1;
        m_selectedItem = static_cast<ItemIndex>(next);
        updateCursorTarget();
        if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
    });

    addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            if (m_coverFsPlaylistOpen && m_coverFsPlaylistFocused) {
                if (playlistVisibleCount() <= 0) return;
                const int last = playlistVisibleCount() - 1;
                if (m_playlistFocus < last) {
                    m_playlistFocus += 1;
                    ensurePlaylistFocusVisible();
                    resetPlaylistMarquee();
                    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                }
                return;
            }
            if (m_selectedItem == ItemIndex::MusicTransport) {
                m_selectedItem = ItemIndex::MusicProgress;
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            }
            return;
        }
        if (m_playlistOpen && !m_playlistFocusParked) {
            if (m_playlistReorderMode) {
                movePlaylistReorder(1);
                return;
            }
            if (m_playlistEyeFocused) {
                m_playlistEyeFocused = false;
                if (playlistArtFocusable()) {
                    m_playlistArtFocused = true;
                } else {
                    m_playlistFocus = 0;
                    ensurePlaylistFocusVisible();
                    resetPlaylistMarquee();
                }
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            if (m_playlistArtFocused) {
                m_playlistArtFocused = false;
                m_playlistFocus = 0;
                ensurePlaylistFocusVisible();
                resetPlaylistMarquee();
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            if (playlistVisibleCount() <= 0) return;
            m_playlistFocus += 1;
            if (m_playlistFocus >= playlistVisibleCount())
                m_playlistFocus = playlistVisibleCount() - 1;
            ensurePlaylistFocusVisible();
            resetPlaylistMarquee();
            updateCursorTarget();
            if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            return;
        }
        int next = static_cast<int>(m_selectedItem) + 1;
        if (next >= static_cast<int>(ItemIndex::Count))
            next = 0;
        m_selectedItem = static_cast<ItemIndex>(next);
        updateCursorTarget();
        if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
    });

    addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            if (m_coverFsPlaylistFocused) {
                m_coverFsPlaylistFocused = false;
                m_selectedItem = ItemIndex::MusicTransport;
                m_selectedMusicControl = MusicControl::Playlist;
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            if (m_selectedItem == ItemIndex::MusicProgress) {
                if (m_music.durationSeconds > 0.f) {
                    float next = std::max(0.f, m_music.positionSeconds - 5.f);
                    m_music.positionSeconds = next;
                    if (m_callbacks.onMusicSeek) m_callbacks.onMusicSeek(next);
                    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                }
            } else {
                cycleCoverFsControl(-1);
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            }
            return;
        }
        // Playlist open + focus in QS: hop back from left-edge controls.
        if (m_playlistOpen && m_playlistFocusParked) {
            if (m_playlistReorderMode) return;
            const bool hopFromPrev = m_selectedItem == ItemIndex::MusicTransport
                && m_selectedMusicControl == MusicControl::Prev;
            const bool hopFromToggle = m_selectedItem == ItemIndex::AirplaneMode
                || m_selectedItem == ItemIndex::Wifi;
            const bool hopFromSleep = m_selectedItem == ItemIndex::PowerActions
                && m_selectedPower == PowerAction::Sleep;
            if (hopFromPrev || hopFromToggle || hopFromSleep) {
                m_playlistFocusParked = false;
                m_playlistEyeFocused = false;
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            // Fall through to normal QS Left handling below.
        } else if (m_playlistOpen && !m_playlistFocusParked) {
            return;
        }
        switch (m_selectedItem) {
            case ItemIndex::Brightness:
                adjustSlider(ItemIndex::Brightness, -0.05f);
                break;
            case ItemIndex::MusicTransport:
                cycleMusicControl(-1);
                break;
            case ItemIndex::MusicProgress: {
                if (m_music.durationSeconds <= 0.f) break;
                float next = std::max(0.f, m_music.positionSeconds - 5.f);
                m_music.positionSeconds = next;
                if (m_callbacks.onMusicSeek) m_callbacks.onMusicSeek(next);
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                break;
            }
            case ItemIndex::BgmVolume:
                adjustSlider(ItemIndex::BgmVolume, -0.05f);
                break;
            case ItemIndex::SfxVolume:
                adjustSlider(ItemIndex::SfxVolume, -0.05f);
                break;
            case ItemIndex::AirplaneMode:
            case ItemIndex::Wifi:
                // A / tap toggles; Left does not.
                break;
            case ItemIndex::PowerActions: {
                int p = static_cast<int>(m_selectedPower) - 1;
                if (p < 0) p = static_cast<int>(PowerAction::Count) - 1;
                m_selectedPower = static_cast<PowerAction>(p);
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                break;
            }
            default:
                break;
        }
    });

    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            if (m_coverFsPlaylistFocused) {
                m_coverFsPlaylistFocused = false;
                m_selectedItem = ItemIndex::MusicTransport;
                m_selectedMusicControl = MusicControl::Playlist;
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            if (m_selectedItem == ItemIndex::MusicProgress) {
                if (m_music.durationSeconds > 0.f) {
                    float next = m_music.positionSeconds + 5.f;
                    next = std::min(next, m_music.durationSeconds);
                    m_music.positionSeconds = next;
                    if (m_callbacks.onMusicSeek) m_callbacks.onMusicSeek(next);
                    if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                }
            } else {
                cycleCoverFsControl(1);
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            }
            return;
        }
        // Playlist open: RIGHT hops focus into Quick Settings without closing.
        if (m_playlistOpen && !m_playlistFocusParked) {
            if (m_playlistReorderMode) return;
            m_playlistFocusParked = true;
            m_playlistEyeFocused = false;
            updateCursorTarget();
            if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            return;
        }
        switch (m_selectedItem) {
            case ItemIndex::Brightness:
                adjustSlider(ItemIndex::Brightness, 0.05f);
                break;
            case ItemIndex::MusicTransport:
                cycleMusicControl(1);
                break;
            case ItemIndex::MusicProgress: {
                if (m_music.durationSeconds <= 0.f) break;
                float next = m_music.positionSeconds + 5.f;
                if (m_music.durationSeconds > 0.f)
                    next = std::min(next, m_music.durationSeconds);
                m_music.positionSeconds = next;
                if (m_callbacks.onMusicSeek) m_callbacks.onMusicSeek(next);
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                break;
            }
            case ItemIndex::BgmVolume:
                adjustSlider(ItemIndex::BgmVolume, 0.05f);
                break;
            case ItemIndex::SfxVolume:
                adjustSlider(ItemIndex::SfxVolume, 0.05f);
                break;
            case ItemIndex::AirplaneMode:
            case ItemIndex::Wifi:
                // A / tap toggles; Right does not.
                break;
            case ItemIndex::PowerActions: {
                int p = static_cast<int>(m_selectedPower) + 1;
                if (p >= static_cast<int>(PowerAction::Count)) p = 0;
                m_selectedPower = static_cast<PowerAction>(p);
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                break;
            }
            default:
                break;
        }
    });

    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            if (m_coverFsPlaylistOpen && m_coverFsPlaylistFocused) {
                activatePlaylistRow();
                return;
            }
            if (m_selectedItem == ItemIndex::MusicProgress)
                return;
            activateCoverFsControl();
            return;
        }
        if (m_playlistOpen && !m_playlistFocusParked) {
            if (m_playlistEyeFocused) {
                if (!hasPlaylistCover()) return;
                togglePlaylistArtVisible();
                return;
            }
            if (m_playlistArtFocused) {
                openCoverFullscreen();
                return;
            }
            if (m_playlistReorderMode) {
                confirmPlaylistReorder();
                return;
            }
            activatePlaylistRow();
            return;
        }
        switch (m_selectedItem) {
            case ItemIndex::MusicTransport:
                activateMusicControl();
                break;
            case ItemIndex::AirplaneMode:
                toggleItem(ItemIndex::AirplaneMode);
                break;
            case ItemIndex::Wifi:
                toggleItem(ItemIndex::Wifi);
                break;
            case ItemIndex::PowerActions:
                triggerPowerAction(m_selectedPower);
                break;
            default:
                break;
        }
    });

    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            if (m_coverFsPlaylistOpen) {
                toggleCoverFsPlaylist();
                return;
            }
            closeCoverFullscreen();
            return;
        }
        if (m_playlistReorderMode) {
            cancelPlaylistReorder();
            return;
        }
        if (m_playlistOpen && m_playlistFocusParked) {
            // From QS side with playlist still open: first B returns to playlist.
            m_playlistFocusParked = false;
            updateCursorTarget();
            if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
            return;
        }
        if (m_playlistOpen) {
            closePlaylist();
            return;
        }
        if (m_callbacks.onClose) {
            m_callbacks.onClose();
        } else {
            hide();
        }
    });

    addAction(static_cast<uint64_t>(nxui::Button::Y), [this]() {
        if (!m_active || m_coverFullscreen) return;
        if (!m_playlistOpen || m_playlistFocusParked || m_playlistReorderMode) return;
        beginPlaylistReorder();
    });

    addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (!m_active || m_coverFullscreen) return;
        if (!m_playlistOpen || m_playlistFocusParked || m_playlistReorderMode) return;
        if (!hasPlaylistCover()) return;
        togglePlaylistArtVisible();
    });

    addAction(static_cast<uint64_t>(nxui::Button::L), [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            closeCoverFullscreen();
            return;
        }
        if (m_playlistReorderMode) {
            cancelPlaylistReorder();
            return;
        }
        if (m_playlistOpen) {
            closePlaylist();
            return;
        }
        if (m_callbacks.onClose) {
            m_callbacks.onClose();
        } else {
            hide();
        }
    });

    // Media shortcuts while Quick Settings is open (home ZL/ZR/R stay page/sort).
    addAction(static_cast<uint64_t>(nxui::Button::ZL), [this]() {
        if (!m_active || m_playlistReorderMode) return;
        if (m_callbacks.onMusicPrev) m_callbacks.onMusicPrev();
        if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
        syncMusicFromCallbacks();
        if (m_playlistOpen)
            syncPlaylistToPlayingTrack(true);
        else
            updateCursorTarget();
    });
    addAction(static_cast<uint64_t>(nxui::Button::ZR), [this]() {
        if (!m_active || m_playlistReorderMode) return;
        if (m_callbacks.onMusicNext) m_callbacks.onMusicNext();
        if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
        syncMusicFromCallbacks();
        if (m_playlistOpen)
            syncPlaylistToPlayingTrack(true);
        else
            updateCursorTarget();
    });
    addAction(static_cast<uint64_t>(nxui::Button::R), [this]() {
        if (!m_active || m_playlistReorderMode) return;
        if (m_callbacks.onMusicPlayPause) m_callbacks.onMusicPlayPause();
        if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        syncMusicFromCallbacks();
        updateCursorTarget();
    });

    // + / Start toggles playlist (side sheet, or fullscreen sheet when in cover FS).
    addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            toggleCoverFsPlaylist();
            return;
        }
        if (m_playlistReorderMode) {
            cancelPlaylistReorder();
            return;
        }
        if (m_playlistOpen) {
            if (m_playlistFocusParked) {
                m_playlistFocusParked = false;
                m_playlistEyeFocused = false;
                m_playlistArtFocused = false;
                updateCursorTarget();
                if (m_callbacks.onNavigateSfx) m_callbacks.onNavigateSfx();
                return;
            }
            closePlaylist();
            return;
        }
        openPlaylist();
        if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
    });

    // Select (−): open cover fullscreen from QS; while fullscreen, close it
    // (Plus still toggles the playlist sheet). Home layout toggle is blocked
    // while QS is active.
    addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        if (!m_active) return;
        if (m_coverFullscreen) {
            closeCoverFullscreen();
            return;
        }
        if (m_playlistReorderMode) return;
        if (!m_playlistArtVisible)
            m_playlistArtVisible = true;
        if (!m_playlistOpen)
            openPlaylist();
        m_playlistFocusParked = false;
        m_playlistEyeFocused = false;
        m_playlistArtFocused = true;
        if (m_coverTex.valid()) {
            openCoverFullscreen();
        } else if (hasPlaylistCover()) {
            // Texture uploads on next paint — open as soon as it lands.
            m_coverFullscreenPending = true;
            if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        }
    });

    // L3 cycles repeat mode; R3 toggles shuffle.
    addAction(static_cast<uint64_t>(nxui::Button::LStick), [this]() {
        if (!m_active || m_playlistReorderMode) return;
        if (m_callbacks.onMusicCycleRepeat) m_callbacks.onMusicCycleRepeat();
        if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        syncMusicFromCallbacks();
        if (!m_coverFullscreen)
            updateCursorTarget();
    });
    addAction(static_cast<uint64_t>(nxui::Button::RStick), [this]() {
        if (!m_active || m_playlistReorderMode) return;
        if (m_callbacks.onMusicToggleShuffle) m_callbacks.onMusicToggleShuffle();
        if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        syncMusicFromCallbacks();
        if (!m_coverFullscreen)
            updateCursorTarget();
    });
}

void QuickSettingsOverlay::setInitialValues(float brightness, float bgmVolume, float sfxVolume,
                                            bool airplaneMode, bool wifiEnabled) {
    m_brightness = std::clamp(brightness, 0.05f, 1.0f);
    m_bgmVolume = std::clamp(bgmVolume, 0.0f, 1.0f);
    m_sfxVolume = std::clamp(sfxVolume, 0.0f, 1.0f);
    m_airplaneMode = airplaneMode;
    m_wifiEnabled = wifiEnabled;
}

void QuickSettingsOverlay::setBatteryStatus(int percent, bool charging) {
    m_batteryPercent = percent;
    m_batteryCharging = charging;
}

void QuickSettingsOverlay::show() {
    m_active = true;
    m_animating = true;
    m_animProgress = 0.f;
    setVisible(true);
    setFocusable(true);
    m_selectedItem = ItemIndex::MusicTransport;
    m_selectedMusicControl = MusicControl::PlayPause;
    m_selectedPower = PowerAction::Sleep;
    m_statusPollTimer = 0.f;
    closePlaylist();
    m_playlistAnim = 0.f;

    // L3/R3 are music shortcuts here — don't let Input's gyro pointer steal them.
    if (m_input)
        m_input->setVirtualPointerShortcutsEnabled(false);

    refreshHardwareStatus();
    refreshMusicState();
    updateCursorTarget();
    DebugLog::log("[quicksettings] shown");
}

void QuickSettingsOverlay::hide() {
    if (!m_active) return;
    m_active = false;
    m_animating = true;
    m_playlistOpen = false;
    m_coverFullscreen = false;
    m_coverFullscreenPending = false;
    m_coverFsPlaylistOpen = false;
    m_coverFsPlaylistFocused = false;
    m_coverFsPlaylistAnim = 0.f;
    m_playlistEyeFocused = false;
    m_playlistArtFocused = false;
    if (m_input)
        m_input->setVirtualPointerShortcutsEnabled(true);
    setFocusable(false);
    DebugLog::log("[quicksettings] hiding");
}

void QuickSettingsOverlay::refreshHardwareStatus() {
#ifdef __SWITCH__
    float b = 0.5f;
    if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&b))) {
        m_brightness = std::clamp(b, 0.05f, 1.0f);
    }

    bool ap = false;
    if (R_SUCCEEDED(queryAirplaneMode(&ap))) {
        m_airplaneMode = ap;
    }

    bool wf = true;
    if (R_SUCCEEDED(setsysGetWirelessLanEnableFlag(&wf))) {
        m_wifiEnabled = wf;
    }

    if (m_batteryPercent < 0) {
        u32 pct = 0;
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) {
            m_batteryPercent = static_cast<int>(pct);
            PsmChargerType ct = PsmChargerType_Unconnected;
            if (R_SUCCEEDED(psmGetChargerType(&ct))) {
                m_batteryCharging = (ct != PsmChargerType_Unconnected);
            }
        }
    }

    s32 soc = 0;
    s32 pcb = 0;
    m_hasSocTemp = false;
    m_hasPcbTemp = false;

    if (R_SUCCEEDED(tsInitialize())) {
        if (R_SUCCEEDED(tsGetTemperature(TsLocation_External, &soc))) {
            m_socTemp = static_cast<float>(soc);
            m_hasSocTemp = true;
        }
        if (R_SUCCEEDED(tsGetTemperature(TsLocation_Internal, &pcb))) {
            m_pcbTemp = static_cast<float>(pcb);
            m_hasPcbTemp = true;
        }
        tsExit();
    }

    if (!m_hasPcbTemp && R_SUCCEEDED(tcInitialize())) {
        s32 skin = 0;
        if (R_SUCCEEDED(tcGetSkinTemperatureMilliC(&skin))) {
            m_pcbTemp = static_cast<float>(skin) / 1000.f;
            m_hasPcbTemp = true;
        }
        tcExit();
    }
#else
    if (m_batteryPercent < 0) {
        m_batteryPercent = 85;
        m_batteryCharging = true;
    }
    m_socTemp = 42.0f;
    m_pcbTemp = 36.5f;
    m_hasSocTemp = true;
    m_hasPcbTemp = true;
#endif
}

void QuickSettingsOverlay::adjustSlider(ItemIndex item, float delta, bool playSfx) {
    float* target = nullptr;
    std::function<void(float)> cb;

    switch (item) {
        case ItemIndex::Brightness:
            target = &m_brightness;
            cb = m_callbacks.onBrightnessChanged;
            break;
        case ItemIndex::BgmVolume:
            target = &m_bgmVolume;
            cb = m_callbacks.onBgmVolumeChanged;
            break;
        case ItemIndex::SfxVolume:
            target = &m_sfxVolume;
            cb = m_callbacks.onSfxVolumeChanged;
            break;
        default:
            return;
    }

    if (!target) return;
    float minVal = (item == ItemIndex::Brightness) ? 0.05f : 0.0f;
    *target = std::clamp(*target + delta, minVal, 1.0f);

#ifdef __SWITCH__
    if (item == ItemIndex::Brightness) {
        lblSetCurrentBrightnessSetting(*target);
    }
#endif

    if (item == ItemIndex::BgmVolume)
        m_music.volume = m_bgmVolume;

    if (cb) cb(*target);
    if (playSfx && m_callbacks.onNavigateSfx)
        m_callbacks.onNavigateSfx();
}

void QuickSettingsOverlay::toggleItem(ItemIndex item) {
    if (item == ItemIndex::AirplaneMode) {
        const bool requested = !m_airplaneMode;
#ifdef __SWITCH__
        const Result rc = setAirplaneModeSetting(requested);
        if (R_FAILED(rc)) {
            DebugLog::log("[quicksettings] airplane toggle failed: 0x%08X", rc);
            if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
            return;
        }
#endif
        m_airplaneMode = requested;
#ifdef __SWITCH__
        if (m_airplaneMode) {
            m_wifiEnabled = false;
        }
#endif
        if (m_callbacks.onAirplaneModeToggled)
            m_callbacks.onAirplaneModeToggled(m_airplaneMode);

        if (m_airplaneMode) {
            if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        } else {
            if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
        }
    } else if (item == ItemIndex::Wifi) {
        const bool requested = !m_wifiEnabled;
#ifdef __SWITCH__
        if (requested && m_airplaneMode) {
            const Result airplaneRc = setAirplaneModeSetting(false);
            if (R_FAILED(airplaneRc)) {
                DebugLog::log("[quicksettings] airplane disable failed: 0x%08X", airplaneRc);
                if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
                return;
            }
            m_airplaneMode = false;
            if (m_callbacks.onAirplaneModeToggled)
                m_callbacks.onAirplaneModeToggled(false);
        }
        const Result wifiRc = setsysSetWirelessLanEnableFlag(requested);
        if (R_FAILED(wifiRc)) {
            DebugLog::log("[quicksettings] Wi-Fi toggle failed: 0x%08X", wifiRc);
            if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
            return;
        }
#endif
        m_wifiEnabled = requested;
        if (m_callbacks.onWifiToggled)
            m_callbacks.onWifiToggled(m_wifiEnabled);

        if (m_wifiEnabled) {
            if (m_callbacks.onActivateSfx) m_callbacks.onActivateSfx();
        } else {
            if (m_callbacks.onToggleOffSfx) m_callbacks.onToggleOffSfx();
        }
    }
}

void QuickSettingsOverlay::triggerPowerAction(PowerAction action) {
    switch (action) {
        case PowerAction::Sleep:
            if (m_callbacks.onSleepRequested) m_callbacks.onSleepRequested();
            break;
        case PowerAction::Reboot:
            if (m_callbacks.onRebootRequested) m_callbacks.onRebootRequested();
            break;
        case PowerAction::Shutdown:
            if (m_callbacks.onShutdownRequested) m_callbacks.onShutdownRequested();
            break;
        default:
            break;
    }
}

nxui::Rect QuickSettingsOverlay::computePanelRect() const {
    float openX = 1280.f - kPanelWidth - kPanelMarginRight;
    float closedX = 1280.f;
    float eased = nxui::Easing::outCubic(m_animProgress);
    float curX = closedX + (openX - closedX) * eased;
    return {curX, kPanelTop, kPanelWidth, kPanelHeight};
}

nxui::Rect QuickSettingsOverlay::computePlaylistRect() const {
    nxui::Rect panel = computePanelRect();
    float eased = nxui::Easing::outCubic(m_playlistAnim);
    float x = panel.x - kPlaylistWidth * eased;
    return {x, panel.y, kPlaylistWidth, kPanelHeight};
}

nxui::Rect QuickSettingsOverlay::computeItemRect(ItemIndex item) const {
    nxui::Rect panel = computePanelRect();
    float cx = panel.x + 22.f;
    float cw = panel.width - 44.f;

    switch (item) {
        case ItemIndex::Brightness:
            return {cx - 4.f, panel.y + kBrightnessY, cw + 8.f, kSliderItemH};
        case ItemIndex::MusicTransport:
            return {cx - 4.f, panel.y + kMusicY, cw + 8.f, kMusicH};
        case ItemIndex::MusicProgress:
            return {cx - 4.f, panel.y + kMusicY + kMusicProgressLabelY - 4.f, cw + 8.f,
                    (kMusicProgressTrackY - kMusicProgressLabelY) + kSliderTrackH + 8.f};
        case ItemIndex::BgmVolume:
            return {cx - 4.f, panel.y + kBgmY, cw + 8.f, kSliderItemH};
        case ItemIndex::SfxVolume:
            return {cx - 4.f, panel.y + kSfxY, cw + 8.f, kSliderItemH};
        case ItemIndex::AirplaneMode:
            return {cx - 4.f, panel.y + kAirplaneY, cw + 8.f, kToggleH};
        case ItemIndex::Wifi:
            return {cx - 4.f, panel.y + kWifiY, cw + 8.f, kToggleH};
        case ItemIndex::PowerActions:
            return computePowerButtonRect(m_selectedPower);
        default:
            return {cx, panel.y, cw, 40.f};
    }
}

nxui::Rect QuickSettingsOverlay::computeMusicCardRect() const {
    return computeItemRect(ItemIndex::MusicTransport);
}

nxui::Rect QuickSettingsOverlay::computePowerButtonRect(PowerAction action) const {
    // Match the outer width of every other QS card (cx - 4, cw + 8).
    nxui::Rect panel = computePanelRect();
    float left = panel.x + 18.f;
    float rowW = panel.width - 36.f;
    float btnW = (rowW - 2.f * kPowerBtnGap) / 3.f;
    int idx = static_cast<int>(action);
    float bx = left + idx * (btnW + kPowerBtnGap);
    return {bx, panel.y + kPowerBtnY, btnW, kPowerBtnH};
}

nxui::Rect QuickSettingsOverlay::computeSliderTrackRect(ItemIndex item) const {
    nxui::Rect panel = computePanelRect();
    float cx = panel.x + 26.f;
    float cw = panel.width - 52.f;
    const float sliderTrackY = 30.f;

    switch (item) {
        case ItemIndex::Brightness:
            return {cx, panel.y + kBrightnessY + sliderTrackY, cw, kSliderTrackH};
        case ItemIndex::BgmVolume:
            return {cx, panel.y + kBgmY + sliderTrackY, cw, kSliderTrackH};
        case ItemIndex::SfxVolume:
            return {cx, panel.y + kSfxY + sliderTrackY, cw, kSliderTrackH};
        default:
            return {};
    }
}

nxui::Rect QuickSettingsOverlay::computeMusicControlRect(MusicControl control) const {
    nxui::Rect card = computeMusicCardRect();
    const int count = static_cast<int>(MusicControl::Count);
    const int idx = static_cast<int>(control);
    float padX = 8.f;
    float rowY = card.y + kMusicControlsY;
    float rowH = kMusicControlsH;
    float gap = 6.f;
    float totalGap = gap * static_cast<float>(count - 1);
    float btnW = (card.width - padX * 2.f - totalGap) / static_cast<float>(count);
    float x = card.x + padX + idx * (btnW + gap);
    return {x, rowY, btnW, rowH};
}

nxui::Rect QuickSettingsOverlay::computeProgressTrackRect() const {
    nxui::Rect panel = computePanelRect();
    float cx = panel.x + 26.f;
    float cw = panel.width - 52.f;
    return {cx, panel.y + kMusicY + kMusicProgressTrackY, cw, kSliderTrackH};
}

nxui::Rect QuickSettingsOverlay::computeCoverFsCardRect() const {
    // Bottom-centered floating player over the fullscreen cover.
    const float sw = 1280.f;
    const float sh = 720.f;
    return {
        (sw - kCoverFsCardW) * 0.5f,
        sh - kCoverFsCardBottomPad - kCoverFsCardH,
        kCoverFsCardW,
        kCoverFsCardH
    };
}

nxui::Rect QuickSettingsOverlay::computeCoverFsControlRect(MusicControl control) const {
    nxui::Rect card = computeCoverFsCardRect();
    const int idx = static_cast<int>(control);
    if (idx < 0 || idx >= kCoverFsControlCount)
        return {};
    float padX = 14.f;
    float rowY = card.y + kCoverFsControlsY;
    float rowH = kCoverFsControlsH;
    float gap = 8.f;
    float totalGap = gap * static_cast<float>(kCoverFsControlCount - 1);
    float btnW = (card.width - padX * 2.f - totalGap) / static_cast<float>(kCoverFsControlCount);
    float x = card.x + padX + idx * (btnW + gap);
    return {x, rowY, btnW, rowH};
}

nxui::Rect QuickSettingsOverlay::computeCoverFsProgressTrackRect() const {
    nxui::Rect card = computeCoverFsCardRect();
    float padX = 18.f;
    return {card.x + padX, card.y + kCoverFsProgressTrackY,
            card.width - padX * 2.f, kSliderTrackH};
}

nxui::Rect QuickSettingsOverlay::computeCoverFsPlaylistRect() const {
    nxui::Rect card = computeCoverFsCardRect();
    // Full-height sheet slides up from under the player card (like the QS side sheet).
    const float topPad = kCoverFsCardBottomPad;
    const float maxH = std::max(80.f, card.y - topPad);
    const float eased = nxui::Easing::outCubic(m_coverFsPlaylistAnim);
    const float overlap = 18.f; // tuck under card so bottom rounding is hidden
    const float y = card.y - maxH * eased;
    const float h = maxH + overlap * eased;
    return {card.x, y, card.width, h};
}

nxui::Rect QuickSettingsOverlay::computeCoverFsPlaylistListRect() const {
    nxui::Rect panel = computeCoverFsPlaylistRect();
    const float overlap = 18.f * nxui::Easing::outCubic(m_coverFsPlaylistAnim);
    return {
        panel.x + kCoverFsPlaylistListPad,
        panel.y + kCoverFsPlaylistHeaderH,
        panel.width - kCoverFsPlaylistListPad * 2.f,
        std::max(40.f, panel.height - overlap - kCoverFsPlaylistHeaderH - kCoverFsPlaylistListPad)
    };
}

nxui::Rect QuickSettingsOverlay::activePlaylistListRect() const {
    if (m_coverFullscreen && m_coverFsPlaylistAnim > 0.001f)
        return computeCoverFsPlaylistListRect();
    return computePlaylistListRect();
}

float QuickSettingsOverlay::playlistArtHeight() const {
    if (!m_coverTex.valid() || !m_playlistArtVisible)
        return 0.f;
    nxui::Rect pl = computePlaylistRect();
    return std::max(0.f, pl.width - kPlaylistArtSidePad * 2.f);
}

nxui::Rect QuickSettingsOverlay::computePlaylistArtRect() const {
    nxui::Rect pl = computePlaylistRect();
    const float artW = pl.width - kPlaylistArtSidePad * 2.f;
    const float artH = playlistArtHeight();
    return {pl.x + kPlaylistArtSidePad, pl.y + kPlaylistHeaderBaseH, artW, artH};
}

nxui::Rect QuickSettingsOverlay::computePlaylistEyeRect() const {
    nxui::Rect pl = computePlaylistRect();
    return {
        pl.x + pl.width - 12.f - kPlaylistEyeSize,
        pl.y + (kPlaylistHeaderBaseH - kPlaylistEyeSize) * 0.5f,
        kPlaylistEyeSize,
        kPlaylistEyeSize
    };
}

float QuickSettingsOverlay::playlistHeaderHeight() const {
    const float artH = playlistArtHeight();
    if (artH > 0.f)
        return kPlaylistHeaderBaseH + kPlaylistArtPad + artH + 8.f;
    return kPlaylistHeaderBaseH;
}

nxui::Rect QuickSettingsOverlay::computePlaylistListRect() const {
    nxui::Rect pl = computePlaylistRect();
    const float headerH = playlistHeaderHeight();
    return {pl.x + 8.f, pl.y + headerH, pl.width - 16.f,
            pl.height - headerH - kPlaylistListBottomPad};
}

float QuickSettingsOverlay::playlistMaxScroll() const {
    const int count = playlistVisibleCount();
    if (count <= 0) return 0.f;
    const float contentH = static_cast<float>(count) * kPlaylistRowH;
    const float viewH = activePlaylistListRect().height;
    return std::max(0.f, contentH - viewH);
}

nxui::Rect QuickSettingsOverlay::computePlaylistRowRect(int index) const {
    nxui::Rect list = activePlaylistListRect();
    float y = list.y + static_cast<float>(index) * kPlaylistRowH - m_playlistScrollY;
    if (m_playlistDragging && index == m_playlistDragIndex)
        y += m_playlistDragOffsetY;
    return {list.x, y, list.width, kPlaylistRowH - 4.f};
}

nxui::Rect QuickSettingsOverlay::computePlaylistHandleRect(int index) const {
    nxui::Rect row = computePlaylistRowRect(index);
    return {row.x, row.y, kPlaylistHandleW, row.height};
}

int QuickSettingsOverlay::playlistIndexAtPoint(float x, float y) const {
    const int count = playlistVisibleCount();
    if (count <= 0) return -1;
    nxui::Rect list = activePlaylistListRect();
    if (!list.contains(x, y)) return -1;
    int idx = static_cast<int>((y - list.y + m_playlistScrollY) / kPlaylistRowH);
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

void QuickSettingsOverlay::updateCursorTarget(bool instant) {
    if (m_coverFullscreen) {
        m_cursor.setVisible(false);
        return;
    }
    const float dur = instant ? 0.f : 0.15f;
    if (m_playlistOpen && !m_playlistFocusParked && m_playlistAnim > 0.01f) {
        if (m_playlistEyeFocused) {
            m_cursor.setVisible(true);
            m_cursor.moveTo(computePlaylistEyeRect(), 8.f, dur);
            return;
        }
        if (m_playlistArtFocused && playlistArtFocusable()) {
            m_cursor.setVisible(true);
            m_cursor.moveTo(computePlaylistArtRect(), 14.f, dur);
            return;
        }
        if (!m_music.playlistTitles.empty()) {
            nxui::Rect list = computePlaylistListRect();
            nxui::Rect r = computePlaylistRowRect(m_playlistFocus);
            // Touch scroll/reorder can park the focused row outside the list
            // viewport (over cover art / below the panel). Hide the cursor then;
            // the clipped row fill still shows when partially visible.
            if (r.y + r.height < list.y || r.y > list.bottom()) {
                m_cursor.setVisible(false);
                return;
            }
            m_cursor.setVisible(true);
            // Instant snap while finger-scrolling — animated chase desyncs badly.
            const float rowDur = (instant || m_playlistScrolling || m_playlistDragging)
                ? 0.f : 0.15f;
            m_cursor.moveTo(r, 10.f, rowDur);
            return;
        }
    }

    m_cursor.setVisible(true);
    if (m_selectedItem == ItemIndex::MusicTransport) {
        nxui::Rect r = computeMusicControlRect(m_selectedMusicControl);
        m_cursor.moveTo(r, 10.f, dur);
        return;
    }

    nxui::Rect r = computeItemRect(m_selectedItem);
    m_cursor.moveTo(r, 14.f, dur);
}

void QuickSettingsOverlay::update(float dt) {
    if (!isVisible()) return;

    setRect(computePanelRect());

    if (m_animating) {
        if (m_active) {
            m_animProgress += dt / kAnimDuration;
            if (m_animProgress >= 1.f) {
                m_animProgress = 1.f;
                m_animating = false;
            }
        } else {
            m_animProgress -= dt / kAnimDuration;
            if (m_animProgress <= 0.f) {
                m_animProgress = 0.f;
                m_animating = false;
                setVisible(false);
            }
        }
        updateCursorTarget();
    }

    const float playlistTarget = m_playlistOpen ? 1.f : 0.f;
    if (std::fabs(m_playlistAnim - playlistTarget) > 0.001f) {
        const float step = dt / kAnimDuration;
        if (m_playlistAnim < playlistTarget)
            m_playlistAnim = std::min(playlistTarget, m_playlistAnim + step);
        else
            m_playlistAnim = std::max(playlistTarget, m_playlistAnim - step);
        updateCursorTarget();
    }
    if (m_playlistCenterPending && m_playlistOpen && m_playlistAnim >= 0.999f) {
        centerPlaylistFocus();
        m_playlistCenterPending = false;
        updateCursorTarget();
    }

    const float fsPlTarget = m_coverFsPlaylistOpen ? 1.f : 0.f;
    if (std::fabs(m_coverFsPlaylistAnim - fsPlTarget) > 0.001f) {
        const float step = dt / kAnimDuration;
        if (m_coverFsPlaylistAnim < fsPlTarget)
            m_coverFsPlaylistAnim = std::min(fsPlTarget, m_coverFsPlaylistAnim + step);
        else
            m_coverFsPlaylistAnim = std::max(fsPlTarget, m_coverFsPlaylistAnim - step);
    }

    if (isFullyVisible()) {
        m_statusPollTimer += dt;
        if (m_statusPollTimer >= 1.0f) {
            m_statusPollTimer = 0.f;
            refreshHardwareStatus();
        }
        syncMusicFromCallbacks();

        if (m_input) {
            handleTouch(*m_input);
        }
        updatePlaylistMarquee(dt);
    }

    m_cursor.update(dt);
}

void QuickSettingsOverlay::handleTouch(nxui::Input& input) {
    nxui::Rect panel = computePanelRect();
    nxui::Rect playlist = computePlaylistRect();

    if (input.touchDown()) {
        float tx = input.touchX();
        float ty = input.touchY();

        if (m_coverFullscreen) {
            nxui::Rect fsCard = computeCoverFsCardRect();
            nxui::Rect progressHit = computeCoverFsProgressTrackRect();
            progressHit = {progressHit.x - 8.f, progressHit.y - 14.f,
                           progressHit.width + 16.f, progressHit.height + 28.f};
            if (progressHit.contains(tx, ty) && m_music.durationSeconds > 0.05f) {
                m_coverFsPlaylistFocused = false;
                m_selectedItem = ItemIndex::MusicProgress;
                m_draggingProgress = true;
                seekFromProgressX(tx, computeCoverFsProgressTrackRect());
                return;
            }
            for (int i = 0; i < kCoverFsControlCount; ++i) {
                auto ctrl = static_cast<MusicControl>(i);
                if (computeCoverFsControlRect(ctrl).contains(tx, ty)) {
                    m_coverFsPlaylistFocused = false;
                    m_selectedItem = ItemIndex::MusicTransport;
                    m_selectedMusicControl = ctrl;
                    activateCoverFsControl();
                    return;
                }
            }
            if (m_coverFsPlaylistAnim > 0.05f) {
                nxui::Rect fsPl = computeCoverFsPlaylistRect();
                if (fsPl.contains(tx, ty)) {
                    if (!m_coverFsPlaylistOpen)
                        return;
                    const int count = playlistVisibleCount();
                    int hit = playlistIndexAtPoint(tx, ty);
                    if (hit >= 0 && count > 0) {
                        m_coverFsPlaylistFocused = true;
                        m_playlistFocus = hit;
                        m_playlistTouchIndex = hit;
                        m_playlistTouchStartY = ty;
                        m_playlistTouchStartScroll = m_playlistScrollY;
                        m_playlistDragging = false;
                        m_playlistScrolling = false;
                        m_playlistTouchOnHandle = false;
                        m_playlistDragIndex = -1;
                        resetPlaylistMarquee();
                    }
                    return;
                }
            }
            // Tap outside chrome closes fullscreen playlist first, then cover.
            if (!fsCard.contains(tx, ty)) {
                if (m_coverFsPlaylistOpen)
                    toggleCoverFsPlaylist();
                else
                    closeCoverFullscreen();
            }
            return;
        }

        const bool inPlaylist = m_playlistAnim > 0.05f && playlist.contains(tx, ty);
        const bool inPanel = panel.contains(tx, ty);

        if (!inPlaylist && !inPanel) {
            if (m_playlistOpen) {
                closePlaylist();
            } else if (m_callbacks.onClose) {
                m_callbacks.onClose();
            } else {
                hide();
            }
            return;
        }

        if (inPlaylist) {
            if (hasPlaylistCover() && computePlaylistEyeRect().contains(tx, ty)) {
                // Touching the eye must claim playlist focus the same way
                // controller navigation does — otherwise parked QS focus leaves
                // the selection cursor stranded while the eye outline lights up.
                m_playlistFocusParked = false;
                m_playlistEyeFocused = true;
                m_playlistArtFocused = false;
                togglePlaylistArtVisible();
                updateCursorTarget(true);
                return;
            }
            if (playlistArtFocusable() && computePlaylistArtRect().contains(tx, ty)) {
                m_playlistFocusParked = false;
                m_playlistArtFocused = true;
                m_playlistEyeFocused = false;
                openCoverFullscreen();
                return;
            }
            if (m_playlistReorderMode)
                cancelPlaylistReorder();
            const int count = playlistVisibleCount();
            if (count > 0) {
                int hit = playlistIndexAtPoint(tx, ty);
                if (hit >= 0) {
                    const bool focusChanged = (hit != m_playlistFocus);
                    m_playlistFocusParked = false;
                    m_playlistFocus = hit;
                    m_playlistEyeFocused = false;
                    m_playlistArtFocused = false;
                    m_playlistTouchIndex = hit;
                    m_playlistTouchStartY = ty;
                    m_playlistTouchStartScroll = m_playlistScrollY;
                    m_playlistDragging = false;
                    m_playlistScrolling = false;
                    m_playlistTouchOnHandle = playlistReorderAllowed()
                        && computePlaylistHandleRect(hit).contains(tx, ty);
                    m_playlistDragIndex = m_playlistTouchOnHandle ? hit : -1;
                    m_playlistDragOffsetY = 0.f;
                    if (focusChanged)
                        resetPlaylistMarquee();
                    updateCursorTarget(true);
                }
            }
            return;
        }

        if (m_playlistOpen && inPanel) {
            // Touching the sidebar while playlist is open keeps QS usable;
            // fall through to normal panel controls.
        }

        if (computeMusicCardRect().contains(tx, ty)) {
            nxui::Rect progressHit = computeProgressTrackRect();
            progressHit = {progressHit.x - 8.f, progressHit.y - 12.f,
                           progressHit.width + 16.f, progressHit.height + 24.f};
            if (progressHit.contains(tx, ty) && m_music.durationSeconds > 0.05f) {
                m_selectedItem = ItemIndex::MusicProgress;
                m_draggingProgress = true;
                seekFromProgressX(tx);
                updateCursorTarget();
                return;
            }
            m_selectedItem = ItemIndex::MusicTransport;
            for (int i = 0; i < static_cast<int>(MusicControl::Count); ++i) {
                auto ctrl = static_cast<MusicControl>(i);
                if (computeMusicControlRect(ctrl).contains(tx, ty)) {
                    m_selectedMusicControl = ctrl;
                    activateMusicControl();
                    updateCursorTarget();
                    return;
                }
            }
        }

        for (auto item : {ItemIndex::Brightness, ItemIndex::BgmVolume, ItemIndex::SfxVolume}) {
            nxui::Rect track = computeSliderTrackRect(item);
            nxui::Rect hitArea = {track.x - 10.f, track.y - 14.f, track.width + 20.f, track.height + 28.f};
            if (hitArea.contains(tx, ty)) {
                m_selectedItem = item;
                m_draggingSlider = true;
                m_draggedSlider = item;
                float val = std::clamp((tx - track.x) / track.width, 0.0f, 1.0f);
                if (item == ItemIndex::Brightness) val = std::max(0.05f, val);
                adjustSlider(item, val - ((item == ItemIndex::Brightness) ? m_brightness
                    : (item == ItemIndex::BgmVolume ? m_bgmVolume : m_sfxVolume)),
                    false);
                updateCursorTarget();
                return;
            }
        }

        nxui::Rect apRect = computeItemRect(ItemIndex::AirplaneMode);
        if (apRect.contains(tx, ty)) {
            m_selectedItem = ItemIndex::AirplaneMode;
            toggleItem(ItemIndex::AirplaneMode);
            updateCursorTarget();
            return;
        }

        nxui::Rect wfRect = computeItemRect(ItemIndex::Wifi);
        if (wfRect.contains(tx, ty)) {
            m_selectedItem = ItemIndex::Wifi;
            toggleItem(ItemIndex::Wifi);
            updateCursorTarget();
            return;
        }

        for (auto pa : {PowerAction::Sleep, PowerAction::Reboot, PowerAction::Shutdown}) {
            nxui::Rect btnRect = computePowerButtonRect(pa);
            if (btnRect.contains(tx, ty)) {
                m_selectedItem = ItemIndex::PowerActions;
                m_selectedPower = pa;
                updateCursorTarget();
                triggerPowerAction(pa);
                return;
            }
        }
    }

    if (input.isTouching() && m_playlistTouchIndex >= 0
        && (m_playlistAnim > 0.05f || m_coverFsPlaylistAnim > 0.05f)) {
        const int count = playlistVisibleCount();
        if (count > 0) {
            const float dy = input.touchY() - m_playlistTouchStartY;
            if (playlistReorderAllowed() && m_playlistTouchOnHandle) {
                if (!m_playlistDragging && std::fabs(dy) > 8.f)
                    m_playlistDragging = true;
                if (m_playlistDragging) {
                    m_playlistDragOffsetY = dy;
                    int hover = playlistIndexAtPoint(input.touchX(), input.touchY());
                    if (hover < 0) {
                        nxui::Rect list = computePlaylistListRect();
                        hover = static_cast<int>((input.touchY() - list.y + m_playlistScrollY) / kPlaylistRowH);
                        hover = std::clamp(hover, 0, count - 1);
                    }
                    m_playlistFocus = hover;
                    updateCursorTarget();
                }
            } else {
                if (!m_playlistScrolling && std::fabs(dy) > 8.f)
                    m_playlistScrolling = true;
                if (m_playlistScrolling) {
                    m_playlistScrollY = std::clamp(m_playlistTouchStartScroll - dy, 0.f, playlistMaxScroll());
                    // Keep highlight under the finger so it stays in the list.
                    int under = playlistIndexAtPoint(input.touchX(), input.touchY());
                    if (under >= 0)
                        m_playlistFocus = under;
                    updateCursorTarget();
                }
            }
        }
    }

    if (input.isTouching() && m_draggingProgress) {
        if (m_coverFullscreen)
            seekFromProgressX(input.touchX(), computeCoverFsProgressTrackRect());
        else
            seekFromProgressX(input.touchX());
    }

    if (input.isTouching() && m_draggingSlider) {
        nxui::Rect track = computeSliderTrackRect(m_draggedSlider);
        float tx = input.touchX();
        float val = std::clamp((tx - track.x) / track.width, 0.0f, 1.0f);
        if (m_draggedSlider == ItemIndex::Brightness) val = std::max(0.05f, val);
        adjustSlider(m_draggedSlider, val - ((m_draggedSlider == ItemIndex::Brightness) ? m_brightness
            : (m_draggedSlider == ItemIndex::BgmVolume ? m_bgmVolume : m_sfxVolume)),
            false);
    }

    if (input.touchUp()) {
        if (m_playlistTouchIndex >= 0) {
            const int count = playlistVisibleCount();
            if (count > 0) {
                if (playlistReorderAllowed()
                    && m_playlistTouchOnHandle && m_playlistDragging
                    && m_callbacks.onMusicMoveTrack) {
                    int toIndex = m_playlistFocus;
                    if (toIndex != m_playlistDragIndex && m_playlistDragIndex >= 0)
                        m_callbacks.onMusicMoveTrack(m_playlistDragIndex, toIndex);
                    syncMusicFromCallbacks();
                } else if (!m_playlistDragging && !m_playlistScrolling) {
                    m_playlistFocus = m_playlistTouchIndex;
                    activatePlaylistRow();
                }
            }
        }
        m_playlistDragging = false;
        m_playlistScrolling = false;
        m_playlistDragIndex = -1;
        m_playlistTouchIndex = -1;
        m_playlistDragOffsetY = 0.f;
        m_playlistTouchOnHandle = false;
        m_draggingSlider = false;
        m_draggingProgress = false;
    }
}

void QuickSettingsOverlay::render(nxui::Renderer& ren) {
    if (!isVisible() || m_animProgress <= 0.001f) return;

    auto& i18n = nxui::I18n::instance();
    float alpha = m_animProgress;

    nxui::Rect screen = {0.f, 0.f, (float)ren.width(), (float)ren.height()};
    nxui::Color scrim = m_theme
        ? nxui::Color::lerp(m_theme->background, nxui::Color::black(),
                            m_theme->mode == nxui::ThemeMode::Dark ? 0.65f : 0.20f)
              .withAlpha((m_theme->mode == nxui::ThemeMode::Dark ? 0.25f : 0.14f) * alpha)
        : nxui::Color(0.f, 0.f, 0.f, 0.20f * alpha);
    ren.drawRect(screen, scrim);

    nxui::Rect panel = computePanelRect();
    const bool lightMode = m_theme && m_theme->mode == nxui::ThemeMode::Light;
    nxui::Color panelFill = lightMode
        ? nxui::Color(0.96f, 0.97f, 0.99f, 0.97f * alpha)
        : (m_theme
            ? m_theme->panelBase.withAlpha(0.92f * alpha)
            : nxui::Color(0.10f, 0.13f, 0.18f, 0.90f * alpha));

    const nxui::Color primaryText = lightMode
        ? nxui::Color(0.06f, 0.07f, 0.09f, alpha)
        : nxui::Color(1.f, 1.f, 1.f, alpha);
    const nxui::Color secondaryText = lightMode
        ? nxui::Color(0.18f, 0.20f, 0.24f, 0.90f * alpha)
        : nxui::Color(0.92f, 0.94f, 0.98f, 0.90f * alpha);
    const nxui::Color mutedText = lightMode
        ? nxui::Color(0.31f, 0.34f, 0.40f, alpha)
        : nxui::Color(0.65f, 0.72f, 0.82f, alpha);
    const nxui::Color neutralBorder = lightMode
        ? nxui::Color(0.08f, 0.10f, 0.14f, 0.14f * alpha)
        : nxui::Color(1.f, 1.f, 1.f, 0.14f * alpha);
    nxui::Color cursorColor = m_theme
        ? m_theme->cursorNormal.withAlpha(alpha)
        : nxui::Color(0.12f, 0.76f, 0.98f, alpha);

    nxui::Color cardFill = lightMode
        ? nxui::Color(1.f, 1.f, 1.f, 0.86f * alpha)
        : (m_theme
            ? m_theme->panelHighlight.withAlpha(0.08f * alpha)
            : nxui::Color(1.f, 1.f, 1.f, 0.07f * alpha));

    if (m_playlistAnim > 0.001f) {
        nxui::Rect pl = computePlaylistRect();
        ren.drawRoundedRect(pl, panelFill, 24.f);
        ren.drawRoundedRectOutline(pl, neutralBorder, 24.f, 1.2f);

        refreshCoverTexture(ren);
        if (m_coverFullscreenPending && m_coverTex.valid()) {
            m_coverFullscreenPending = false;
            m_playlistArtFocused = true;
            openCoverFullscreen();
        }

        float plx = pl.x + 16.f;
        if (m_smallFont) {
            ren.drawText(i18n.tr("quicksettings.playlist", "Playlist"),
                         {plx, pl.y + 14.f}, m_smallFont, primaryText, 0.72f);
        }

        // Eye toggle stays visible even when cover art is hidden.
        if (m_coverTex.valid() || !m_coverBytes.empty()) {
            nxui::Rect eye = computePlaylistEyeRect();
            ren.drawRoundedRect(eye, cardFill, 8.f);
            ren.drawRoundedRectOutline(eye,
                m_playlistEyeFocused ? cursorColor.withAlpha(0.95f * alpha) : neutralBorder,
                8.f, m_playlistEyeFocused ? 1.6f : 1.f);
            const float ecx = eye.x + eye.width * 0.5f;
            const float ecy = eye.y + eye.height * 0.5f;
            const float er = 5.2f;
            drawArcStroke(ren, {ecx, ecy + 0.5f}, er + 1.4f, 200.f, 340.f, mutedText, 1.8f, 10);
            drawArcStroke(ren, {ecx, ecy - 0.5f}, er + 1.4f, 20.f, 160.f, mutedText, 1.8f, 10);
            ren.drawCircle({ecx, ecy}, 3.2f, mutedText);
            ren.drawCircle({ecx, ecy}, 1.35f, primaryText);
            if (!m_playlistArtVisible) {
                // Slash when art is hidden.
                ren.drawLine({eye.x + 6.f, eye.y + eye.height - 7.f},
                             {eye.x + eye.width - 6.f, eye.y + 7.f},
                             primaryText.withAlpha(0.9f * alpha), 2.f);
            }
        }

        if (m_coverTex.valid() && m_playlistArtVisible) {
            nxui::Rect art = computePlaylistArtRect();
            ren.drawRoundedRect(
                {art.x + 2.f, art.y + 3.f, art.width, art.height},
                nxui::Color(0.f, 0.f, 0.f, 0.28f * alpha), 14.f);
            ren.drawTextureRounded(&m_coverTex, art, 14.f,
                                   nxui::Color(1.f, 1.f, 1.f, alpha));
            const bool artFocused = m_playlistOpen && !m_playlistFocusParked
                && m_playlistArtFocused;
            ren.drawRoundedRectOutline(art,
                artFocused ? cursorColor.withAlpha(0.95f * alpha) : neutralBorder,
                14.f, artFocused ? 1.6f : 1.f);
        }

        const int count = playlistVisibleCount();
        if (m_smallFont) {
            if (count == 0) {
                ren.drawText(i18n.tr("quicksettings.playlist_empty", "No tracks"),
                             {plx, pl.y + playlistHeaderHeight() + 8.f}, m_smallFont, mutedText, 0.78f);
            } else {
                nxui::Rect list = computePlaylistListRect();
                const bool showHandle = playlistReorderAllowed();
                ren.pushClipRect(list);
                for (int i = 0; i < count; ++i) {
                    nxui::Rect rowRect = computePlaylistRowRect(i);
                    if (rowRect.y + rowRect.height < list.y || rowRect.y > list.y + list.height)
                        continue;

                    const auto& prow = m_playlistRows[static_cast<size_t>(i)];
                    const bool focused = m_playlistOpen && !m_playlistFocusParked
                        && !m_playlistEyeFocused && !m_playlistArtFocused
                        && i == m_playlistFocus;
                    const bool reordering = showHandle && m_playlistReorderMode
                        && prow.kind == PlaylistRowKind::Track
                        && prow.trackIndex == m_playlistReorderCurrent;
                    const bool current = prow.kind == PlaylistRowKind::Track
                        && prow.trackIndex == m_music.currentIndex;
                    const bool folderHasCurrent = prow.kind == PlaylistRowKind::Folder
                        && m_music.currentIndex >= 0
                        && m_music.currentIndex < static_cast<int>(m_music.playlistAlbumFolders.size())
                        && m_music.playlistAlbumFolders[static_cast<size_t>(m_music.currentIndex)] == prow.folder;

                    if (reordering) {
                        rowRect.y -= 2.f;
                        ren.drawRoundedRect(
                            {rowRect.x + 2.f, rowRect.y + 3.f, rowRect.width, rowRect.height},
                            nxui::Color(0.f, 0.f, 0.f, 0.22f * alpha), 10.f);
                        ren.drawRoundedRect(rowRect, cursorColor.withAlpha(0.32f * alpha), 10.f);
                        ren.drawRoundedRectOutline(rowRect, cursorColor.withAlpha(0.85f * alpha), 10.f, 1.4f);
                    } else if (focused || current || folderHasCurrent) {
                        nxui::Color rowFill = focused
                            ? cursorColor.withAlpha(0.22f * alpha)
                            : cardFill;
                        ren.drawRoundedRect(rowRect, rowFill, 10.f);
                    }

                    float contentX = rowRect.x + (prow.indented ? kPlaylistTreeIndent : 0.f);
                    if (showHandle) {
                        nxui::Rect handle = computePlaylistHandleRect(i);
                        if (reordering)
                            handle.y -= 2.f;
                        drawPlaylistHandle(ren, handle, (focused || reordering) ? primaryText : mutedText);
                        contentX = handle.x + handle.width;
                    }

                    // Thumbnail
                    nxui::Rect thumb = {
                        contentX + 2.f,
                        rowRect.y + (rowRect.height - kPlaylistThumbSize) * 0.5f,
                        kPlaylistThumbSize,
                        kPlaylistThumbSize
                    };
                    const std::vector<uint8_t>* artBytes = nullptr;
                    std::string artKey;
                    if (prow.kind == PlaylistRowKind::Folder) {
                        artKey = "folder:" + prow.folder;
                        if (m_callbacks.onMusicQueryFolderCover)
                            artBytes = m_callbacks.onMusicQueryFolderCover(prow.folder);
                    } else {
                        artKey = "track:" + std::to_string(prow.trackIndex);
                        if (m_callbacks.onMusicQueryTrackCover)
                            artBytes = m_callbacks.onMusicQueryTrackCover(prow.trackIndex);
                        if ((!artBytes || artBytes->empty()) && !prow.folder.empty()
                            && m_callbacks.onMusicQueryFolderCover)
                            artBytes = m_callbacks.onMusicQueryFolderCover(prow.folder);
                    }
                    const nxui::Texture* thumbTex = playlistThumbTexture(ren, artKey, artBytes);
                    ren.drawRoundedRect(thumb, cardFill, 6.f);
                    if (thumbTex)
                        ren.drawTextureRounded(thumbTex, thumb, 6.f,
                                               nxui::Color(1.f, 1.f, 1.f, alpha));
                    else if (prow.kind == PlaylistRowKind::Folder) {
                        // Simple folder glyph fallback.
                        ren.drawRoundedRect(
                            {thumb.x + 5.f, thumb.y + 8.f, thumb.width - 10.f, thumb.height - 12.f},
                            mutedText.withAlpha(0.35f * alpha), 3.f);
                    }

                    std::string label = prow.label;
                    if (prow.kind == PlaylistRowKind::Folder) {
                        const bool open = m_playlistExpanded.count(prow.folder) > 0;
                        label = std::string(open ? "▼ " : "▶ ") + prow.folder
                            + "  (" + std::to_string(prow.trackCount) + ")";
                    }

                    nxui::Color textCol = (current || reordering || folderHasCurrent)
                        ? primaryText : secondaryText;
                    const float textScale = 0.78f;
                    const float textX = thumb.x + thumb.width + kPlaylistThumbGap;
                    const float textMaxW = rowRect.x + rowRect.width - textX - 6.f;
                    nxui::Vec2 fullSz = m_smallFont->measure(label);
                    float ty = rowRect.y + (rowRect.height - fullSz.y * textScale) * 0.5f;
                    nxui::Rect textClip = {textX, rowRect.y, textMaxW, rowRect.height};

                    const bool marquee = (focused || reordering)
                        && fullSz.x * textScale > textMaxW + 1.f
                        && m_playlistMarqueeHold <= 0.f;

                    if (marquee) {
                        ren.pushClipRect(textClip);
                        const float cycle = fullSz.x * textScale + kMarqueeGapPx;
                        const float scroll = std::fmod(m_playlistMarqueeOffset, cycle);
                        ren.drawText(label, {textX - scroll, ty},
                                     m_smallFont, textCol, textScale);
                        ren.drawText(label, {textX - scroll + cycle, ty},
                                     m_smallFont, textCol, textScale);
                        ren.popClipRect();
                    } else {
                        std::string title = ellipsize(m_smallFont, label, textMaxW, textScale);
                        ren.drawText(title, {textX, ty}, m_smallFont, textCol, textScale);
                    }
                }
                ren.popClipRect();
            }
        }
    }

    ren.drawRoundedRect(panel, panelFill, 24.f);

    nxui::Color borderColor = m_theme
        ? m_theme->panelBorder.withAlpha((lightMode ? 0.48f : 0.28f) * alpha)
        : nxui::Color(1.f, 1.f, 1.f, 0.22f * alpha);
    ren.drawRoundedRectOutline(panel, borderColor, 24.f, 1.2f);

    nxui::Color highlightColor = m_theme
        ? m_theme->panelHighlight.withAlpha((lightMode ? 0.32f : 0.08f) * alpha)
        : nxui::Color(1.f, 1.f, 1.f, 0.08f * alpha);
    ren.drawRoundedRectOutline(panel.shrunk(1.f), highlightColor, 23.f, 1.0f);

    float cx = panel.x + 22.f;
    // Match slider / toggle / music card outer width (cx - 4, cw + 8).
    const float cardX = panel.x + 18.f;
    const float cardW = panel.width - 36.f;

    if (m_font || m_smallFont) {
        std::string title = i18n.tr("quicksettings.title", "Quick Settings");
        nxui::Font* titleFont = m_font ? m_font : m_smallFont;
        ren.drawText(title, {cx, panel.y + kTitleY}, titleFont, primaryText, 0.95f);
    }

    nxui::Rect statusCard = {cardX, panel.y + kStatusY, cardW, kStatusH};
    ren.drawRoundedRect(statusCard, cardFill, 14.f);
    ren.drawRoundedRectOutline(statusCard, neutralBorder, 14.f, 1.f);

    ren.drawLine({statusCard.x + cardW * 0.5f, statusCard.y + 8.f},
                 {statusCard.x + cardW * 0.5f, statusCard.y + kStatusH - 8.f},
                 neutralBorder, 1.f);

    if (m_smallFont) {
        const float titleY = statusCard.y + 8.f;
        const float infoY = statusCard.y + 32.f;
        const float valueScale = 0.82f;

        ren.drawText(i18n.tr("quicksettings.battery", "BATTERY"),
                     {statusCard.x + 12.f, titleY}, m_smallFont, mutedText, 0.68f);

        char bBuf[32];
        if (m_batteryPercent >= 0)
            std::snprintf(bBuf, sizeof(bBuf), "%d%%", m_batteryPercent);
        else
            std::snprintf(bBuf, sizeof(bBuf), "--%%");
        ren.drawText(bBuf, {statusCard.x + 12.f, infoY}, m_smallFont, primaryText, valueScale);

        nxui::Vec2 pctSz = m_smallFont->measure(bBuf);
        std::string chgText = m_batteryCharging
            ? i18n.tr("quicksettings.charging", "Charging")
            : i18n.tr("quicksettings.discharging", "Discharging");
        nxui::Color chgCol = m_batteryCharging
            ? nxui::Color(0.25f, 0.90f, 0.45f, alpha)
            : mutedText;
        ren.drawText(chgText, {statusCard.x + 12.f + pctSz.x * valueScale + 8.f, infoY},
                     m_smallFont, chgCol, 0.72f);

        float rx = statusCard.x + cardW * 0.5f + 12.f;
        ren.drawText(i18n.tr("quicksettings.hardware_temp", "THERMALS"),
                     {rx, titleY}, m_smallFont, mutedText, 0.68f);

        char tBuf[64];
        if (m_hasSocTemp || m_hasPcbTemp) {
            if (m_hasSocTemp && m_hasPcbTemp)
                std::snprintf(tBuf, sizeof(tBuf), "SoC: %.0f\u00B0C  PCB: %.0f\u00B0C", m_socTemp, m_pcbTemp);
            else if (m_hasSocTemp)
                std::snprintf(tBuf, sizeof(tBuf), "SoC: %.0f\u00B0C", m_socTemp);
            else
                std::snprintf(tBuf, sizeof(tBuf), "PCB: %.0f\u00B0C", m_pcbTemp);
        } else {
            std::snprintf(tBuf, sizeof(tBuf), "-- \u00B0C");
        }
        ren.drawText(tBuf, {rx, infoY}, m_smallFont, primaryText, valueScale);

        float maxT = -1.f;
        if (m_hasSocTemp) maxT = std::max(maxT, m_socTemp);
        if (m_hasPcbTemp) maxT = std::max(maxT, m_pcbTemp);
        nxui::Vec2 tempSz = m_smallFont->measure(tBuf);
        std::string badgeText = (maxT >= 0.f && maxT > 65.f)
            ? i18n.tr("quicksettings.temp_warm", "Warm")
            : i18n.tr("quicksettings.temp_optimal", "Optimal");
        nxui::Color badgeCol = (maxT >= 0.f && maxT > 65.f)
            ? nxui::Color(0.95f, 0.75f, 0.20f, alpha)
            : nxui::Color(0.20f, 0.85f, 0.50f, alpha);
        ren.drawText(badgeText, {rx + tempSz.x * valueScale + 8.f, infoY},
                     m_smallFont, badgeCol, 0.72f);
    }

    auto drawSlider = [&](ItemIndex idx, const std::string& label, float value,
                          const nxui::Color& fillColor) {
        nxui::Rect card = computeItemRect(idx);
        ren.drawRoundedRect(card, cardFill, 12.f);
        ren.drawRoundedRectOutline(card, neutralBorder, 12.f, 1.f);

        if (m_smallFont) {
            ren.drawText(label, {card.x + 12.f, card.y + 8.f}, m_smallFont,
                         primaryText, kSliderLabelScale);

            char pBuf[16];
            std::snprintf(pBuf, sizeof(pBuf), "%d%%", static_cast<int>(std::round(value * 100.f)));
            nxui::Vec2 psz = m_smallFont->measure(pBuf);
            ren.drawText(pBuf, {card.x + card.width - 12.f - psz.x * kSliderLabelScale, card.y + 8.f},
                         m_smallFont, secondaryText, kSliderLabelScale);
        }

        nxui::Rect track = computeSliderTrackRect(idx);
        ren.drawRoundedRect(track, lightMode
            ? nxui::Color(0.12f, 0.14f, 0.18f, 0.18f * alpha)
            : nxui::Color(0.05f, 0.08f, 0.12f, 0.75f * alpha), 7.f);

        float fillW = std::clamp(track.width * value, 10.f, track.width);
        nxui::Rect fillRect = {track.x, track.y, fillW, track.height};
        ren.drawRoundedRect(fillRect, fillColor.withAlpha(alpha), 7.f);

        float knobX = track.x + fillW;
        ren.drawCircle({knobX, track.y + track.height * 0.5f}, 9.5f,
                       nxui::Color(1.f, 1.f, 1.f, alpha));
    };

    {
        nxui::Rect musicCard = computeMusicCardRect();
        ren.drawRoundedRect(musicCard, cardFill, 12.f);
        ren.drawRoundedRectOutline(musicCard, neutralBorder, 12.f, 1.f);

        if (m_smallFont) {
            std::string np = m_music.nowPlaying.empty()
                ? i18n.tr("quicksettings.music_idle", "Not playing")
                : m_music.nowPlaying;
            np = ellipsize(m_smallFont, np, musicCard.width - 24.f, 0.78f);
            ren.drawText(np, {musicCard.x + 12.f, musicCard.y + 8.f},
                         m_smallFont, primaryText, 0.78f);
        }

        for (int i = 0; i < static_cast<int>(MusicControl::Count); ++i) {
            auto ctrl = static_cast<MusicControl>(i);
            nxui::Rect btn = computeMusicControlRect(ctrl);
            const bool focused = (m_selectedItem == ItemIndex::MusicTransport
                && m_selectedMusicControl == ctrl
                && (!m_playlistOpen || m_playlistFocusParked));
            const bool activeMode =
                (ctrl == MusicControl::Shuffle && m_music.shuffle)
                || (ctrl == MusicControl::Repeat && m_music.repeat != MusicRepeatMode::Off)
                || (ctrl == MusicControl::Playlist && m_playlistOpen)
                || (ctrl == MusicControl::PlayPause && m_music.playing);

            nxui::Color glassBase = lightMode
                ? nxui::Color(0.98f, 0.99f, 1.f, 0.78f)
                : (m_theme
                    ? m_theme->panelHighlight.withAlpha(0.14f)
                    : nxui::Color(1.f, 1.f, 1.f, 0.12f));
            if (activeMode && !focused) {
                glassBase = cursorColor.withAlpha((lightMode ? 0.18f : 0.22f));
            }
            nxui::Color glassBorder = focused
                ? cursorColor.withAlpha(0.95f * alpha)
                : (lightMode
                    ? nxui::Color(0.08f, 0.10f, 0.14f, 0.16f * alpha)
                    : nxui::Color(1.f, 1.f, 1.f, 0.22f * alpha));
            nxui::Color glassHighlight = lightMode
                ? nxui::Color(1.f, 1.f, 1.f, 0.55f * alpha)
                : (m_theme
                    ? m_theme->panelHighlight.withAlpha(0.18f * alpha)
                    : nxui::Color(1.f, 1.f, 1.f, 0.16f * alpha));

            ren.drawFrostedInset(btn, glassBase.withAlpha(glassBase.a * alpha),
                                 glassBorder, glassHighlight, 12.f, 1.f);

            if (focused) {
                ren.drawRoundedRectOutline(btn, cursorColor.withAlpha(0.9f * alpha), 12.f, 1.6f);
            }

            nxui::Color iconCol = focused ? primaryText
                : (activeMode ? primaryText : mutedText);
            if (ctrl == MusicControl::Shuffle && m_music.shuffle)
                iconCol = cursorColor;
            if (ctrl == MusicControl::Repeat && m_music.repeat != MusicRepeatMode::Off)
                iconCol = cursorColor;
            drawMusicControlIcon(ren, ctrl, m_music, btn, iconCol.withAlpha(alpha));
        }

        // Progress / scrub bar between transport and volume.
        {
            nxui::Rect track = computeProgressTrackRect();
            const bool focused = (m_selectedItem == ItemIndex::MusicProgress
                && (!m_playlistOpen || m_playlistFocusParked));
            const float radius = track.height * 0.5f;

            if (m_smallFont) {
                std::string left = formatTrackTime(m_music.positionSeconds);
                std::string right = m_music.durationSeconds > 0.05f
                    ? formatTrackTime(m_music.durationSeconds)
                    : "--:--";
                // Same side inset + dim as Music (BGM) title / percent.
                const float labelY = musicCard.y + kMusicProgressLabelY;
                const float leftX = musicCard.x + kMusicCardLabelSidePad;
                ren.drawText(left, {leftX, labelY}, m_smallFont, secondaryText, kSliderLabelScale);
                nxui::Vec2 rsz = m_smallFont->measure(right);
                ren.drawText(right,
                             {musicCard.x + musicCard.width - kMusicCardLabelSidePad
                                  - rsz.x * kSliderLabelScale,
                              labelY},
                             m_smallFont, secondaryText, kSliderLabelScale);
            }

            ren.drawRoundedRect(track, lightMode
                ? nxui::Color(0.12f, 0.14f, 0.18f, 0.18f * alpha)
                : nxui::Color(0.05f, 0.08f, 0.12f, 0.75f * alpha), radius);
            float progress = 0.f;
            if (m_music.durationSeconds > 0.05f)
                progress = std::clamp(m_music.positionSeconds / m_music.durationSeconds, 0.f, 1.f);
            float fillW = std::max(progress > 0.f ? 8.f : 0.f, track.width * progress);
            if (fillW > 0.f) {
                ren.drawRoundedRect({track.x, track.y, fillW, track.height},
                                   cursorColor.withAlpha(0.85f * alpha), radius);
            }
            if (m_music.durationSeconds > 0.05f) {
                ren.drawCircle({track.x + fillW, track.y + track.height * 0.5f},
                               focused ? 10.5f : 9.5f,
                               nxui::Color(1.f, 1.f, 1.f, alpha));
            }
            if (focused)
                ren.drawRoundedRectOutline(
                    {track.x - 4.f, track.y - 6.f, track.width + 8.f, track.height + 12.f},
                    cursorColor.withAlpha(0.7f * alpha), 8.f, 1.4f);
        }
    }

    drawSlider(ItemIndex::BgmVolume, i18n.tr("quicksettings.bgm_volume", "Music (BGM)"),
               m_bgmVolume, nxui::Color(0.68f, 0.45f, 0.95f, 1.f));

    drawSlider(ItemIndex::SfxVolume, i18n.tr("quicksettings.sfx_volume", "Sound Effects"),
               m_sfxVolume, nxui::Color(0.18f, 0.82f, 0.55f, 1.f));

    drawSlider(ItemIndex::Brightness, i18n.tr("quicksettings.brightness", "Brightness"),
               m_brightness, nxui::Color(0.15f, 0.75f, 0.98f, 1.f));

    auto drawToggle = [&](ItemIndex idx, const std::string& label, bool enabled) {
        nxui::Rect card = computeItemRect(idx);
        ren.drawRoundedRect(card, cardFill, 12.f);
        ren.drawRoundedRectOutline(card, neutralBorder, 12.f, 1.f);

        float textX = card.x + 14.f;
        if (idx == ItemIndex::Wifi) {
            drawWifiIcon(ren, {card.x + 24.f, card.y + card.height * 0.5f + 3.f},
                         primaryText, 0.95f);
            textX = card.x + 40.f;
        }

        if (m_smallFont) {
            float ty = card.y + (card.height - m_smallFont->measure(label).y * 0.82f) * 0.5f;
            ren.drawText(label, {textX, ty}, m_smallFont,
                         primaryText, 0.82f);
        }

        float pillW = 56.f;
        float pillH = 28.f;
        nxui::Rect pill = {card.x + card.width - pillW - 12.f, card.y + (card.height - pillH) * 0.5f, pillW, pillH};
        nxui::Color pillBg = enabled
            ? nxui::Color(0.20f, 0.82f, 0.45f, 0.90f * alpha)
            : nxui::Color(0.35f, 0.38f, 0.45f, 0.75f * alpha);

        ren.drawRoundedRect(pill, pillBg, pillH * 0.5f);

        float knobX = enabled ? (pill.x + pill.width - pillH * 0.5f) : (pill.x + pillH * 0.5f);
        ren.drawCircle({knobX, pill.y + pillH * 0.5f}, pillH * 0.40f,
                       nxui::Color(1.f, 1.f, 1.f, alpha));

        if (m_smallFont) {
            std::string stateStr = enabled
                ? i18n.tr("quicksettings.on", "ON")
                : i18n.tr("quicksettings.off", "OFF");
            nxui::Vec2 ssz = m_smallFont->measure(stateStr);
            float stx = enabled ? (pill.x + 8.f) : (pill.x + pill.width - ssz.x * 0.65f - 8.f);
            ren.drawText(stateStr, {stx, pill.y + (pillH - ssz.y * 0.65f) * 0.5f}, m_smallFont,
                         lightMode
                            ? nxui::Color(0.04f, 0.05f, 0.07f, 0.95f * alpha)
                            : nxui::Color(1.f, 1.f, 1.f, 0.95f * alpha),
                         0.65f);
        }
    };

    drawToggle(ItemIndex::AirplaneMode, i18n.tr("quicksettings.airplane_mode", "Airplane Mode"),
               m_airplaneMode);

    drawToggle(ItemIndex::Wifi, i18n.tr("quicksettings.wifi", "Wi-Fi"),
               m_wifiEnabled);

    auto drawPowerBtn = [&](PowerAction pa, const std::string& label) {
        nxui::Rect btn = computePowerButtonRect(pa);
        bool focused = (m_selectedItem == ItemIndex::PowerActions && m_selectedPower == pa);

        nxui::Color fill = focused
            ? (lightMode
                ? nxui::Color(0.08f, 0.10f, 0.14f, 0.08f * alpha)
                : nxui::Color(1.f, 1.f, 1.f, 0.15f * alpha))
            : cardFill;
        nxui::Color border = focused
            ? cursorColor.withAlpha(0.95f * alpha)
            : neutralBorder;

        ren.drawRoundedRect(btn, fill, 14.f);
        ren.drawRoundedRectOutline(btn, border, 14.f, focused ? 1.4f : 1.0f);

        ren.drawLine({btn.x + 10.f, btn.y + 1.f}, {btn.x + btn.width - 10.f, btn.y + 1.f},
                     lightMode
                        ? nxui::Color(1.f, 1.f, 1.f, 0.58f * alpha)
                        : nxui::Color(1.f, 1.f, 1.f, 0.16f * alpha), 1.f);

        if (m_smallFont) {
            nxui::Vec2 lsz = m_smallFont->measure(label);
            nxui::Color textColor = focused ? primaryText : secondaryText;
            // Shared metrics so Sleep / Reboot / Power Off read as one family.
            const float textScale = kPowerTextScale;
            const float iconGap = kPowerIconGap;

            if (pa == PowerAction::Sleep) {
                const float iconSlot = 18.f;
                float totalW = iconSlot + iconGap + lsz.x * textScale;
                float curXBtn = btn.x + (btn.width - totalW) * 0.5f;
                drawMoonIcon(ren, {curXBtn + iconSlot * 0.5f, btn.y + btn.height * 0.5f},
                              textColor, kPowerMoonScale);
                float ty = btn.y + (btn.height - lsz.y * textScale) * 0.5f;
                ren.drawText(label, {curXBtn + iconSlot + iconGap, ty}, m_smallFont,
                             textColor, textScale);
            } else {
                std::string iconGlyph = (pa == PowerAction::Reboot)
                    ? utf8Codepoint(0xE08F) : utf8Codepoint(0xE0B8);
                nxui::Font* fIcon = m_iconFont ? m_iconFont : m_smallFont;
                nxui::Vec2 isz = fIcon ? fIcon->measure(iconGlyph) : nxui::Vec2{18.f, 18.f};
                float actualIconW = isz.x * kPowerIconScale;
                float totalW = actualIconW + iconGap + lsz.x * textScale;
                float curXBtn = btn.x + (btn.width - totalW) * 0.5f;

                if (fIcon) {
                    float iy = btn.y + (btn.height - isz.y * kPowerIconScale) * 0.5f;
                    ren.drawText(iconGlyph, {curXBtn, iy}, fIcon, textColor, kPowerIconScale);
                }
                float ty = btn.y + (btn.height - lsz.y * textScale) * 0.5f;
                ren.drawText(label, {curXBtn + actualIconW + iconGap, ty}, m_smallFont,
                             textColor, textScale);
            }
        }
    };

    drawPowerBtn(PowerAction::Sleep, i18n.tr("quicksettings.sleep", "Sleep"));
    drawPowerBtn(PowerAction::Reboot, i18n.tr("quicksettings.reboot", "Reboot"));
    drawPowerBtn(PowerAction::Shutdown, i18n.tr("quicksettings.power_off", "Power Off"));

    // Clip playlist-row cursor to the list viewport so it cannot paint over
    // cover art or below the playlist panel during touch scroll/reorder.
    const bool clipPlaylistCursor = m_playlistOpen && !m_playlistFocusParked
        && !m_playlistEyeFocused && !m_playlistArtFocused
        && !m_coverFullscreen && m_playlistAnim > 0.01f
        && !m_music.playlistTitles.empty();
    if (clipPlaylistCursor)
        ren.pushClipRect(computePlaylistListRect());
    if (!m_coverFullscreen)
        m_cursor.render(ren);
    if (clipPlaylistCursor)
        ren.popClipRect();

    if (m_coverFullscreen && m_coverTex.valid()) {
        const float sw = static_cast<float>(ren.width());
        const float sh = static_cast<float>(ren.height());
        const float tw = static_cast<float>(std::max(1, m_coverTex.width()));
        const float th = static_cast<float>(std::max(1, m_coverTex.height()));
        const bool canBlur = ren.gpu().offscreenReady();
        constexpr int kCoverFsBlurTarget = 2; // shared leave-frame slot; unused while cover FS is up

        // Bandcamp-style: blur cover once into offscreen cache, then reuse every frame.
        if (canBlur) {
            if (!m_coverFsBlurCached) {
                const float fillScale = std::max(sw / tw, sh / th);
                const float fw = tw * fillScale;
                const float fh = th * fillScale;
                ren.drawTexture(&m_coverTex,
                                {(sw - fw) * 0.5f, (sh - fh) * 0.5f, fw, fh},
                                nxui::Color(1.f, 1.f, 1.f, 1.f));
                ren.captureToOffscreen(false);
                ren.applyBlur(2.2f, 5);
                ren.copyOffscreen(0, kCoverFsBlurTarget);
                m_coverFsBlurCached = true;
            }
            ren.drawOffscreen(kCoverFsBlurTarget, {0.f, 0.f, sw, sh},
                              nxui::Color(1.f, 1.f, 1.f, alpha));
            ren.drawRect({0.f, 0.f, sw, sh},
                         nxui::Color(0.f, 0.f, 0.f, 0.22f * alpha));
        } else {
            ren.drawRect({0.f, 0.f, sw, sh},
                         nxui::Color(0.f, 0.f, 0.f, 0.72f * alpha));
        }

        // Sharp cover: fit to screen (square/portrait = side letterbox).
        {
            const float fitScale = std::min(sw / tw, sh / th);
            const float dw = tw * fitScale;
            const float dh = th * fitScale;
            ren.drawTexture(&m_coverTex,
                            {(sw - dw) * 0.5f, (sh - dh) * 0.5f, dw, dh},
                            nxui::Color(1.f, 1.f, 1.f, alpha));
        }

        // Fullscreen playlist sheet — solid slide (no per-frame blur; that was the hitch).
        if (m_coverFsPlaylistAnim > 0.001f) {
            constexpr float kFsPlRadius = 18.f;
            nxui::Rect fsPl = computeCoverFsPlaylistRect();
            nxui::Rect fsCard = computeCoverFsCardRect();
            // Only paint the portion emerging above the player card.
            const float visibleH = std::max(0.f, fsCard.y + 18.f - fsPl.y);
            if (visibleH > 2.f) {
            ren.pushClipRect({fsPl.x, fsPl.y, fsPl.width, visibleH});

            nxui::Color plFill = lightMode
                ? nxui::Color(0.94f, 0.95f, 0.98f, 0.94f * alpha)
                : (m_theme
                    ? m_theme->panelBase.withAlpha(0.90f * alpha)
                    : nxui::Color(0.07f, 0.09f, 0.13f, 0.92f * alpha));
            ren.drawRoundedRect(fsPl, plFill, kFsPlRadius);
            ren.drawRoundedRectOutline(fsPl,
                lightMode
                    ? nxui::Color(1.f, 1.f, 1.f, 0.50f * alpha)
                    : nxui::Color(1.f, 1.f, 1.f, 0.30f * alpha),
                kFsPlRadius, 1.2f);

            if (m_smallFont) {
                std::string plTitle = i18n.tr("quicksettings.playlist", "Playlist");
                ren.drawText(plTitle, {fsPl.x + 16.f, fsPl.y + 12.f},
                             m_smallFont, primaryText, 0.78f);
            }

            const int count = playlistVisibleCount();
            nxui::Rect list = computeCoverFsPlaylistListRect();
            if (count == 0) {
                if (m_smallFont) {
                    ren.drawText(i18n.tr("quicksettings.playlist_empty", "No tracks"),
                                 {list.x + 4.f, list.y + 8.f}, m_smallFont, mutedText, 0.78f);
                }
            } else {
                ren.pushClipRect(list);
                for (int i = 0; i < count; ++i) {
                    nxui::Rect rowRect = computePlaylistRowRect(i);
                    if (rowRect.y + rowRect.height < list.y || rowRect.y > list.y + list.height)
                        continue;
                    if (rowRect.y > fsCard.y)
                        continue;
                    if (i >= static_cast<int>(m_playlistRows.size()))
                        continue;
                    const auto& prow = m_playlistRows[static_cast<size_t>(i)];
                    const bool focused = m_coverFsPlaylistFocused && i == m_playlistFocus;
                    const bool current = prow.kind == PlaylistRowKind::Track
                        && prow.trackIndex == m_music.currentIndex;
                    const bool folderHasCurrent = prow.kind == PlaylistRowKind::Folder
                        && m_music.currentIndex >= 0
                        && m_music.currentIndex < static_cast<int>(m_music.playlistAlbumFolders.size())
                        && m_music.playlistAlbumFolders[static_cast<size_t>(m_music.currentIndex)] == prow.folder;
                    if (focused || current || folderHasCurrent) {
                        ren.drawRoundedRect(rowRect,
                            focused ? cursorColor.withAlpha(0.22f * alpha) : cardFill, 10.f);
                    }
                    if (focused)
                        ren.drawRoundedRectOutline(rowRect, cursorColor.withAlpha(0.85f * alpha), 10.f, 1.4f);

                    float contentX = rowRect.x + (prow.indented ? kPlaylistTreeIndent : 0.f) + 4.f;
                    nxui::Rect thumb = {
                        contentX,
                        rowRect.y + (rowRect.height - kPlaylistThumbSize) * 0.5f,
                        kPlaylistThumbSize,
                        kPlaylistThumbSize
                    };
                    const std::vector<uint8_t>* artBytes = nullptr;
                    std::string artKey;
                    if (prow.kind == PlaylistRowKind::Folder) {
                        artKey = "folder:" + prow.folder;
                        if (m_callbacks.onMusicQueryFolderCover)
                            artBytes = m_callbacks.onMusicQueryFolderCover(prow.folder);
                    } else {
                        artKey = "track:" + std::to_string(prow.trackIndex);
                        if (m_callbacks.onMusicQueryTrackCover)
                            artBytes = m_callbacks.onMusicQueryTrackCover(prow.trackIndex);
                        if ((!artBytes || artBytes->empty()) && !prow.folder.empty()
                            && m_callbacks.onMusicQueryFolderCover)
                            artBytes = m_callbacks.onMusicQueryFolderCover(prow.folder);
                    }
                    const nxui::Texture* thumbTex = playlistThumbTexture(ren, artKey, artBytes);
                    ren.drawRoundedRect(thumb, cardFill, 6.f);
                    if (thumbTex)
                        ren.drawTextureRounded(thumbTex, thumb, 6.f,
                                               nxui::Color(1.f, 1.f, 1.f, alpha));

                    std::string label = prow.label;
                    if (prow.kind == PlaylistRowKind::Folder) {
                        const bool open = m_playlistExpanded.count(prow.folder) > 0;
                        label = std::string(open ? "▼ " : "▶ ") + prow.folder
                            + "  (" + std::to_string(prow.trackCount) + ")";
                    }

                    nxui::Color textCol = (current || focused || folderHasCurrent)
                        ? primaryText : secondaryText;
                    const float textScale = 0.78f;
                    const float textX = thumb.x + thumb.width + kPlaylistThumbGap;
                    const float textMaxW = rowRect.x + rowRect.width - textX - 8.f;
                    if (m_smallFont) {
                        nxui::Vec2 fullSz = m_smallFont->measure(label);
                        float ty = rowRect.y + (rowRect.height - fullSz.y * textScale) * 0.5f;
                        std::string shown = ellipsize(m_smallFont, label, textMaxW, textScale);
                        if (focused && fullSz.x * textScale > textMaxW + 1.f
                            && m_playlistMarqueeHold <= 0.f) {
                            ren.pushClipRect({textX, rowRect.y, textMaxW, rowRect.height});
                            ren.drawText(label, {textX - m_playlistMarqueeOffset, ty},
                                         m_smallFont, textCol, textScale);
                            ren.drawText(label,
                                         {textX - m_playlistMarqueeOffset + fullSz.x * textScale
                                              + kMarqueeGapPx, ty},
                                         m_smallFont, textCol, textScale);
                            ren.popClipRect();
                        } else {
                            ren.drawText(shown, {textX, ty}, m_smallFont, textCol, textScale);
                        }
                    }
                }
                ren.popClipRect();
            }
            ren.popClipRect();
            } // visibleH
        }

        // Frosted player card — sample the cached cover blur (no per-frame re-blur).
        constexpr float kFsCardRadius = 18.f;
        nxui::Rect fsCard = computeCoverFsCardRect();
        if (canBlur && m_coverFsBlurCached) {
            ren.drawOffscreenRounded(kCoverFsBlurTarget, fsCard, kFsCardRadius,
                                     nxui::Color(1.f, 1.f, 1.f, alpha));
        }
        nxui::Color fsFill = lightMode
            ? nxui::Color(0.97f, 0.98f, 1.f, 0.52f * alpha)
            : (m_theme
                ? m_theme->panelBase.withAlpha(0.50f * alpha)
                : nxui::Color(0.08f, 0.10f, 0.14f, 0.52f * alpha));
        ren.drawRoundedRect(fsCard, fsFill, kFsCardRadius);
        ren.drawRoundedRectOutline(fsCard,
            lightMode
                ? nxui::Color(1.f, 1.f, 1.f, 0.42f * alpha)
                : nxui::Color(1.f, 1.f, 1.f, 0.28f * alpha),
            kFsCardRadius, 1.2f);
        ren.drawRoundedRectOutline(fsCard.shrunk(1.f),
            nxui::Color(1.f, 1.f, 1.f, 0.12f * alpha),
            std::max(0.f, kFsCardRadius - 1.f), 1.f);

        if (m_font || m_smallFont) {
            nxui::Font* titleFont = m_font ? m_font : m_smallFont;
            const float titleScale = m_font ? 0.78f : kCoverFsTitleScale;
            std::string np = m_music.nowPlaying.empty()
                ? i18n.tr("quicksettings.music_idle", "Not playing")
                : m_music.nowPlaying;
            np = ellipsize(titleFont, np, fsCard.width - 36.f, titleScale);
            nxui::Vec2 tsz = titleFont->measure(np);
            ren.drawText(np,
                         {fsCard.x + (fsCard.width - tsz.x * titleScale) * 0.5f,
                          fsCard.y + 11.f},
                         titleFont, primaryText, titleScale);
        }

        for (int i = 0; i < kCoverFsControlCount; ++i) {
            auto ctrl = static_cast<MusicControl>(i);
            nxui::Rect btn = computeCoverFsControlRect(ctrl);
            const bool focused = (m_selectedItem == ItemIndex::MusicTransport
                && m_selectedMusicControl == ctrl
                && !m_coverFsPlaylistFocused);
            const bool activeMode =
                (ctrl == MusicControl::Shuffle && m_music.shuffle)
                || (ctrl == MusicControl::Repeat && m_music.repeat != MusicRepeatMode::Off)
                || (ctrl == MusicControl::Playlist && m_coverFsPlaylistOpen)
                || (ctrl == MusicControl::PlayPause && m_music.playing);

            nxui::Color glassBase = lightMode
                ? nxui::Color(1.f, 1.f, 1.f, 0.42f)
                : nxui::Color(1.f, 1.f, 1.f, 0.16f);
            if (activeMode && !focused)
                glassBase = cursorColor.withAlpha(lightMode ? 0.28f : 0.32f);
            nxui::Color glassBorder = focused
                ? cursorColor.withAlpha(0.95f * alpha)
                : nxui::Color(1.f, 1.f, 1.f, (lightMode ? 0.32f : 0.24f) * alpha);
            nxui::Color glassHighlight = nxui::Color(1.f, 1.f, 1.f, 0.32f * alpha);

            ren.drawFrostedInset(btn, glassBase.withAlpha(glassBase.a * alpha),
                                 glassBorder, glassHighlight, 12.f, 1.f);
            if (focused)
                ren.drawRoundedRectOutline(btn, cursorColor.withAlpha(0.9f * alpha), 12.f, 1.6f);

            nxui::Color iconCol = focused ? primaryText
                : (activeMode ? primaryText : mutedText);
            if (ctrl == MusicControl::Shuffle && m_music.shuffle)
                iconCol = cursorColor;
            if (ctrl == MusicControl::Repeat && m_music.repeat != MusicRepeatMode::Off)
                iconCol = cursorColor;
            drawMusicControlIcon(ren, ctrl, m_music, btn, iconCol.withAlpha(alpha));
        }

        {
            nxui::Rect track = computeCoverFsProgressTrackRect();
            const bool focused = (m_selectedItem == ItemIndex::MusicProgress
                && !m_coverFsPlaylistFocused);
            const float radius = track.height * 0.5f;

            if (m_smallFont) {
                std::string left = formatTrackTime(m_music.positionSeconds);
                std::string right = m_music.durationSeconds > 0.05f
                    ? formatTrackTime(m_music.durationSeconds)
                    : "--:--";
                const float labelY = fsCard.y + kCoverFsProgressLabelY;
                ren.drawText(left, {fsCard.x + 18.f, labelY},
                             m_smallFont, secondaryText, kSliderLabelScale);
                nxui::Vec2 rsz = m_smallFont->measure(right);
                ren.drawText(right,
                             {fsCard.x + fsCard.width - 18.f - rsz.x * kSliderLabelScale, labelY},
                             m_smallFont, secondaryText, kSliderLabelScale);
            }

            ren.drawRoundedRect(track,
                nxui::Color(0.f, 0.f, 0.f, (lightMode ? 0.28f : 0.50f) * alpha), radius);
            float progress = 0.f;
            if (m_music.durationSeconds > 0.05f)
                progress = std::clamp(m_music.positionSeconds / m_music.durationSeconds, 0.f, 1.f);
            float fillW = std::max(progress > 0.f ? 8.f : 0.f, track.width * progress);
            if (fillW > 0.f) {
                ren.drawRoundedRect({track.x, track.y, fillW, track.height},
                                   cursorColor.withAlpha(0.90f * alpha), radius);
            }
            if (m_music.durationSeconds > 0.05f) {
                ren.drawCircle({track.x + fillW, track.y + track.height * 0.5f},
                               focused ? 10.5f : 9.5f,
                               nxui::Color(1.f, 1.f, 1.f, alpha));
            }
            if (focused) {
                ren.drawRoundedRectOutline(
                    {track.x - 4.f, track.y - 6.f, track.width + 8.f, track.height + 12.f},
                    cursorColor.withAlpha(0.7f * alpha), 8.f, 1.4f);
            }
        }
    }
}
