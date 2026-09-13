#pragma once
#include "core/AppLayoutMode.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct AppConfig {
    bool  musicEnabled = true;
    float musicVolume  = 0.4f;
    float sfxVolume    = 0.7f;
    int   gridColumns  = 5;
    int   gridRows     = 3;
    AppLayoutMode appLayoutMode = AppLayoutMode::Grid;
    std::string actionHintStyle = "capsules";
    // 0 = animated translation, 1 = immediate jump.
    int cursorMotionMode = 0;
    std::string uiLanguageOverride = "auto";
    std::string soundPreset = "wiiu";
    bool  defaultProfileEnabled = false;
    std::string defaultProfileUid;
    bool  tutorialCompleted = false;
    bool  clockUse12Hour = false;
    bool  accessibilityEnabled = true;
    bool  accessibilitySpeakHints = true;
    bool  accessibilitySpeakContextEveryFocus = false;
    bool  accessibilitySpeakPosition = true;
    int   accessibilitySpeechRate = 190;
    bool  steamGridDbEnabled = true;
    std::string steamGridDbApiKey;

    // 0 keeps the hand-made layout. The other modes are display-only
    // projections and never overwrite layout.json.
    int sortMode = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> lastOpened;
    std::uint64_t lastOpenedSequence = 0;

    std::uint64_t lastOpenedAt(std::uint64_t titleId) const {
        for (const auto& entry : lastOpened)
            if (entry.first == titleId) return entry.second;
        return 0;
    }
    void noteOpened(std::uint64_t titleId) {
        for (const auto& entry : lastOpened)
            lastOpenedSequence = std::max(lastOpenedSequence, entry.second);
        if (lastOpenedSequence != std::numeric_limits<std::uint64_t>::max())
            ++lastOpenedSequence;
        for (auto& entry : lastOpened) {
            if (entry.first == titleId) {
                entry.second = lastOpenedSequence;
                return;
            }
        }
        lastOpened.emplace_back(titleId, lastOpenedSequence);
    }

    std::string themePreset = "Default Light";
    // See switchu::folders::kFolderStyle*. Applies to every folder tile.
    int folderStyle = 0;
    // First-game icon overlay. Ignored by Classic (the mosaic is the cover).
    bool folderShowCover = false;

    bool load();

    bool save() const;

    static constexpr const char* kConfigDir  = "sdmc:/config/SwitchU";
    static constexpr const char* kConfigPath = "sdmc:/config/SwitchU/settings.json";
    // The copy that was current before the last save. Read only when the live
    // settings file is missing or does not parse.
    static constexpr const char* kBackupPath = "sdmc:/config/SwitchU/settings.json.bak";
};
