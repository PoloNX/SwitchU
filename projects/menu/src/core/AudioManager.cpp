#include "AudioManager.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

namespace {

std::string decodeId3Text(const char* data, int size) {
    if (!data || size <= 0)
        return {};
    int encoding = static_cast<unsigned char>(data[0]);
    const char* body = data + 1;
    int bodySize = size - 1;
    if (bodySize <= 0)
        return {};

    auto trim = [](std::string s) {
        while (!s.empty() && (s.back() == '\0' || s.back() == ' '))
            s.pop_back();
        size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\0'))
            ++start;
        return s.substr(start);
    };

    if (encoding == 0 || encoding == 3) {
        // ISO-8859-1 or UTF-8
        return trim(std::string(body, body + bodySize));
    }
    if (encoding == 1 || encoding == 2) {
        // UTF-16 with/without BOM - keep ASCII-ish bytes only for Switch UI simplicity
        std::string out;
        out.reserve(static_cast<size_t>(bodySize / 2));
        int i = 0;
        if (bodySize >= 2 &&
            (((unsigned char)body[0] == 0xFF && (unsigned char)body[1] == 0xFE) ||
             ((unsigned char)body[0] == 0xFE && (unsigned char)body[1] == 0xFF)))
            i = 2;
        const bool be = (encoding == 2) ||
            (bodySize >= 2 && (unsigned char)body[0] == 0xFE && (unsigned char)body[1] == 0xFF);
        for (; i + 1 < bodySize; i += 2) {
            unsigned char hi = static_cast<unsigned char>(be ? body[i] : body[i + 1]);
            unsigned char lo = static_cast<unsigned char>(be ? body[i + 1] : body[i]);
            unsigned int cp = (static_cast<unsigned int>(hi) << 8) | lo;
            if (cp == 0)
                break;
            if (cp < 0x80)
                out.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return trim(out);
    }
    return trim(std::string(body, body + bodySize));
}

int id3SynchSafe(const unsigned char* p) {
    return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) |
           ((p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

int mpegBitrateKbps(int version, int layer, int bitrateIndex) {
    // MPEG1 Layer III / MPEG2 Layer III common tables
    static const int v1l3[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const int v2l3[] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
    if (bitrateIndex <= 0 || bitrateIndex >= 15) return 0;
    if (version == 3 && layer == 1) return v1l3[bitrateIndex]; // MPEG1 Layer3
    if ((version == 2 || version == 0) && layer == 1) return v2l3[bitrateIndex];
    if (version == 3 && layer == 2) {
        static const int v1l2[] = {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0};
        return v1l2[bitrateIndex];
    }
    return v1l3[bitrateIndex];
}

int mpegSampleRate(int version, int rateIndex) {
    static const int rates[4][4] = {
        {11025, 12000, 8000, 0},   // MPEG2.5
        {0, 0, 0, 0},
        {22050, 24000, 16000, 0}, // MPEG2
        {44100, 48000, 32000, 0}, // MPEG1
    };
    if (version < 0 || version > 3 || rateIndex < 0 || rateIndex > 3) return 0;
    return rates[version][rateIndex];
}

struct Mp3DurationEstimate {
    float seconds = 0.f;
    bool reliable = false; // Xing/Info frame count (not a CBR bitrate guess)
};

Mp3DurationEstimate estimateMp3Duration(const std::string& path, int id3TagBytes) {
    Mp3DurationEstimate out;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return out;
    const auto fileSize = static_cast<long long>(f.tellg());
    if (fileSize <= id3TagBytes + 4) return out;

    f.seekg(id3TagBytes, std::ios::beg);
    unsigned char buf[4096]{};
    f.read(reinterpret_cast<char*>(buf), sizeof(buf));
    const int n = static_cast<int>(f.gcount());
    if (n < 4) return out;

    int frameAt = -1;
    for (int i = 0; i + 3 < n; ++i) {
        if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
            frameAt = i;
            break;
        }
    }
    if (frameAt < 0) return out;

    const unsigned char b1 = buf[frameAt + 1];
    const unsigned char b2 = buf[frameAt + 2];
    const int version = (b1 >> 3) & 0x3;
    const int layer = (b1 >> 1) & 0x3;
    const int bitrateIndex = (b2 >> 4) & 0xF;
    const int rateIndex = (b2 >> 2) & 0x3;
    const int bitrate = mpegBitrateKbps(version, layer, bitrateIndex);
    const int sampleRate = mpegSampleRate(version, rateIndex);
    if (bitrate <= 0 || sampleRate <= 0) return out;

    const bool mpeg1 = (version == 3);
    const int channels = ((buf[frameAt + 3] >> 6) & 0x3) == 3 ? 1 : 2;
    const int sideInfo = mpeg1 ? (channels == 1 ? 17 : 32) : (channels == 1 ? 9 : 17);
    const int samplesPerFrame = mpeg1 ? 1152 : 576;

    // Xing / Info VBR header
    const int xingAt = frameAt + 4 + sideInfo;
    if (xingAt + 12 < n) {
        const char* tag = reinterpret_cast<const char*>(buf + xingAt);
        if (std::memcmp(tag, "Xing", 4) == 0 || std::memcmp(tag, "Info", 4) == 0) {
            const unsigned char flags = buf[xingAt + 7];
            if (flags & 0x01) {
                const unsigned int frames =
                    (static_cast<unsigned int>(buf[xingAt + 8]) << 24) |
                    (static_cast<unsigned int>(buf[xingAt + 9]) << 16) |
                    (static_cast<unsigned int>(buf[xingAt + 10]) << 8) |
                    static_cast<unsigned int>(buf[xingAt + 11]);
                if (frames > 0) {
                    out.seconds = static_cast<float>(frames) * static_cast<float>(samplesPerFrame)
                                  / static_cast<float>(sampleRate);
                    out.reliable = true;
                    return out;
                }
            }
        }
    }

    const long long audioBytes = fileSize - id3TagBytes;
    if (audioBytes <= 0) return out;
    // Bitrate × size is only a guess (especially for VBR) — not safe for hard seeks.
    out.seconds = static_cast<float>(audioBytes * 8.0 / (static_cast<double>(bitrate) * 1000.0));
    out.reliable = false;
    return out;
}

// Deep Mix_SetMusicPosition into MP3 has frozen the menu on Switch (SDL_mixer /
// mpg123). Shallow resume seeks (~30s) are fine; multi-minute jumps are not.
constexpr float kMaxSafeMp3ResumeSeekSeconds = 90.f;
// Let Mix_PlayMusic settle a few frames before jumping into the stream.
constexpr int kDeferredSeekWarmupFrames = 20;

void extractApicCover(const char* payload, int frameSize, std::vector<uint8_t>& out) {
    out.clear();
    if (!payload || frameSize < 4) return;
    int enc = static_cast<unsigned char>(payload[0]);
    int i = 1;
    // MIME type (always latin1 / UTF-8 null-terminated)
    while (i < frameSize && payload[i] != 0) ++i;
    if (i >= frameSize) return;
    ++i; // skip null
    if (i >= frameSize) return;
    ++i; // picture type
    // Description (encoding-dependent terminator)
    if (enc == 0 || enc == 3) {
        while (i < frameSize && payload[i] != 0) ++i;
        if (i < frameSize) ++i;
    } else {
        while (i + 1 < frameSize && !(payload[i] == 0 && payload[i + 1] == 0)) i += 2;
        if (i + 1 < frameSize) i += 2;
    }
    if (i >= frameSize) return;
    const int dataSize = frameSize - i;
    if (dataSize < 24 || dataSize > 2 * 1024 * 1024)
        return;
    // Prefer JPEG/PNG signatures
    const auto* bytes = reinterpret_cast<const unsigned char*>(payload + i);
    const bool jpeg = dataSize > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8;
    const bool png = dataSize > 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G';
    if (!jpeg && !png) return;
    out.assign(payload + i, payload + frameSize);
}

} // namespace

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
    m_paused = false;

    for (auto* m : m_tracks) Mix_FreeMusic(m);
    m_tracks.clear();
    m_trackInfos.clear();
    m_shuffleOrder.clear();
    for (auto& [id, chunk] : m_sfx) Mix_FreeChunk(chunk);
    m_sfx.clear();
    for (auto& [id, chunk] : m_namedSfx) Mix_FreeChunk(chunk);
    m_namedSfx.clear();
    m_namedSfxVolumeScales.clear();
}

std::string AudioManager::cleanedFilenameTitle(const std::string& filename) {
    std::string name = filename;
    if (name.size() > 4) {
        const std::string ext = name.substr(name.size() - 4);
        if (ext == ".mp3" || ext == ".MP3" || ext == ".ogg" || ext == ".OGG" ||
            ext == ".wav" || ext == ".WAV")
            name = name.substr(0, name.size() - 4);
    }
    for (char& c : name) {
        if (c == '_' || c == '-')
            c = ' ';
    }
    // Collapse repeated spaces
    std::string out;
    bool space = false;
    for (char c : name) {
        if (c == ' ') {
            if (!space && !out.empty())
                out.push_back(' ');
            space = true;
        } else {
            out.push_back(c);
            space = false;
        }
    }
    return out;
}

bool AudioManager::pathLooksLikeMp3(const std::string& path) {
    if (path.size() < 4)
        return false;
    const std::string ext = path.substr(path.size() - 4);
    return ext == ".mp3" || ext == ".MP3" || ext == ".Mp3";
}

bool AudioManager::audioContainerLooksSafe(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    unsigned char head[16] = {};
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    const auto got = static_cast<size_t>(in.gcount());
    if (got < 4)
        return false;

    // ISO BMFF (MP4/M4A/AAC) mislabeled as .mp3 — Mix_PlayMusic has soft-locked
    // the Switch on these (BOTW Sheikah Tower OST shipped as ftypdash).
    if (got >= 8 && std::memcmp(head + 4, "ftyp", 4) == 0)
        return false;

    auto lowerExt = [](std::string p) {
        for (char& c : p) {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        const auto slash = p.find_last_of("/\\");
        const auto base = (slash == std::string::npos) ? p : p.substr(slash + 1);
        const auto dot = base.find_last_of('.');
        return (dot == std::string::npos) ? std::string{} : base.substr(dot);
    };
    const std::string ext = lowerExt(path);

    if (ext == ".mp3") {
        if (got >= 3 && std::memcmp(head, "ID3", 3) == 0)
            return true;
        // MPEG frame sync (1111 1111 111x xxxx)
        if (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0)
            return true;
        return false;
    }
    if (ext == ".ogg")
        return got >= 4 && std::memcmp(head, "OggS", 4) == 0;
    if (ext == ".wav")
        return got >= 4 && std::memcmp(head, "RIFF", 4) == 0;
    // Other extensions: let Mix decide (romfs may use uncommon names).
    return true;
}

void AudioManager::readMp3Metadata(const std::string& path, MusicTrackInfo& info) {
    info.title.clear();
    info.artist.clear();
    info.coverArt.clear();
    info.durationSeconds = 0.f;
    info.seekReliable = false;

    auto applyEstimate = [&](int id3TagBytes) {
        const Mp3DurationEstimate est = estimateMp3Duration(path, id3TagBytes);
        info.durationSeconds = est.seconds;
        info.seekReliable = est.reliable;
    };

    std::ifstream f(path, std::ios::binary);
    if (!f)
        return;
    char header[10]{};
    f.read(header, 10);
    if (f.gcount() < 10 || std::memcmp(header, "ID3", 3) != 0) {
        applyEstimate(0);
        return;
    }
    const int version = static_cast<unsigned char>(header[3]);
    if (version < 2 || version > 4) {
        applyEstimate(0);
        return;
    }
    const int tagSize = id3SynchSafe(reinterpret_cast<const unsigned char*>(header + 6));
    if (tagSize <= 0 || tagSize > 4 * 1024 * 1024) {
        applyEstimate(0);
        return;
    }
    std::vector<char> tag(static_cast<size_t>(tagSize));
    f.read(tag.data(), tagSize);
    const int got = static_cast<int>(f.gcount());
    if (got <= 0) {
        applyEstimate(0);
        return;
    }

    int offset = 0;
    if ((header[5] & 0x40) && got >= 4) {
        int ext = 0;
        if (version == 4)
            ext = id3SynchSafe(reinterpret_cast<const unsigned char*>(tag.data()));
        else
            ext = (static_cast<unsigned char>(tag[0]) << 24) |
                  (static_cast<unsigned char>(tag[1]) << 16) |
                  (static_cast<unsigned char>(tag[2]) << 8) |
                  static_cast<unsigned char>(tag[3]);
        offset = std::clamp(ext, 0, got);
    }

    float tlenSeconds = 0.f;
    while (offset + 10 <= got) {
        const char* id = tag.data() + offset;
        if (id[0] == 0)
            break;
        int frameSize = 0;
        if (version == 4)
            frameSize = id3SynchSafe(reinterpret_cast<const unsigned char*>(id + 4));
        else
            frameSize = (static_cast<unsigned char>(id[4]) << 24) |
                        (static_cast<unsigned char>(id[5]) << 16) |
                        (static_cast<unsigned char>(id[6]) << 8) |
                        static_cast<unsigned char>(id[7]);
        if (frameSize <= 0 || offset + 10 + frameSize > got)
            break;
        const char* payload = id + 10;
        std::string frameId(id, 4);
        if (frameId == "TIT2" && info.title.empty())
            info.title = decodeId3Text(payload, frameSize);
        else if (frameId == "TPE1" && info.artist.empty())
            info.artist = decodeId3Text(payload, frameSize);
        else if (frameId == "TLEN" && tlenSeconds <= 0.f) {
            std::string msText = decodeId3Text(payload, frameSize);
            try {
                const float ms = std::stof(msText);
                if (ms > 0.f)
                    tlenSeconds = ms / 1000.f;
            } catch (...) {}
        } else if (frameId == "APIC" && info.coverArt.empty()) {
            extractApicCover(payload, frameSize, info.coverArt);
        }
        offset += 10 + frameSize;
    }

    const int id3Total = 10 + tagSize;
    if (tlenSeconds > 0.f) {
        info.durationSeconds = tlenSeconds;
        info.seekReliable = true;
    } else {
        applyEstimate(id3Total);
    }
}

bool AudioManager::loadTrack(const std::string& path, const std::string& albumFolder) {
    if (!audioContainerLooksSafe(path)) {
        std::fprintf(stderr,
                     "[Audio] Rejected unsafe/mismatched audio container: %s "
                     "(need real MP3/OGG/WAV — MP4/AAC .mp3 renames freeze playback on Switch)\n",
                     path.c_str());
        return false;
    }

    Mix_Music* music = Mix_LoadMUS(path.c_str());
    if (!music) {
        std::fprintf(stderr, "[Audio] Failed to load %s: %s\n", path.c_str(), Mix_GetError());
        return false;
    }

    MusicTrackInfo info;
    info.path = path;
    const auto slash = path.find_last_of("/\\");
    info.filename = (slash == std::string::npos) ? path : path.substr(slash + 1);
    info.albumFolder = albumFolder;
    info.relativeKey = albumFolder.empty()
        ? info.filename
        : (albumFolder + "/" + info.filename);
    if (pathLooksLikeMp3(path)) {
        readMp3Metadata(path, info);
    } else {
        // OGG/WAV (and other Mix containers) seek reliably via SDL_mixer.
        info.seekReliable = true;
    }
    if (info.title.empty())
        info.title = cleanedFilenameTitle(info.filename);

    std::lock_guard<std::mutex> lk(m_trackMutex);
    m_tracks.push_back(music);
    m_trackInfos.push_back(std::move(info));
    rebuildShuffleOrder(true);
    return true;
}

void AudioManager::clearTracks() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    m_playing.store(false);
    m_paused = false;
    m_positionSeconds = 0.f;
    m_deferredSeekSeconds = -1.f;
    m_deferredSeekWarmupFrames = 0;
    for (auto* m : m_tracks) Mix_FreeMusic(m);
    m_tracks.clear();
    m_trackInfos.clear();
    m_folderCovers.clear();
    m_shuffleOrder.clear();
    m_current = 0;
}

void AudioManager::setFolderCover(const std::string& albumFolder, std::vector<uint8_t> bytes) {
    if (albumFolder.empty() || bytes.empty())
        return;
    std::lock_guard<std::mutex> lk(m_trackMutex);
    m_folderCovers[albumFolder] = std::move(bytes);
}

void AudioManager::applyPlaylistOrder(const std::vector<std::string>& orderedKeys) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_trackInfos.empty() || orderedKeys.empty())
        return;

    std::vector<Mix_Music*> newTracks;
    std::vector<MusicTrackInfo> newInfos;
    newTracks.reserve(m_tracks.size());
    newInfos.reserve(m_trackInfos.size());

    std::vector<bool> used(m_trackInfos.size(), false);
    auto take = [&](const std::string& key) {
        // Prefer relativeKey (folder-aware); fall back to bare filename for old configs.
        for (size_t i = 0; i < m_trackInfos.size(); ++i) {
            if (used[i]) continue;
            if (m_trackInfos[i].relativeKey == key) {
                used[i] = true;
                newTracks.push_back(m_tracks[i]);
                newInfos.push_back(m_trackInfos[i]);
                return;
            }
        }
        for (size_t i = 0; i < m_trackInfos.size(); ++i) {
            if (used[i]) continue;
            if (m_trackInfos[i].filename == key) {
                used[i] = true;
                newTracks.push_back(m_tracks[i]);
                newInfos.push_back(m_trackInfos[i]);
                return;
            }
        }
    };

    for (const auto& name : orderedKeys)
        take(name);
    for (size_t i = 0; i < m_trackInfos.size(); ++i) {
        if (!used[i]) {
            newTracks.push_back(m_tracks[i]);
            newInfos.push_back(m_trackInfos[i]);
        }
    }

    const std::string currentPath =
        (m_current >= 0 && m_current < static_cast<int>(m_trackInfos.size()))
            ? m_trackInfos[static_cast<size_t>(m_current)].path
            : std::string();

    m_tracks = std::move(newTracks);
    m_trackInfos = std::move(newInfos);
    m_current = 0;
    for (size_t i = 0; i < m_trackInfos.size(); ++i) {
        if (m_trackInfos[i].path == currentPath) {
            m_current = static_cast<int>(i);
            break;
        }
    }
    rebuildShuffleOrder(true);
}

std::vector<std::string> AudioManager::playlistFilenames() const {
    std::vector<std::string> names;
    names.reserve(m_trackInfos.size());
    for (const auto& t : m_trackInfos)
        names.push_back(t.filename);
    return names;
}

std::vector<std::string> AudioManager::playlistOrderKeys() const {
    std::vector<std::string> keys;
    keys.reserve(m_trackInfos.size());
    for (const auto& t : m_trackInfos)
        keys.push_back(t.relativeKey.empty() ? t.filename : t.relativeKey);
    return keys;
}

bool AudioManager::hasAlbumFolders() const {
    for (const auto& t : m_trackInfos) {
        if (!t.albumFolder.empty())
            return true;
    }
    return false;
}

const std::vector<uint8_t>* AudioManager::trackCoverArt(int index) const {
    if (index < 0 || index >= static_cast<int>(m_trackInfos.size()))
        return nullptr;
    const auto& art = m_trackInfos[static_cast<size_t>(index)].coverArt;
    return art.empty() ? nullptr : &art;
}

const std::vector<uint8_t>* AudioManager::folderCoverArt(const std::string& albumFolder) const {
    if (albumFolder.empty())
        return nullptr;
    auto it = m_folderCovers.find(albumFolder);
    if (it == m_folderCovers.end() || it->second.empty())
        return nullptr;
    return &it->second;
}

std::string AudioManager::currentDisplayTitle() const {
    if (m_current < 0 || m_current >= static_cast<int>(m_trackInfos.size()))
        return {};
    return m_trackInfos[static_cast<size_t>(m_current)].displayTitle();
}

std::atomic<AudioManager*> AudioManager::s_instance{nullptr};

void AudioManager::onTrackFinished() {
    AudioManager* inst = s_instance.load();
    if (!inst || inst->m_suppressFinishedHook)
        return;
    if (inst->m_repeat == MusicRepeatMode::One) {
        std::lock_guard<std::mutex> lk(inst->m_trackMutex);
        inst->m_positionSeconds = 0.f;
        inst->playCurrentInternal(true);
        return;
    }
    inst->nextTrack();
}

void AudioManager::rebuildShuffleOrder(bool keepCurrent) {
    m_shuffleOrder.resize(m_trackInfos.size());
    for (int i = 0; i < static_cast<int>(m_trackInfos.size()); ++i)
        m_shuffleOrder[static_cast<size_t>(i)] = i;
    if (!m_shuffle || m_shuffleOrder.size() <= 1)
        return;
    std::mt19937 rng{std::random_device{}()};
    if (keepCurrent && m_current >= 0 && m_current < static_cast<int>(m_shuffleOrder.size())) {
        std::swap(m_shuffleOrder[0], m_shuffleOrder[static_cast<size_t>(m_current)]);
        if (m_shuffleOrder.size() > 1)
            std::shuffle(m_shuffleOrder.begin() + 1, m_shuffleOrder.end(), rng);
    } else {
        std::shuffle(m_shuffleOrder.begin(), m_shuffleOrder.end(), rng);
    }
}

int AudioManager::nextIndexInOrder(int from) const {
    const int n = static_cast<int>(m_trackInfos.size());
    if (n <= 0) return 0;
    if (!m_shuffle)
        return (from + 1) % n;
    int pos = 0;
    for (int i = 0; i < n; ++i) {
        if (m_shuffleOrder[static_cast<size_t>(i)] == from) {
            pos = i;
            break;
        }
    }
    return m_shuffleOrder[static_cast<size_t>((pos + 1) % n)];
}

int AudioManager::prevIndexInOrder(int from) const {
    const int n = static_cast<int>(m_trackInfos.size());
    if (n <= 0) return 0;
    if (!m_shuffle)
        return (from - 1 + n) % n;
    int pos = 0;
    for (int i = 0; i < n; ++i) {
        if (m_shuffleOrder[static_cast<size_t>(i)] == from) {
            pos = i;
            break;
        }
    }
    return m_shuffleOrder[static_cast<size_t>((pos - 1 + n) % n)];
}

void AudioManager::playCurrentInternal(bool fromStart) {
    if (m_tracks.empty() || m_current < 0 ||
        m_current >= static_cast<int>(m_tracks.size()))
        return;
    if (!currentTrackFileExistsUnlocked()) {
        std::fprintf(stderr,
                     "[Audio] Resume skipped: track file missing (%s)\n",
                     m_trackInfos[static_cast<size_t>(m_current)].path.c_str());
        m_playing.store(false);
        m_paused = true;
        m_deferredSeekSeconds = -1.f;
        return;
    }
    s_instance.store(this);
    Mix_HookMusicFinished(onTrackFinished);
    m_suppressFinishedHook = true;
    Mix_PlayMusic(m_tracks[static_cast<size_t>(m_current)], 1);
    if (!fromStart) {
        const float seekTo = sanitizeSeekSecondsUnlocked(m_positionSeconds);
        m_positionSeconds = seekTo;
        if (seekTo > 0.25f)
            Mix_SetMusicPosition(static_cast<double>(seekTo));
    } else {
        m_positionSeconds = 0.f;
    }
    m_suppressFinishedHook = false;
    m_playing.store(true);
    m_paused = false;
    applyMusicVolume();
}

void AudioManager::play() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    // HOME / overlay dismiss used to call play() while music was already
    // running; Mix_PlayMusic again caused an audible skip.
    if (m_playing.load() && Mix_PlayingMusic())
        return;
    playCurrentInternal(m_positionSeconds <= 0.05f);
}

void AudioManager::pause() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (!m_playing.load() && m_paused)
        return;
    m_suppressFinishedHook = true;
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    m_suppressFinishedHook = false;
    m_playing.store(false);
    m_paused = true;
}

void AudioManager::stop() {
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    m_playing.store(false);
    m_paused = true;
    m_positionSeconds = 0.f;
}

void AudioManager::togglePlayPause() {
    if (m_playing.load())
        pause();
    else
        play();
}

void AudioManager::nextTrack() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    if (m_repeat == MusicRepeatMode::Off && !m_shuffle) {
        if (m_current >= static_cast<int>(m_tracks.size()) - 1) {
            m_suppressFinishedHook = true;
            Mix_HaltMusic();
            m_suppressFinishedHook = false;
            m_playing.store(false);
            m_paused = true;
            m_positionSeconds = 0.f;
            return;
        }
    }
    m_current = nextIndexInOrder(m_current);
    m_positionSeconds = 0.f;
    playCurrentInternal(true);
}

void AudioManager::prevTrack() {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;
    if (m_positionSeconds > 3.f) {
        m_positionSeconds = 0.f;
        playCurrentInternal(true);
        return;
    }
    m_current = prevIndexInOrder(m_current);
    m_positionSeconds = 0.f;
    playCurrentInternal(true);
}

void AudioManager::playTrackAt(int index) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (index < 0 || index >= static_cast<int>(m_tracks.size()))
        return;
    m_current = index;
    m_positionSeconds = 0.f;
    playCurrentInternal(true);
}

bool AudioManager::moveTrack(int from, int to) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    const int n = static_cast<int>(m_tracks.size());
    if (from < 0 || to < 0 || from >= n || to >= n || from == to)
        return false;
    auto music = m_tracks[static_cast<size_t>(from)];
    auto info = m_trackInfos[static_cast<size_t>(from)];
    m_tracks.erase(m_tracks.begin() + from);
    m_trackInfos.erase(m_trackInfos.begin() + from);
    m_tracks.insert(m_tracks.begin() + to, music);
    m_trackInfos.insert(m_trackInfos.begin() + to, std::move(info));
    if (m_current == from)
        m_current = to;
    else if (from < m_current && to >= m_current)
        --m_current;
    else if (from > m_current && to <= m_current)
        ++m_current;
    rebuildShuffleOrder(true);
    return true;
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
    // External fades (launch anim) own the envelope; cancel restore ramp.
    if (m_restoreFadeIn && fade < 0.999f)
        m_restoreFadeIn = false;
    applyMusicVolume();
}

void AudioManager::setShuffle(bool enabled) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    m_shuffle = enabled;
    rebuildShuffleOrder(true);
}

void AudioManager::setRepeatMode(MusicRepeatMode mode) {
    m_repeat = mode;
}

void AudioManager::update(float dt) {
    // Apply resume seeks off the splash/GPU burst, after Mix_PlayMusic has
    // actually started (see updateDeferredSeek warm-up).
    updateDeferredSeek();

    if (m_restoreFadeIn && dt > 0.f) {
        // Soft fade-in after mute-until-seek restore (HOME AppletReturn).
        m_musicFade = std::min(1.f, m_musicFade + dt / 0.45f);
        applyMusicVolume();
        if (m_musicFade >= 0.999f) {
            m_musicFade = 1.f;
            m_restoreFadeIn = false;
            applyMusicVolume();
        }
    }
    if (m_playing.load() && dt > 0.f) {
        m_positionSeconds += dt;
        const float dur = durationSeconds();
        if (dur > 0.f && m_positionSeconds > dur)
            m_positionSeconds = dur;
    }
}

float AudioManager::durationSeconds() const {
    if (m_current < 0 || m_current >= static_cast<int>(m_trackInfos.size()))
        return 0.f;
    return m_trackInfos[static_cast<size_t>(m_current)].durationSeconds;
}

void AudioManager::setPositionSeconds(float seconds) {
    m_positionSeconds = std::max(0.f, seconds);
}

void AudioManager::seekTo(float seconds) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    const float target = sanitizeSeekSecondsUnlocked(seconds);
    m_positionSeconds = target;
    if (m_playing.load()) {
        if (target > 0.25f)
            Mix_SetMusicPosition(static_cast<double>(m_positionSeconds));
        else
            playCurrentInternal(true);
    } else if (m_paused && !m_tracks.empty()) {
        // Keep paused but remember seek point for next play().
    }
}

const std::vector<uint8_t>& AudioManager::currentCoverArt() const {
    static const std::vector<uint8_t> kEmpty;
    if (m_current < 0 || m_current >= static_cast<int>(m_trackInfos.size()))
        return kEmpty;
    return m_trackInfos[static_cast<size_t>(m_current)].coverArt;
}

void AudioManager::restorePlaybackState(int trackIndex,
                                        float positionSeconds,
                                        bool playing,
                                        const std::string& trackKey) {
    std::lock_guard<std::mutex> lk(m_trackMutex);
    if (m_tracks.empty()) return;

    int resolved = findTrackIndexByKeyUnlocked(trackKey);
    bool dropSeek = false;
    if (resolved >= 0) {
        // Stable key matched — resume position is meaningful for this file.
    } else if (!trackKey.empty()) {
        // User deleted/renamed the saved track. Do not fall back to a stale
        // index + seek into whatever now sits at that slot.
        std::fprintf(stderr,
                     "[Audio] Saved track key missing (%s); starting at first remaining track\n",
                     trackKey.c_str());
        resolved = 0;
        dropSeek = true;
    } else {
        // Legacy configs only store an index. After add/remove that index often
        // points at a different file — never Mix_SetMusicPosition without a key.
        if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size()))
            resolved = trackIndex;
        else
            resolved = 0;
        dropSeek = true;
    }

    m_current = resolved;
    const float safeSeek = dropSeek ? 0.f : sanitizeResumeSeekUnlocked(positionSeconds);
    if (!dropSeek && safeSeek + 0.01f < std::max(0.f, positionSeconds)
        && positionSeconds > 0.25f) {
        std::fprintf(stderr,
                     "[Audio] Clamped/dropped unsafe resume seek %.2fs (duration=%.2fs reliable=%d mp3=%d)\n",
                     positionSeconds,
                     durationSeconds(),
                     (m_current >= 0 && m_current < static_cast<int>(m_trackInfos.size()))
                         ? (m_trackInfos[static_cast<size_t>(m_current)].seekReliable ? 1 : 0)
                         : 0,
                     currentTrackIsMp3Unlocked() ? 1 : 0);
    }
    m_positionSeconds = safeSeek;
    m_deferredSeekSeconds = -1.f;
    m_deferredSeekWarmupFrames = 0;
    m_restoreFadeIn = false;
    if (playing) {
        // Stay silent only until the deferred seek lands, then snap to full volume.
        m_musicFade = 0.f;
        applyMusicVolume();
        if (safeSeek > 0.25f) {
            m_deferredSeekSeconds = safeSeek;
            m_deferredSeekWarmupFrames = kDeferredSeekWarmupFrames;
        } else {
            m_musicFade = 1.f;
            applyMusicVolume();
        }
        playCurrentInternal(true);
    } else {
        m_suppressFinishedHook = true;
        Mix_HaltMusic();
        m_suppressFinishedHook = false;
        m_playing.store(false);
        m_paused = true;
        m_musicFade = 1.f;
        applyMusicVolume();
    }
}

void AudioManager::updateDeferredSeek() {
    float seekTo = -1.f;
    {
        std::lock_guard<std::mutex> lk(m_trackMutex);
        if (m_deferredSeekSeconds < 0.f || !m_playing.load())
            return;
        if (m_deferredSeekWarmupFrames > 0) {
            --m_deferredSeekWarmupFrames;
            return;
        }
        if (!Mix_PlayingMusic())
            return;
        seekTo = sanitizeResumeSeekUnlocked(m_deferredSeekSeconds);
        m_deferredSeekSeconds = -1.f;
        m_positionSeconds = seekTo;
        if (seekTo <= 0.25f) {
            m_musicFade = 1.f;
            m_restoreFadeIn = false;
            applyMusicVolume();
            return;
        }
    }
    Mix_SetMusicPosition(static_cast<double>(seekTo));
    {
        std::lock_guard<std::mutex> lk(m_trackMutex);
        m_musicFade = 1.f;
        m_restoreFadeIn = false;
        applyMusicVolume();
    }
}

std::string AudioManager::currentTrackKey() const {
    if (m_current < 0 || m_current >= static_cast<int>(m_trackInfos.size()))
        return {};
    const auto& info = m_trackInfos[static_cast<size_t>(m_current)];
    return info.relativeKey.empty() ? info.filename : info.relativeKey;
}

bool AudioManager::reconcileResumeConfig(int& trackIndex,
                                         float& positionSeconds,
                                         std::string& trackKey,
                                         std::vector<std::string>& playlistOrder) const {
    bool changed = false;
    bool playlistChurn = false;

    if (!playlistOrder.empty()) {
        std::vector<std::string> kept;
        kept.reserve(playlistOrder.size());
        for (const auto& key : playlistOrder) {
            if (findTrackIndexByKeyUnlocked(key) >= 0)
                kept.push_back(key);
            else {
                playlistChurn = true;
                changed = true;
            }
        }
        // Append any newly added tracks that were not in the saved order.
        for (const auto& info : m_trackInfos) {
            const std::string& key = info.relativeKey.empty() ? info.filename : info.relativeKey;
            if (std::find(kept.begin(), kept.end(), key) == kept.end()) {
                kept.push_back(key);
                playlistChurn = true;
                changed = true;
            }
        }
        if (kept != playlistOrder) {
            playlistOrder = std::move(kept);
            changed = true;
        }
    }

    int resolved = findTrackIndexByKeyUnlocked(trackKey);
    if (!trackKey.empty() && resolved < 0) {
        trackKey.clear();
        trackIndex = 0;
        positionSeconds = 0.f;
        if (!m_trackInfos.empty()) {
            trackKey = m_trackInfos[0].relativeKey.empty()
                ? m_trackInfos[0].filename
                : m_trackInfos[0].relativeKey;
        }
        changed = true;
    } else if (resolved >= 0) {
        if (trackIndex != resolved) {
            trackIndex = resolved;
            changed = true;
        }
        const std::string& canonical =
            m_trackInfos[static_cast<size_t>(resolved)].relativeKey.empty()
                ? m_trackInfos[static_cast<size_t>(resolved)].filename
                : m_trackInfos[static_cast<size_t>(resolved)].relativeKey;
        if (trackKey != canonical) {
            trackKey = canonical;
            changed = true;
        }
        const float safe = sanitizeSeekSecondsForDuration(
            positionSeconds,
            m_trackInfos[static_cast<size_t>(resolved)].durationSeconds);
        if (safe + 0.01f < positionSeconds || safe > positionSeconds + 0.01f) {
            positionSeconds = safe;
            changed = true;
        }
    } else if (m_trackInfos.empty()) {
        if (trackIndex != 0 || positionSeconds != 0.f || !trackKey.empty()) {
            trackIndex = 0;
            positionSeconds = 0.f;
            trackKey.clear();
            changed = true;
        }
    } else {
        // Legacy (no musicTrackKey yet), or playlist files changed under us.
        if (playlistChurn
            || trackIndex < 0
            || trackIndex >= static_cast<int>(m_trackInfos.size())) {
            trackIndex = 0;
            positionSeconds = 0.f;
            changed = true;
        } else if (positionSeconds > 0.25f) {
            // Index-only resume cannot safely seek after users edit music files.
            positionSeconds = 0.f;
            changed = true;
        }
        const auto& info = m_trackInfos[static_cast<size_t>(trackIndex)];
        const std::string canonical =
            info.relativeKey.empty() ? info.filename : info.relativeKey;
        if (trackKey != canonical) {
            trackKey = canonical;
            changed = true;
        }
    }

    return changed;
}

int AudioManager::findTrackIndexByKeyUnlocked(const std::string& trackKey) const {
    if (trackKey.empty())
        return -1;
    for (size_t i = 0; i < m_trackInfos.size(); ++i) {
        if (m_trackInfos[i].relativeKey == trackKey)
            return static_cast<int>(i);
    }
    for (size_t i = 0; i < m_trackInfos.size(); ++i) {
        if (m_trackInfos[i].filename == trackKey)
            return static_cast<int>(i);
    }
    return -1;
}

float AudioManager::sanitizeSeekSecondsForDuration(float positionSeconds,
                                                   float durationSeconds) const {
    // NaN / negative → no seek.
    if (!(positionSeconds > 0.25f))
        return 0.f;
    // Unknown duration: seeking into an arbitrary MP3 has hung SDL_mixer on
    // Switch after playlist churn. Prefer start-of-track over a speculative seek.
    if (!(durationSeconds > 1.f))
        return 0.f;
    if (positionSeconds >= durationSeconds - 0.05f)
        return 0.f;
    return positionSeconds;
}

float AudioManager::sanitizeSeekSecondsUnlocked(float positionSeconds) const {
    return sanitizeSeekSecondsForDuration(positionSeconds, durationSeconds());
}

float AudioManager::sanitizeResumeSeekUnlocked(float positionSeconds) const {
    const float clamped = sanitizeSeekSecondsUnlocked(positionSeconds);
    if (clamped <= 0.25f)
        return 0.f;
    if (!currentTrackIsMp3Unlocked())
        return clamped;
    if (m_current < 0 || m_current >= static_cast<int>(m_trackInfos.size()))
        return 0.f;
    const auto& info = m_trackInfos[static_cast<size_t>(m_current)];
    // Bitrate-guessed lengths are not trustworthy for mpg123 jumps.
    if (!info.seekReliable)
        return 0.f;
    // Deep MP3 hard-seeks have frozen AppletReturn (menu.log stopped between
    // "music restore target" and "music playback restore returned").
    if (clamped > kMaxSafeMp3ResumeSeekSeconds)
        return 0.f;
    return clamped;
}

bool AudioManager::currentTrackIsMp3Unlocked() const {
    if (m_current < 0 || m_current >= static_cast<int>(m_trackInfos.size()))
        return false;
    return pathLooksLikeMp3(m_trackInfos[static_cast<size_t>(m_current)].path);
}

bool AudioManager::currentTrackFileExistsUnlocked() const {
    if (m_current < 0 || m_current >= static_cast<int>(m_trackInfos.size()))
        return false;
    std::error_code ec;
    return std::filesystem::is_regular_file(
        m_trackInfos[static_cast<size_t>(m_current)].path, ec);
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
