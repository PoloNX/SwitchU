#include "TabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <algorithm>

SettingsScreen::Tab settings::tabs::MusicTab::build(SettingsScreen& screen) {
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.music", "Music");

    if (!screen.m_soundPresets.empty()) {
        SettingItem it;
        it.label = i18n.tr("settings.music.sound_preset", "Sound Preset");
        it.type = ItemType::Selector;
        it.options = screen.m_soundPresets;
        it.intVal = 0;
        for (int i = 0; i < (int)screen.m_soundPresets.size(); ++i) {
            if (screen.m_soundPresets[i] == screen.m_soundPreset) {
                it.intVal = i;
                break;
            }
        }
        it.onChange = [&screen](SettingItem& self) {
            if (self.intVal >= 0 && self.intVal < (int)screen.m_soundPresets.size()) {
                screen.m_soundPreset = screen.m_soundPresets[self.intVal];
                if (screen.m_soundPresetCb) screen.m_soundPresetCb(screen.m_soundPreset);
            }
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.music.menu_music", "Menu Music"); it.type = ItemType::Toggle;
        it.boolVal = screen.m_musicEnabled;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_musicEnabled = self.boolVal;
            if (screen.m_musicEnabledCb) screen.m_musicEnabledCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.music.music_volume", "Music Volume"); it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_musicVolume, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_musicVolume = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_musicVolumeCb) screen.m_musicVolumeCb(screen.m_musicVolume);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.music.sfx_volume", "SFX Volume"); it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_sfxVolume, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_sfxVolume = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_sfxVolumeCb) screen.m_sfxVolumeCb(screen.m_sfxVolume);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it; it.label = i18n.tr("settings.music.next_track", "Next Track"); it.type = ItemType::Action;
        it.anim01 = 0.f;
        it.onChange = [&screen, &i18n](SettingItem&) {
            if (screen.m_nextTrackCb) screen.m_nextTrackCb();
            screen.requestToast(i18n.tr("settings.music.next_track", "Next Track"), 3.0f);
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
