#pragma once

#include "TabbedOverlayScreen.hpp"

#include <functional>
#include <string>
#include <vector>

// Dedicated overlay for configuring automatic day/night theme switching. Opened
// from the Theme Shop options as a single entry, it gathers every auto-theme
// setting in one focused window instead of scattering them across the options
// list.
class AutoThemeScreen final : public TabbedOverlayScreen {
public:
    struct PresetOption {
        std::string id;
        std::string name;
    };

    struct State {
        int         mode = 0;            // 0 = off, 1 = manual hours, 2 = geolocation
        std::string dayPreset;           // preset id used during the day
        std::string nightPreset;         // preset id used during the night
        int         dayStartHour = 7;    // manual boundary [0..23]
        int         nightStartHour = 19; // manual boundary [0..23]
        bool        geoResolved = false; // whether an IP position has been resolved
        std::string geoCity;             // resolved-location hint
        std::string geoSunrise;          // formatted sunrise for the geo section
        std::string geoSunset;           // formatted sunset for the geo section
    };

    AutoThemeScreen();

    // Seed the screen with the current state and the list of selectable presets,
    // then (re)build the content.
    void configure(const State& state, std::vector<PresetOption> presets);

    // Update only the geolocation display fields (resolved location + computed
    // sunrise/sunset) and refresh the content without disturbing the rest.
    void updateGeoDisplay(bool resolved, std::string city,
                          std::string sunrise, std::string sunset);

    const State& state() const { return m_state; }
    void onChanged(std::function<void(const State&)> cb) { m_changedCb = std::move(cb); }

protected:
    void buildTabs() override;
    void onContentUpdate(float dt) override;
    float overlayHeaderHeight() const override { return 118.f; }
    float overlayTabWidth() const override { return 240.f; }
    void drawOverlayHeader(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) override;

private:
    int presetIndexOf(const std::string& id) const;
    std::vector<std::string> presetNames() const;
    std::string modeSummary() const;
    void notifyChanged() { if (m_changedCb) m_changedCb(m_state); }
    void requestRebuild() { m_pendingRebuild = true; }

    State m_state;
    std::vector<PresetOption> m_presets;
    std::function<void(const State&)> m_changedCb;
    bool m_pendingRebuild = false;
};
