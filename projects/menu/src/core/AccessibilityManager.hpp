#pragma once

#include <nxui/widgets/Widget.hpp>
#include <string>
#include <vector>

#ifdef SWITCHU_ENABLE_ESPEAK
#include <SDL2/SDL_mixer.h>
#include <espeak-ng/speak_lib.h>
#endif

class AccessibilityManager {
public:
    AccessibilityManager() = default;
    ~AccessibilityManager();

    bool initialize(bool enabled, const std::string& voice = "fr",
                    const std::string& dataRoot = {});
    void shutdown();

    void setEnabled(bool enabled);
    bool enabled() const { return m_enabled; }
    void setSpeakHints(bool enabled) { m_speakHints = enabled; }
    bool speakHints() const { return m_speakHints; }
    void setSpeakContextEveryFocus(bool enabled) { m_speakContextEveryFocus = enabled; }
    bool speakContextEveryFocus() const { return m_speakContextEveryFocus; }
    void setSpeechRate(int wordsPerMinute);
    int speechRate() const { return m_speechRate; }

    bool setVoiceForLanguageTag(const std::string& languageTag);
    static std::string voiceForLanguageTag(const std::string& languageTag);

    void announce(const std::string& text, bool interrupt = true, bool allowRepeat = false);
    void repeatLastAnnouncement();
    void announceFocus(nxui::Widget* widget, const std::string& context = {},
                       bool forceRepeat = false);
    void announceStructuredFocus(const std::string& context,
                                 const std::string& position,
                                 const std::string& summary,
                                 bool forceRepeat = false);

private:
    std::string describeWidget(nxui::Widget* widget, const std::string& context);
    std::string composeStructuredFocus(const std::string& context,
                                       const std::string& position,
                                       const std::string& summary);
#ifdef SWITCHU_ENABLE_ESPEAK
    static int synthCallback(short* wav, int numsamples, espeak_EVENT* events);
    void appendSynthSamples(short* wav, int numsamples);
    void playSynthBuffer();
    void releaseCurrentSpeech();

    std::vector<short> m_synthPcm;
    std::vector<unsigned char> m_mixBuffer;
    Mix_Chunk* m_ttsChunk = nullptr;
    int m_ttsChannel = -1;
    int m_sampleRate = 0;
    static AccessibilityManager* s_activeSynthTarget;
#endif

    bool m_initialized = false;
    bool m_enabled = true;
    bool m_speakHints = true;
    bool m_speakContextEveryFocus = false;
    int m_speechRate = 190;
    std::string m_voice = "en-us";
    std::string m_lastAnnouncement;
    std::string m_lastFocusContext;
};
