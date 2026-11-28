#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>

SettingsScreen::Tab settings::tabs::AudioTab::build(SettingsScreen& screen) {
    (void)screen;
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.audio", "Audio");

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
        // OS shows a loud-headphone warning until the user acknowledges it
        // (count > 0). Expose as a toggle so you can reset it to show again.
        SettingItem it;
        it.label = i18n.tr("settings.audio.headphone_warning", "Headphone Volume Warning");
        it.description = i18n.tr(
            "settings.audio.headphone_warning_desc",
            "Warn when headphone volume is high. Turn off after you acknowledge it, or turn on to show the warning again.");
        it.type = ItemType::Toggle;
        u32 count = 0;
        setsysGetHeadphoneVolumeWarningCount(&count);
        // On = warning still active (not yet acknowledged).
        it.boolVal = (count == 0);
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetHeadphoneVolumeWarningCount(self.boolVal ? 0u : 1u);
        };
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
