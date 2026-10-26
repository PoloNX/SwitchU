#pragma once
#include "core/AppLayoutMode.hpp"
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct AppConfig {
    bool  musicEnabled = true;
    float musicVolume  = 0.4f;
    float sfxVolume    = 0.7f;
    int   musicTrackIndex = 0;
    // Stable id ("track.mp3" or "Album/track.mp3"). Preferred over musicTrackIndex
    // when users add/remove/reorder files between boots.
    std::string musicTrackKey;
    float musicPositionSeconds = 0.f;
    bool  musicShuffle = false;
    int   musicRepeatMode = 1; // MusicRepeatMode::All
    std::vector<std::string> musicPlaylistOrder;
    int   gridColumns  = 5;
    int   gridRows     = 3;
    AppLayoutMode appLayoutMode = AppLayoutMode::Grid;
    std::string actionHintStyle = "capsules";
    // 0 = animated translation, 1 = immediate jump.
    int cursorMotionMode = 0;
    // 0 = pages slide sideways, 1 = pages swap instantly.
    int pageTransitionMode = 0;
    std::string uiLanguageOverride = "auto";
    std::string soundPreset = "wiiu";
    bool  defaultProfileEnabled = false;
    std::string defaultProfileUid;
    bool  tutorialCompleted = false;
    bool  clockUse12Hour = false;
    // Alternate to Nintendo auto clock correction: HTTPS Date sync on boot.
    bool  webClockSyncEnabled = false;
    bool  accessibilityEnabled = true;
    bool  accessibilitySpeakHints = true;
    bool  accessibilitySpeakContextEveryFocus = false;
    bool  accessibilitySpeakPosition = true;
    int   accessibilitySpeechRate = 190;
    bool  steamGridDbEnabled = true;
    // Sub-toggles used when steamGridDbEnabled is on. Defaults keep prior
    // behaviour (artwork everywhere the focused title allows).
    bool  steamGridDbShowInGrid = true;
    bool  steamGridDbShowInDynamicLine = true;
    bool  steamGridDbShowInFolders = true;
    // Tall SteamGridDB portrait covers (Poster) on the single-row carousel only.
    // Settings → SteamGridDB toggle; Change View stays grid ↔ single-row.
    bool  steamGridDbShowPosterInDynamicLine = true;
    // Logo overlay above / beside the single-row carousel.
    bool  steamGridDbShowLogoInDynamicLine = true;
    std::string steamGridDbApiKey;

    // 0 keeps the hand-made layout. The other modes are display-only
    // projections and never overwrite layout.json.
    int sortMode = 0;
    // A-Z / Recent also reorder applications that use a larger tile size.
    bool sortLargeTiles = true;
    // Colour HOME chrome icons follow single-row hero light/dark chrome when on
    // (Theme > Options). Default on — matches pre-PoloNX enjoyment.
    bool iconContrastAdapt = true;
    // Theme Shop → Options: applying a theme with bundled music.
    // Override on = replace the current playlist (legacy). Off = keep the
    // user's library; optionally append theme tracks and/or start them.
    bool themeMusicOverride = true;
    bool themeMusicAddToPlaylist = true;
    bool themeMusicPlayOnApply = true;
    // Per-pack Photoshop-style HSL adjust (0.5 pivots = leave original) plus
    // Solid / Grayscale presentation toggles. matchTheme keeps pivots synced
    // to the active theme accent until the user edits HSL or turns it off.
    struct IconPackTint {
        bool enabled = false;
        bool flatten = false;
        bool grayscale = false;
        bool matchTheme = false;
        float hue = 0.5f;
        float sat = 0.5f;
        float light = 0.5f;
    };
    std::unordered_map<std::string, IconPackTint> iconPackTints;

    IconPackTint iconPackTintFor(const std::string& packId) const {
        auto it = iconPackTints.find(packId.empty() ? "default" : packId);
        if (it != iconPackTints.end())
            return it->second;
        return {};
    }
    void setIconPackTint(const std::string& packId, const IconPackTint& tint) {
        const std::string id = packId.empty() ? "default" : packId;
        if (!tint.enabled && !tint.flatten && !tint.grayscale && !tint.matchTheme
            && std::fabs(tint.hue - 0.5f) < 0.0005f
            && std::fabs(tint.sat - 0.5f) < 0.0005f
            && std::fabs(tint.light - 0.5f) < 0.0005f) {
            iconPackTints.erase(id);
            return;
        }
        iconPackTints[id] = tint;
    }
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
    // HOME sidebar glyph pack under romfs:/icons/<id>/.
    // Missing files fall back to the "default" pack.
    std::string iconPack = "default";
    // See switchu::folders::kFolderStyle*. Applies to every folder tile.
    int folderStyle = 0;
    // First-game icon overlay. Ignored by Classic (the mosaic is the cover).
    bool folderShowCover = false;
    // Layout when opening a folder: 0 = Auto (keep current), 1 = Grid,
    // 2 = Single-row. Does not change the HOME layout preference on its own.
    int folderDefaultView = 0;

    // Automatic day/night theme switching.
    std::string autoThemeMode = "off";       // "off" | "manual" | "geo"
    std::string autoThemeDayPreset;          // preset id/name used during the day
    std::string autoThemeNightPreset;        // preset id/name used during the night
    int         autoThemeDayStartHour = 7;   // manual boundary [0..23]
    int         autoThemeNightStartHour = 19;// manual boundary [0..23]
    // Cached IP-geolocated position for the geolocation mode.
    bool        autoThemeGeoResolved = false;
    double      autoThemeGeoLat = 0.0;       // degrees, north positive
    double      autoThemeGeoLon = 0.0;       // degrees, east positive
    std::string autoThemeGeoCity;            // human-readable resolved location

    bool load();

    bool save() const;

    static constexpr const char* kConfigDir  = "sdmc:/config/SwitchU";
    static constexpr const char* kConfigPath = "sdmc:/config/SwitchU/settings.json";
    // The copy that was current before the last save. Read only when the live
    // settings file is missing or does not parse.
    static constexpr const char* kBackupPath = "sdmc:/config/SwitchU/settings.json.bak";
};
