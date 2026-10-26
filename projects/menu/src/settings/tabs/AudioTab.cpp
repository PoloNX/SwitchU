#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>
#include <cmath>

SettingsScreen::Tab settings::tabs::AudioTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.audio", "Audio");

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.menu_music", "Menu Music");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_musicEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_musicEnabled = self.boolVal;
            if (screen.m_musicEnabledCb) screen.m_musicEnabledCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.music_volume", "Music Volume");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_musicVolume, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_musicVolume = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_musicVolumeCb) screen.m_musicVolumeCb(screen.m_musicVolume);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.music.sfx_volume", "SFX Volume");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_sfxVolume, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_sfxVolume = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_sfxVolumeCb) screen.m_sfxVolumeCb(screen.m_sfxVolume);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.audio.speaker_auto_mute", "Speaker Auto-Mute"); it.type = ItemType::Toggle;
        it.description = i18n.tr("settings.audio.speaker_auto_mute_desc", "Mute speakers automatically when headphones are connected.");
        bool val = false;
        setsysGetSpeakerAutoMuteFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetSpeakerAutoMuteFlag(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.audio.headphone_warning", "Headphone Volume Warning"); it.type = ItemType::Info;
        u32 count = 0;
        setsysGetHeadphoneVolumeWarningCount(&count);
        it.infoText = count > 0 ? i18n.tr("settings.audio.acknowledged", "Acknowledged")
                                : i18n.tr("common.active", "Active");
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.audio.tv_audio_output", "TV Audio Output"); it.type = ItemType::Selector;
        it.description = i18n.tr("settings.audio.tv_audio_output_desc", "Select audio format sent to TV over HDMI.");
        it.options = {
            i18n.tr("settings.audio.mono", "Mono"),
            i18n.tr("settings.audio.stereo", "Stereo"),
            i18n.tr("settings.audio.surround", "Surround")
        };

        SetSysAudioOutputMode mode = (SetSysAudioOutputMode)1;
        setsysGetAudioOutputMode(SetSysAudioOutputModeTarget_Unknown0, &mode);
        it.intVal = std::clamp((int)mode, 0, 2);
        it.onChange = [](SettingItem& self) {
            setsysSetAudioOutputMode(SetSysAudioOutputModeTarget_Unknown0,
                                     (SetSysAudioOutputMode)self.intVal);
        };
        t.items.push_back(std::move(it));
    }



    return t;
}
