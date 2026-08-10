#pragma once
#include <SDL2/SDL_mixer.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

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

class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();

    bool initialize();
    void shutdown();

    void loadTrack(const std::string& path);
    void clearTracks();
    void play();
    void stop();
    void nextTrack();
    void setVolume(float vol);
    float volume() const { return m_volume; }
    bool  isPlaying() const { return m_playing; }

    // Transient attenuation applied on top of the configured volume, e.g. to fade
    // the music out during the launch animation. Kept separate from m_volume so
    // volume() keeps reporting the user's setting: it seeds the Theme Shop slider,
    // which writes back into the persisted config.
    void setMusicFade(float fade);
    float musicFade() const { return m_musicFade; }

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

    std::mutex m_trackMutex;
    std::vector<Mix_Music*> m_tracks;
    int   m_current = 0;
    float m_volume  = 0.5f;
    float m_musicFade = 1.f;
    std::atomic<bool> m_playing{false};
    bool  m_initialized = false;

    std::mutex m_sfxMutex;
    std::unordered_map<int, Mix_Chunk*> m_sfx;
    std::unordered_map<std::string, Mix_Chunk*> m_namedSfx;
    std::unordered_map<std::string, float> m_namedSfxVolumeScales;
    float m_sfxVolume = 0.7f;
};
