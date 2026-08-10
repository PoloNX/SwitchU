#include "AudioManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "[Audio] SDL_Init(AUDIO) failed: %s\n", SDL_GetError());
        return false;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        std::fprintf(stderr, "[Audio] Mix_OpenAudio failed: %s\n", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(8);
    m_initialized = true;
    setVolume(m_volume);
    return true;
}

void AudioManager::shutdown() {
    if (!m_initialized) return;

    Mix_HookMusicFinished(nullptr);
    s_instance.store(nullptr);
    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    Mix_CloseAudio();
    m_initialized = false;
    m_playing.store(false);

    for (auto* m : m_tracks) Mix_FreeMusic(m);
    m_tracks.clear();
    for (auto& [id, chunk] : m_sfx) Mix_FreeChunk(chunk);
    m_sfx.clear();
    for (auto& [id, chunk] : m_namedSfx) Mix_FreeChunk(chunk);
    m_namedSfx.clear();
    m_namedSfxVolumeScales.clear();
}

void AudioManager::loadTrack(const std::string& path) {
    Mix_Music* music = Mix_LoadMUS(path.c_str());
    if (!music) {
        std::fprintf(stderr, "[Audio] Failed to load %s: %s\n", path.c_str(), Mix_GetError());
        return;
    }
    std::lock_guard<std::mutex> lk(m_trackMutex);
    m_tracks.push_back(music);
}

void AudioManager::clearTracks() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    m_playing.store(false);
    for (auto* m : m_tracks) Mix_FreeMusic(m);
    m_tracks.clear();
    m_current = 0;
}

std::atomic<AudioManager*> AudioManager::s_instance{nullptr};

void AudioManager::onTrackFinished() {
    AudioManager* inst = s_instance.load();
    if (inst) inst->nextTrack();
}

void AudioManager::play() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    s_instance.store(this);
    Mix_HookMusicFinished(onTrackFinished);
    Mix_PlayMusic(m_tracks[m_current], 1);
    m_playing.store(true);
}

void AudioManager::stop() {
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    m_playing.store(false);
}

void AudioManager::nextTrack() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    m_current = (m_current + 1) % (int)m_tracks.size();
    Mix_PlayMusic(m_tracks[m_current], 1);
    m_playing.store(true);
}

void AudioManager::applyMusicVolume() {
    Mix_VolumeMusic((int)(m_volume * m_musicFade * MIX_MAX_VOLUME));
}

void AudioManager::setVolume(float vol) {
    m_volume = vol;
    applyMusicVolume();
}

void AudioManager::setMusicFade(float fade) {
    m_musicFade = std::clamp(fade, 0.f, 1.f);
    applyMusicVolume();
}

void AudioManager::loadSfx(Sfx id, const std::string& path) {
    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if (!chunk) {
        std::fprintf(stderr, "[Audio] Failed to load SFX %s: %s\n", path.c_str(), Mix_GetError());
        return;
    }
    Mix_VolumeChunk(chunk, (int)(m_sfxVolume * MIX_MAX_VOLUME));
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_sfx.find(static_cast<int>(id));
    if (it != m_sfx.end()) {
        Mix_FreeChunk(it->second);
        it->second = chunk;
    } else {
        m_sfx[static_cast<int>(id)] = chunk;
    }
}

void AudioManager::clearSfx() {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    Mix_HaltChannel(-1);
    for (auto& [id, chunk] : m_sfx) Mix_FreeChunk(chunk);
    m_sfx.clear();
    for (auto& [id, chunk] : m_namedSfx) Mix_FreeChunk(chunk);
    m_namedSfx.clear();
    m_namedSfxVolumeScales.clear();
}

void AudioManager::playSfx(Sfx id) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_sfx.find(static_cast<int>(id));
    if (it != m_sfx.end()) {
        Mix_PlayChannel(-1, it->second, 0);
    }
}

void AudioManager::loadNamedSfx(const std::string& id, const std::string& path, float volumeScale) {
    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if (!chunk) {
        std::fprintf(stderr, "[Audio] Failed to load named SFX %s: %s\n", path.c_str(), Mix_GetError());
        return;
    }
    Mix_VolumeChunk(chunk, (int)(m_sfxVolume * volumeScale * MIX_MAX_VOLUME));
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_namedSfx.find(id);
    if (it != m_namedSfx.end()) {
        Mix_FreeChunk(it->second);
        it->second = chunk;
    } else {
        m_namedSfx[id] = chunk;
    }
    m_namedSfxVolumeScales[id] = volumeScale;
}

void AudioManager::playNamedSfx(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    auto it = m_namedSfx.find(id);
    if (it != m_namedSfx.end()) {
        Mix_PlayChannel(-1, it->second, 0);
    }
}

void AudioManager::setSfxVolume(float vol) {
    std::lock_guard<std::mutex> lk(m_sfxMutex);
    m_sfxVolume = vol;
    for (auto& [id, chunk] : m_sfx) {
        Mix_VolumeChunk(chunk, (int)(vol * MIX_MAX_VOLUME));
    }
    for (auto& [id, chunk] : m_namedSfx) {
        float scale = 1.f;
        auto it = m_namedSfxVolumeScales.find(id);
        if (it != m_namedSfxVolumeScales.end())
            scale = it->second;
        Mix_VolumeChunk(chunk, (int)(vol * scale * MIX_MAX_VOLUME));
    }
}
