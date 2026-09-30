#pragma once

#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Types.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/Theme.hpp>
#include "core/AudioManager.hpp"
#include "widgets/SelectionCursor.hpp"

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>

namespace nxui {
class Renderer;
}

class QuickSettingsOverlay : public nxui::Widget {
public:
    enum class ItemIndex {
        MusicTransport = 0,
        MusicProgress,
        BgmVolume,
        SfxVolume,
        Brightness,
        AirplaneMode,
        Wifi,
        PowerActions,
        Count
    };

    enum class MusicControl {
        Prev = 0,
        PlayPause,
        Next,
        Repeat,
        Shuffle,
        Playlist,
        Count
    };

    enum class PowerAction {
        Sleep = 0,
        Reboot,
        Shutdown,
        Count
    };

    struct MusicUiState {
        std::string nowPlaying;
        bool playing = false;
        bool shuffle = false;
        MusicRepeatMode repeat = MusicRepeatMode::All;
        float volume = 0.4f;
        float positionSeconds = 0.f;
        float durationSeconds = 0.f;
        int currentIndex = 0;
        bool hasAlbumFolders = false;
        std::vector<std::string> playlistTitles;
        std::vector<std::string> playlistFilenames;
        std::vector<std::string> playlistOrderKeys;
        std::vector<std::string> playlistAlbumFolders; // parallel to titles
    };

    struct Callbacks {
        std::function<void(float)> onBrightnessChanged;
        std::function<void(float)> onBgmVolumeChanged;
        std::function<void(float)> onSfxVolumeChanged;
        std::function<void(bool)>  onAirplaneModeToggled;
        std::function<void(bool)>  onWifiToggled;
        std::function<void()>      onSleepRequested;
        std::function<void()>      onRebootRequested;
        std::function<void()>      onShutdownRequested;
        std::function<void()>      onClose;
        std::function<void()>      onNavigateSfx;
        std::function<void()>      onActivateSfx;
        std::function<void()>      onToggleOffSfx;
        std::function<void()>      onMusicPlayPause;
        std::function<void()>      onMusicPrev;
        std::function<void()>      onMusicNext;
        std::function<void()>      onMusicToggleShuffle;
        std::function<void()>      onMusicCycleRepeat;
        std::function<void(int)>   onMusicSelectTrack;
        std::function<void(int, int)> onMusicMoveTrack;
        std::function<void(float)> onMusicSeek;
        std::function<MusicUiState()> onMusicQueryState;
        std::function<std::vector<uint8_t>()> onMusicQueryCoverArt;
        std::function<const std::vector<uint8_t>*(int)> onMusicQueryTrackCover;
        std::function<const std::vector<uint8_t>*(const std::string&)> onMusicQueryFolderCover;
    };

    QuickSettingsOverlay();
    ~QuickSettingsOverlay() override;

    void setFont(nxui::Font* f)      { m_font = f; }
    void setSmallFont(nxui::Font* f) { m_smallFont = f; }
    void setIconFont(nxui::Font* f)  { m_iconFont = f; }
    void setTheme(const nxui::Theme* t);
    void setInstantCursorMotion(bool instant) { m_cursor.setInstantMotion(instant); }
    void setInput(nxui::Input* input) { m_input = input; }
    void setCallbacks(const Callbacks& cb) { m_callbacks = cb; }

    void show();
    void hide();
    bool isActive() const { return m_active || m_animating; }
    bool isFullyVisible() const { return m_active && !m_animating; }
    bool isPlaylistOpen() const { return m_playlistOpen; }
    bool isPlaylistReorderMode() const { return m_playlistReorderMode; }
    bool isCoverFullscreen() const { return m_coverFullscreen; }
    bool hasAlbumFolders() const { return m_music.hasAlbumFolders; }

    void setBatteryStatus(int percent, bool charging);
    void setInitialValues(float brightness, float bgmVolume, float sfxVolume,
                          bool airplaneMode, bool wifiEnabled);
    void refreshMusicState();

    void handleTouch(nxui::Input& input);
    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    void setupNavigationActions();
    void refreshHardwareStatus();
    void adjustSlider(ItemIndex item, float delta, bool playSfx = true);
    void toggleItem(ItemIndex item);
    void triggerPowerAction(PowerAction action);
    void updateCursorTarget(bool instant = false);
    void activateMusicControl();
    void openPlaylist();
    void closePlaylist();
    void cycleMusicControl(int delta);
    void beginPlaylistReorder();
    void confirmPlaylistReorder();
    void cancelPlaylistReorder();
    void movePlaylistReorder(int delta);
    void ensurePlaylistFocusVisible();
    void centerPlaylistFocus();
    void syncPlaylistToPlayingTrack(bool center);
    void resetPlaylistMarquee();
    void updatePlaylistMarquee(float dt);
    void rebuildPlaylistRows();
    void syncFolderExpandToPlaying();
    void togglePlaylistFolder(const std::string& folder);
    void activatePlaylistRow();
    bool playlistReorderAllowed() const;
    int playlistVisibleCount() const;
    int playlistRowForTrack(int trackIndex) const;
    const nxui::Texture* playlistThumbTexture(nxui::Renderer& ren, const std::string& key,
                                              const std::vector<uint8_t>* bytes);
    void refreshCoverTexture(nxui::Renderer& ren);
    void seekFromProgressX(float x);
    void seekFromProgressX(float x, const nxui::Rect& track);
    void togglePlaylistArtVisible();
    void openCoverFullscreen();
    void closeCoverFullscreen();
    void cycleCoverFsControl(int delta);
    void activateCoverFsControl();
    void toggleCoverFsPlaylist();
    bool hasPlaylistCover() const;
    bool playlistArtFocusable() const;
    nxui::Rect computeProgressTrackRect() const;
    nxui::Rect computeCoverFsCardRect() const;
    nxui::Rect computeCoverFsControlRect(MusicControl control) const;
    nxui::Rect computeCoverFsProgressTrackRect() const;
    nxui::Rect computeCoverFsPlaylistRect() const;
    nxui::Rect computeCoverFsPlaylistListRect() const;
    nxui::Rect activePlaylistListRect() const;
    nxui::Rect computePlaylistArtRect() const;
    nxui::Rect computePlaylistEyeRect() const;
    float playlistHeaderHeight() const;
    float playlistArtHeight() const;
    nxui::Rect computePanelRect() const;
    nxui::Rect computePlaylistRect() const;
    nxui::Rect computeItemRect(ItemIndex item) const;
    nxui::Rect computePowerButtonRect(PowerAction action) const;
    nxui::Rect computeSliderTrackRect(ItemIndex item) const;
    nxui::Rect computeMusicControlRect(MusicControl control) const;
    nxui::Rect computeMusicCardRect() const;
    nxui::Rect computePlaylistRowRect(int index) const;
    nxui::Rect computePlaylistHandleRect(int index) const;
    nxui::Rect computePlaylistListRect() const;
    int playlistIndexAtPoint(float x, float y) const;
    float playlistMaxScroll() const;
    void syncMusicFromCallbacks();
    void ensureMusicControlIcons(nxui::Renderer& ren);
    const nxui::Texture* musicControlIcon(MusicControl control, bool playing) const;
    void drawMusicControlIcon(nxui::Renderer& ren, MusicControl control,
                              const MusicUiState& music, const nxui::Rect& btn,
                              const nxui::Color& color);

    nxui::Font*        m_font = nullptr;
    nxui::Font*        m_smallFont = nullptr;
    nxui::Font*        m_iconFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    nxui::Input*       m_input = nullptr;
    Callbacks          m_callbacks;

    bool  m_active = false;
    bool  m_animating = false;
    float m_animProgress = 0.f;

    ItemIndex   m_selectedItem = ItemIndex::MusicTransport;
    MusicControl m_selectedMusicControl = MusicControl::PlayPause;
    PowerAction m_selectedPower = PowerAction::Sleep;

    SelectionCursor m_cursor;

    float m_brightness = 0.5f;
    float m_bgmVolume = 0.5f;
    float m_sfxVolume = 0.5f;
    bool  m_airplaneMode = false;
    bool  m_wifiEnabled = true;

    MusicUiState m_music;

    enum class PlaylistRowKind { Folder, Track };
    struct PlaylistRow {
        PlaylistRowKind kind = PlaylistRowKind::Track;
        int trackIndex = -1;       // Track rows only
        int trackCount = 0;        // Folder rows: tracks in album
        std::string folder;        // album key (empty for root tracks)
        std::string label;
        bool indented = false;
    };

    struct PlaylistThumb {
        std::string key;
        nxui::Texture tex;
        uint32_t lastUse = 0;
    };

    bool  m_playlistOpen = false;
    bool  m_playlistFocusParked = false; // playlist stays open; focus is in QS panel
    bool  m_playlistEyeFocused = false;  // controller focus on cover eye toggle
    bool  m_playlistArtFocused = false;  // controller focus on cover art
    bool  m_playlistCenterPending = false;
    float m_playlistAnim = 0.f;
    int   m_playlistFocus = 0; // index into m_playlistRows
    float m_playlistScrollY = 0.f;
    int   m_playlistTouchIndex = -1;
    float m_playlistTouchStartY = 0.f;
    float m_playlistTouchStartScroll = 0.f;
    int   m_playlistDragIndex = -1;
    float m_playlistDragOffsetY = 0.f;
    bool  m_playlistDragging = false;
    bool  m_playlistScrolling = false;
    bool  m_playlistTouchOnHandle = false;
    bool  m_playlistReorderMode = false;
    int   m_playlistReorderOrigin = -1;
    int   m_playlistReorderCurrent = -1;
    int   m_playlistMarqueeIndex = -1;
    float m_playlistMarqueeOffset = 0.f;
    float m_playlistMarqueeHold = 0.f;
    std::vector<PlaylistRow> m_playlistRows;
    std::unordered_set<std::string> m_playlistExpanded;
    std::unordered_set<std::string> m_playlistUserExpanded;
    int   m_playlistExpandPlayingIndex = -2; // -2 = never synced
    std::vector<PlaylistThumb> m_playlistThumbs;
    uint32_t m_playlistThumbClock = 0;

    nxui::Texture m_coverTex;
    std::vector<uint8_t> m_coverBytes;
    int   m_coverTrackIndex = -1;
    bool  m_coverLoadAttempted = false;
    bool  m_playlistArtVisible = true;
    bool  m_coverFullscreen = false;
    bool  m_coverFullscreenPending = false;
    bool  m_coverFsPlaylistOpen = false;
    bool  m_coverFsPlaylistFocused = false;
    float m_coverFsPlaylistAnim = 0.f;
    bool  m_coverFsBlurCached = false; // offscreen slot 2 holds cover blur while valid
    bool  m_draggingProgress = false;

    // PNG transport icons (white+alpha masks — tinted per theme).
    nxui::Texture m_iconPrev;
    nxui::Texture m_iconNext;
    nxui::Texture m_iconPlay;
    nxui::Texture m_iconPause;
    nxui::Texture m_iconRepeat;
    nxui::Texture m_iconShuffle;
    nxui::Texture m_iconPlaylist;
    bool  m_musicIconsLoadAttempted = false;

    int   m_batteryPercent = -1;
    bool  m_batteryCharging = false;
    float m_socTemp = -1.f;
    float m_pcbTemp = -1.f;
    bool  m_hasSocTemp = false;
    bool  m_hasPcbTemp = false;
    float m_statusPollTimer = 0.f;

    bool      m_draggingSlider = false;
    ItemIndex m_draggedSlider = ItemIndex::Brightness;
};
