#include "TabBuilders.hpp"
#include "core/AccessibilityManager.hpp"
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>

SettingsScreen::Tab settings::tabs::AccessibilityTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;

    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.accessibility", "Accessibility");

    {
        SettingItem it;
        it.label = i18n.tr("settings.accessibility.voice", "Voice Guidance");
        it.description = i18n.tr("settings.accessibility.voice_desc",
                                 "Read focused items and available actions aloud.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_accessibilityEnabled;
        it.suppressToggleSfx = true;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_accessibilityEnabled = self.boolVal;
            if (screen.m_accessibilityEnabledCb)
                screen.m_accessibilityEnabledCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.accessibility.voice_language", "Voice Language");
        it.description = i18n.tr("settings.accessibility.voice_language_desc",
                                 "Follows the current system or UI language.");
        it.type = ItemType::Info;
        const std::string activeTag = i18n.activeLanguageTag();
        const std::string voice = AccessibilityManager::voiceForLanguageTag(activeTag);
        it.infoText = i18n.tr(std::string("languages.") + activeTag, activeTag) + " / " + voice;
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.accessibility.speech_rate", "Speech Rate");
        it.description = i18n.tr("settings.accessibility.speech_rate_desc",
                                 "Adjust how fast voice guidance speaks.");
        it.type = ItemType::Slider;
        it.sliderSteps = 20;
        it.floatVal = (float)(std::clamp(screen.m_accessibilitySpeechRate, 120, 320) - 120) / 200.f;
        it.infoText = std::to_string(screen.m_accessibilitySpeechRate) + " "
                    + i18n.tr("settings.accessibility.words_per_minute", "wpm");
        it.onChange = [&screen](SettingItem& self) {
            const int rate = 120 + (int)std::round(std::clamp(self.floatVal, 0.f, 1.f) * 200.f);
            screen.m_accessibilitySpeechRate = std::clamp(rate, 120, 320);
            self.infoText = std::to_string(screen.m_accessibilitySpeechRate) + " "
                          + nxui::I18n::instance().tr("settings.accessibility.words_per_minute", "wpm");
            if (screen.m_accessibilitySpeechRateCb)
                screen.m_accessibilitySpeechRateCb(screen.m_accessibilitySpeechRate);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.accessibility.speak_hints", "Speak Action Hints");
        it.description = i18n.tr("settings.accessibility.speak_hints_desc",
                                 "Announce available actions after the selected item.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_accessibilitySpeakHints;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_accessibilitySpeakHints = self.boolVal;
            if (screen.m_accessibilitySpeakHintsCb)
                screen.m_accessibilitySpeakHintsCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.accessibility.repeat_context", "Repeat Location Every Move");
        it.description = i18n.tr("settings.accessibility.repeat_context_desc",
                                 "Repeat the current area on every focus move instead of only when it changes.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_accessibilitySpeakContextEveryFocus;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_accessibilitySpeakContextEveryFocus = self.boolVal;
            if (screen.m_accessibilitySpeakContextEveryFocusCb)
                screen.m_accessibilitySpeakContextEveryFocusCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.accessibility.speak_position", "Speak Position");
        it.description = i18n.tr("settings.accessibility.speak_position_desc",
                                 "Announce row, column, and item position when available.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_accessibilitySpeakPosition;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_accessibilitySpeakPosition = self.boolVal;
            if (screen.m_accessibilitySpeakPositionCb)
                screen.m_accessibilitySpeakPositionCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
