#include "AccessibilityManager.hpp"
#include "DebugLog.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <vector>

#ifdef SWITCHU_ENABLE_ESPEAK
#include <SDL2/SDL.h>
#include <espeak-ng/speak_lib.h>
#endif

namespace {

std::string trimSpaces(std::string text) {
    auto notSpace = [](unsigned char ch) {
        return std::isspace(ch) == 0;
    };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

std::string joinSentence(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    return left + ". " + right;
}

int clampSpeechRate(int rate) {
    return std::clamp(rate, 120, 320);
}

std::string joinPath(const std::string& base, const char* child) {
    if (base.empty())
        return child;
    if (base.back() == '/')
        return base + child;
    return base + "/" + child;
}

std::string normalizeLanguageTag(std::string tag) {
    std::replace(tag.begin(), tag.end(), '_', '-');
    std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });
    return tag;
}

} // namespace

#ifdef SWITCHU_ENABLE_ESPEAK
AccessibilityManager* AccessibilityManager::s_activeSynthTarget = nullptr;
#endif

AccessibilityManager::~AccessibilityManager() {
    shutdown();
}

bool AccessibilityManager::initialize(bool enabled, const std::string& voice,
                                      const std::string& dataRoot) {
    m_enabled = enabled;
    m_voice = voiceForLanguageTag(voice);
    m_speechRate = clampSpeechRate(m_speechRate);
    m_lastAnnouncement.clear();
    m_lastFocusContext.clear();

#ifdef SWITCHU_ENABLE_ESPEAK
    s_activeSynthTarget = this;
    espeak_SetSynthCallback(&AccessibilityManager::synthCallback);

    std::vector<std::string> dataRoots;
    if (!dataRoot.empty())
        dataRoots.push_back(dataRoot);
    dataRoots.push_back("sdmc:/switch/SwitchU");
    dataRoots.push_back("romfs:");

    int sampleRate = -1;
    std::string usedRoot;
    for (const auto& root : dataRoots) {
        sampleRate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 80, root.c_str(),
                                       espeakINITIALIZE_DONT_EXIT);
        if (sampleRate > 0) {
            usedRoot = root;
            break;
        }
        DebugLog::log("[accessibility] espeak init failed with data root %s",
                      joinPath(root, "espeak-ng-data").c_str());
    }

    if (sampleRate <= 0) {
        DebugLog::log("[accessibility] espeak-ng initialization failed");
        if (s_activeSynthTarget == this)
            s_activeSynthTarget = nullptr;
        m_initialized = false;
        return false;
    }
    m_sampleRate = sampleRate;
    m_initialized = true;
    setVoiceForLanguageTag(m_voice);
    setSpeechRate(m_speechRate);
    DebugLog::log("[accessibility] espeak-ng initialized voice=%s rate=%d data=%s",
                  m_voice.c_str(),
                  sampleRate,
                  joinPath(usedRoot, "espeak-ng-data").c_str());
#else
    (void)voice;
    (void)dataRoot;
    m_initialized = true;
#ifdef SWITCHU_ESPEAK_REQUESTED
    DebugLog::log("[accessibility] espeak requested but espeak-ng headers/libs were not found; announcements are logged only");
#else
    DebugLog::log("[accessibility] built without espeak-ng; announcements are logged only");
#endif
#endif

    return true;
}

void AccessibilityManager::shutdown() {
    if (!m_initialized)
        return;

#ifdef SWITCHU_ENABLE_ESPEAK
    releaseCurrentSpeech();
    espeak_Cancel();
    espeak_Terminate();
    if (s_activeSynthTarget == this)
        s_activeSynthTarget = nullptr;
#endif
    m_initialized = false;
    m_lastAnnouncement.clear();
    m_lastFocusContext.clear();
}

void AccessibilityManager::setEnabled(bool enabled) {
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
#ifdef SWITCHU_ENABLE_ESPEAK
    if (!m_enabled) {
        releaseCurrentSpeech();
        espeak_Cancel();
    }
#endif
}

void AccessibilityManager::setSpeechRate(int wordsPerMinute) {
    m_speechRate = clampSpeechRate(wordsPerMinute);
#ifdef SWITCHU_ENABLE_ESPEAK
    if (m_initialized)
        espeak_SetParameter(espeakRATE, m_speechRate, 0);
#endif
}

bool AccessibilityManager::setVoiceForLanguageTag(const std::string& languageTag) {
    const std::string voice = voiceForLanguageTag(languageTag);
    m_voice = voice;

#ifdef SWITCHU_ENABLE_ESPEAK
    if (!m_initialized)
        return true;

    const espeak_ERROR result = espeak_SetVoiceByName(voice.c_str());
    if (result != EE_OK) {
        DebugLog::log("[accessibility] espeak voice '%s' failed, falling back to en-us",
                      voice.c_str());
        m_voice = "en-us";
        return espeak_SetVoiceByName(m_voice.c_str()) == EE_OK;
    }
#endif

    DebugLog::log("[accessibility] voice set to %s for language %s",
                  m_voice.c_str(),
                  languageTag.empty() ? "<empty>" : languageTag.c_str());
    return true;
}

std::string AccessibilityManager::voiceForLanguageTag(const std::string& languageTag) {
    const std::string tag = normalizeLanguageTag(languageTag);
    if (tag.empty() || tag == "auto")
        return "en-us";

    static const std::unordered_map<std::string, std::string> kVoiceByTag = {
        {"ja-jp", "ja"},
        {"ja", "ja"},
        {"en-us", "en-us"},
        {"en-gb", "en-gb"},
        {"en", "en-us"},
        {"fr-fr", "fr"},
        {"fr-ca", "fr"},
        {"fr", "fr"},
        {"de-de", "de"},
        {"de", "de"},
        {"it-it", "it"},
        {"it", "it"},
        {"es-es", "es"},
        {"es-419", "es-419"},
        {"es-mx", "es-419"},
        {"es", "es"},
        {"zh-cn", "cmn"},
        {"zh-hans", "cmn"},
        {"zh-tw", "yue"},
        {"zh-hant", "yue"},
        {"zh", "cmn"},
        {"ko-kr", "ko"},
        {"ko", "ko"},
        {"nl-nl", "nl"},
        {"nl", "nl"},
        {"pt-pt", "pt"},
        {"pt-br", "pt-BR"},
        {"pt", "pt"},
        {"ru-ru", "ru"},
        {"ru", "ru"},
    };

    if (auto it = kVoiceByTag.find(tag); it != kVoiceByTag.end())
        return it->second;

    const size_t dash = tag.find('-');
    if (dash != std::string::npos) {
        const std::string base = tag.substr(0, dash);
        if (auto it = kVoiceByTag.find(base); it != kVoiceByTag.end())
            return it->second;
    }

    return "en-us";
}

void AccessibilityManager::announce(const std::string& text, bool interrupt, bool allowRepeat) {
    if (!m_initialized || !m_enabled)
        return;

    std::string spoken = trimSpaces(text);
    if (spoken.empty())
        return;

    if (!allowRepeat && spoken == m_lastAnnouncement)
        return;
    m_lastAnnouncement = spoken;

#ifdef SWITCHU_ENABLE_ESPEAK
    if (interrupt) {
        releaseCurrentSpeech();
        espeak_Cancel();
    }
    m_synthPcm.clear();
    espeak_Synth(spoken.c_str(),
                 spoken.size() + 1,
                 0,
                 POS_CHARACTER,
                 0,
                 espeakCHARS_UTF8,
                 nullptr,
                 nullptr);
    espeak_Synchronize();
    playSynthBuffer();
#else
    (void)interrupt;
    DebugLog::log("[accessibility] speak: %s", spoken.c_str());
#endif
}

void AccessibilityManager::repeatLastAnnouncement() {
    announce(m_lastAnnouncement, true, true);
}

#ifdef SWITCHU_ENABLE_ESPEAK
int AccessibilityManager::synthCallback(short* wav, int numsamples, espeak_EVENT* events) {
    (void)events;
    if (s_activeSynthTarget)
        s_activeSynthTarget->appendSynthSamples(wav, numsamples);
    return 0;
}

void AccessibilityManager::appendSynthSamples(short* wav, int numsamples) {
    if (!wav || numsamples <= 0)
        return;
    m_synthPcm.insert(m_synthPcm.end(), wav, wav + numsamples);
}

void AccessibilityManager::releaseCurrentSpeech() {
    if (m_ttsChannel >= 0) {
        Mix_HaltChannel(m_ttsChannel);
        m_ttsChannel = -1;
    }
    if (m_ttsChunk) {
        Mix_FreeChunk(m_ttsChunk);
        m_ttsChunk = nullptr;
    }
    m_mixBuffer.clear();
}

void AccessibilityManager::playSynthBuffer() {
    if (m_synthPcm.empty())
        return;

    int mixFreq = 0;
    Uint16 mixFormat = 0;
    int mixChannels = 0;
    if (!Mix_QuerySpec(&mixFreq, &mixFormat, &mixChannels) || mixFreq <= 0 || mixChannels <= 0) {
        DebugLog::log("[accessibility] mixer is not ready for speech playback");
        return;
    }

    const auto* raw = reinterpret_cast<const unsigned char*>(m_synthPcm.data());
    const int rawBytes = (int)(m_synthPcm.size() * sizeof(short));

    SDL_AudioCVT cvt {};
    if (SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, 1, m_sampleRate, mixFormat, mixChannels, mixFreq) < 0) {
        DebugLog::log("[accessibility] SDL_BuildAudioCVT failed: %s", SDL_GetError());
        return;
    }

    releaseCurrentSpeech();

    if (cvt.needed) {
        cvt.len = rawBytes;
        cvt.buf = static_cast<Uint8*>(SDL_malloc((size_t)cvt.len * (size_t)cvt.len_mult));
        if (!cvt.buf) {
            DebugLog::log("[accessibility] speech conversion allocation failed");
            return;
        }
        std::memcpy(cvt.buf, raw, (size_t)rawBytes);
        if (SDL_ConvertAudio(&cvt) < 0) {
            DebugLog::log("[accessibility] SDL_ConvertAudio failed: %s", SDL_GetError());
            SDL_free(cvt.buf);
            return;
        }
        m_mixBuffer.assign(cvt.buf, cvt.buf + cvt.len_cvt);
        SDL_free(cvt.buf);
    } else {
        m_mixBuffer.assign(raw, raw + rawBytes);
    }

    if (m_mixBuffer.empty())
        return;

    m_ttsChunk = Mix_QuickLoad_RAW(m_mixBuffer.data(), (Uint32)m_mixBuffer.size());
    if (!m_ttsChunk) {
        DebugLog::log("[accessibility] Mix_QuickLoad_RAW failed: %s", Mix_GetError());
        m_mixBuffer.clear();
        return;
    }

    Mix_VolumeChunk(m_ttsChunk, MIX_MAX_VOLUME);
    m_ttsChannel = Mix_PlayChannel(-1, m_ttsChunk, 0);
    if (m_ttsChannel < 0)
        DebugLog::log("[accessibility] Mix_PlayChannel failed: %s", Mix_GetError());
}
#endif

void AccessibilityManager::announceFocus(nxui::Widget* widget, const std::string& context,
                                         bool forceRepeat) {
    announce(describeWidget(widget, context), true, forceRepeat);
}

void AccessibilityManager::announceStructuredFocus(const std::string& context,
                                                   const std::string& position,
                                                   const std::string& summary,
                                                   bool forceRepeat) {
    announce(composeStructuredFocus(context, position, summary), true, forceRepeat);
}

std::string AccessibilityManager::describeWidget(nxui::Widget* widget,
                                                 const std::string& context) {
    if (!widget)
        return {};

    std::string summary = widget->accessibilitySummary();
    if (summary.empty())
        summary = widget->tag();

    std::string out = composeStructuredFocus(context, {}, summary);
    if (!m_speakHints) {
        const std::string hint = widget->accessibilityHint();
        if (!hint.empty()) {
            const std::string marker = ". " + hint;
            if (out.size() >= marker.size() &&
                out.compare(out.size() - marker.size(), marker.size(), marker) == 0) {
                out.erase(out.size() - marker.size());
            }
        }
    }
    return out;
}

std::string AccessibilityManager::composeStructuredFocus(const std::string& context,
                                                         const std::string& position,
                                                         const std::string& summary) {
    std::string spokenContext;
    if (!context.empty() && (m_speakContextEveryFocus || context != m_lastFocusContext))
        spokenContext = context;

    m_lastFocusContext = context;

    std::string out = joinSentence(spokenContext, summary);
    out = joinSentence(out, position);
    return out;
}
