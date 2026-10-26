#pragma once
#include <SDL2/SDL_mixer.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>

enum class Sfx {
    Navigate,
    Activate,
    PageChange,
    ModalShow,
    ModalHide,
    LaunchGame,
    ThemeToggle,
    ToggleOff,
    SliderUp,
    SliderDown,
    ConfirmPositive,
    Volume,
};

enum class MusicRepeatMode {
    Off = 0,
    All = 1,
    One = 2,
};

struct MusicTrackInfo {
    std::string path;
    std::string filename;
    std::string relativeKey; // "track.mp3" or "Album/track.mp3" — stable playlist id
    std::string albumFolder; // empty = loose track at music root
    std::string title;
    std::string artist;
    float durationSeconds = 0.f;
    // True when duration comes from Xing/Info frames, ID3 TLEN, or a
    // non-MP3 container. False for bitrate-guessed MP3 lengths — those are
    // unsafe inputs for Mix_SetMusicPosition on Switch.
    bool seekReliable = false;
    std::vector<uint8_t> coverArt; // JPEG/PNG bytes from ID3 APIC when present

    std::string displayTitle() const {
        if (!title.empty() && !artist.empty())
            return artist + " - " + title;
        if (!title.empty())
            return title;
        return filename;
    }
};

class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();

    bool initialize();
    void shutdown();

    bool loadTrack(const std::string& path, const std::string& albumFolder = {});
    void clearTracks();
    void applyPlaylistOrder(const std::vector<std::string>& orderedKeys);
    void setFolderCover(const std::string& albumFolder, std::vector<uint8_t> bytes);
    const std::vector<MusicTrackInfo>& tracks() const { return m_trackInfos; }
    int trackCount() const { return static_cast<int>(m_trackInfos.size()); }
    int currentIndex() const { return m_current; }
    std::string currentDisplayTitle() const;
    std::vector<std::string> playlistFilenames() const;
    std::vector<std::string> playlistOrderKeys() const;
    bool hasAlbumFolders() const;
    const std::vector<uint8_t>* trackCoverArt(int index) const;
    const std::vector<uint8_t>* folderCoverArt(const std::string& albumFolder) const;

    void play();
    void pause();
    void stop();
    void togglePlayPause();
    void nextTrack();
    void prevTrack();
    void playTrackAt(int index);
    bool moveTrack(int from, int to);

    void setVolume(float vol);
    float volume() const { return m_volume; }
    bool  isPlaying() const { return m_playing.load(); }
    bool  isPaused() const { return m_paused; }

    void setMusicFade(float fade);
    float musicFade() const { return m_musicFade; }

    void setShuffle(bool enabled);
    bool shuffle() const { return m_shuffle; }
    void setRepeatMode(MusicRepeatMode mode);
    MusicRepeatMode repeatMode() const { return m_repeat; }

    void update(float dt);
    float positionSeconds() const { return m_positionSeconds; }
    float durationSeconds() const;
    void setPositionSeconds(float seconds);
    void seekTo(float seconds);
    const std::vector<uint8_t>& currentCoverArt() const;

    /// Resume by stable track key when possible. If the saved track is gone
    /// (user deleted/renamed files), starts from the first remaining track at 0
    /// instead of seeking into a different file at the old index/position —
    /// Mix_SetMusicPosition into the wrong MP3 can hang SDL_mixer on Switch.
    void restorePlaybackState(int trackIndex,
                              float positionSeconds,
                              bool playing,
                              const std::string& trackKey = {});
    /// Apply a deferred Mix_SetMusicPosition after HOME is interactive.
    /// No-ops while warm-up frames remain or nothing is pending.
    void updateDeferredSeek();
    bool hasDeferredSeek() const { return m_deferredSeekSeconds >= 0.f; }

    /// Current track's playlist key, or empty if none.
    std::string currentTrackKey() const;
    /// Drop stale playlist keys and rewrite index/key/position when the saved
    /// resume target is no longer on disk. Returns true if config fields changed.
    bool reconcileResumeConfig(int& trackIndex,
                               float& positionSeconds,
                               std::string& trackKey,
                               std::vector<std::string>& playlistOrder) const;

    void loadSfx(Sfx id, const std::string& path);
    void clearSfx();
    void playSfx(Sfx id);
    void loadNamedSfx(const std::string& id, const std::string& path, float volumeScale = 1.f);
    void playNamedSfx(const std::string& id);
    void setSfxVolume(float vol);
    float sfxVolume() const { return m_sfxVolume; }

private:
    static std::atomic<AudioManager*> s_instance;
    static void onTrackFinished();

    void applyMusicVolume();
    void playCurrentInternal(bool fromStart);
    void rebuildShuffleOrder(bool keepCurrent);
    int nextIndexInOrder(int from) const;
    int prevIndexInOrder(int from) const;
    int findTrackIndexByKeyUnlocked(const std::string& trackKey) const;
    float sanitizeSeekSecondsForDuration(float positionSeconds, float durationSeconds) const;
    float sanitizeSeekSecondsUnlocked(float positionSeconds) const;
    /// Resume-only sanitizer: drops unreliable / deep MP3 hard-seeks that can
    /// freeze SDL_mixer on Switch, while keeping shallow mid-track resume.
    float sanitizeResumeSeekUnlocked(float positionSeconds) const;
    bool currentTrackFileExistsUnlocked() const;
    bool currentTrackIsMp3Unlocked() const;
    static std::string cleanedFilenameTitle(const std::string& filename);
    static void readMp3Metadata(const std::string& path, MusicTrackInfo& info);
    static bool pathLooksLikeMp3(const std::string& path);
    /// Reject MP4/AAC (ftyp…) and other non-decodable-on-Switch containers that
    /// are often mislabeled .mp3 — Mix_PlayMusic can soft-lock the console.
    static bool audioContainerLooksSafe(const std::string& path);

    std::mutex m_trackMutex;
    std::vector<Mix_Music*> m_tracks;
    std::vector<MusicTrackInfo> m_trackInfos;
    std::unordered_map<std::string, std::vector<uint8_t>> m_folderCovers;
    std::vector<int> m_shuffleOrder;
    int   m_current = 0;
    float m_volume  = 0.5f;
    float m_musicFade = 1.f;
    float m_positionSeconds = 0.f;
    float m_deferredSeekSeconds = -1.f;
    int   m_deferredSeekWarmupFrames = 0;
    bool  m_restoreFadeIn = false;
    std::atomic<bool> m_playing{false};
    bool  m_paused = false;
    bool  m_shuffle = false;
    MusicRepeatMode m_repeat = MusicRepeatMode::All;
    bool  m_initialized = false;
    bool  m_suppressFinishedHook = false;

    std::mutex m_sfxMutex;
    std::unordered_map<int, Mix_Chunk*> m_sfx;
    std::unordered_map<std::string, Mix_Chunk*> m_namedSfx;
    std::unordered_map<std::string, float> m_namedSfxVolumeScales;
    float m_sfxVolume = 0.7f;
};
