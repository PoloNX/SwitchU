#include "AutoThemeScreen.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cstdio>

AutoThemeScreen::AutoThemeScreen()
    : TabbedOverlayScreen(ScreenMode::FolderOptions) {
    constexpr float width = 900.f;
    setRect({(1280.f - width) * 0.5f, 40.f, width, 640.f});
}

void AutoThemeScreen::configure(const State& state, std::vector<PresetOption> presets) {
    m_state = state;
    m_state.mode = std::clamp(m_state.mode, 0, 2);
    m_state.dayStartHour = std::clamp(m_state.dayStartHour, 0, 23);
    m_state.nightStartHour = std::clamp(m_state.nightStartHour, 0, 23);
    m_presets = std::move(presets);

    m_tabs.clear();
    m_cachedTabContentWidgets.clear();
    warmup();
}

int AutoThemeScreen::presetIndexOf(const std::string& id) const {
    for (std::size_t i = 0; i < m_presets.size(); ++i)
        if (m_presets[i].id == id)
            return static_cast<int>(i);
    return 0;
}

std::vector<std::string> AutoThemeScreen::presetNames() const {
    std::vector<std::string> names;
    names.reserve(m_presets.size());
    for (const auto& p : m_presets)
        names.push_back(p.name.empty() ? p.id : p.name);
    return names;
}

std::string AutoThemeScreen::modeSummary() const {
    auto& i18n = nxui::I18n::instance();
    switch (m_state.mode) {
        case 1:
            return i18n.tr("autotheme.summary.manual", "Manual hours");
        case 2:
            if (m_state.geoResolved && !m_state.geoCity.empty())
                return m_state.geoCity;
            return i18n.tr("autotheme.summary.geo", "Geolocation (IP)");
        default:
            return i18n.tr("autotheme.summary.off", "Off");
    }
}

void AutoThemeScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    m_tabs.clear();

    const bool hasPresets = !m_presets.empty();
    const std::vector<std::string> names = hasPresets
        ? presetNames()
        : std::vector<std::string>{ i18n.tr("common.none", "None") };

    auto hourOptions = []() {
        std::vector<std::string> opts;
        for (int h = 0; h < 24; ++h) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02d:00", h);
            opts.emplace_back(buf);
        }
        return opts;
    };

    Tab tab;
    tab.name = i18n.tr("autotheme.title", "Auto Theme");

    // --- Mode section ------------------------------------------------------
    {
        SettingItem section;
        section.label = i18n.tr("autotheme.section.mode", "Mode");
        section.type = ItemType::Section;
        tab.items.push_back(std::move(section));
    }
    {
        SettingItem it;
        it.label = i18n.tr("autotheme.mode", "Switching");
        it.description = i18n.tr("autotheme.mode_desc",
                                 "Off keeps your chosen theme. Manual uses fixed hours. "
                                 "Geolocation uses your approximate location (needs internet).");
        it.type = ItemType::Selector;
        it.options = {
            i18n.tr("autotheme.mode_off", "Off"),
            i18n.tr("autotheme.mode_manual", "Manual hours"),
            i18n.tr("autotheme.mode_geo", "Geolocation (IP)")
        };
        it.intVal = std::clamp(m_state.mode, 0, 2);
        // Changing the mode reshapes the rest of the window (manual hours vs
        // geolocation info), so request a deferred rebuild — done outside the
        // item iteration in onContentUpdate() to stay safe.
        it.onChange = [this](SettingItem& self) {
            m_state.mode = std::clamp(self.intVal, 0, 2);
            notifyChanged();
            requestRebuild();
        };
        tab.items.push_back(std::move(it));
    }

    // --- Themes section (only when switching is enabled) -------------------
    if (m_state.mode != 0) {
        {
            SettingItem section;
            section.label = i18n.tr("autotheme.section.themes", "Themes");
            section.type = ItemType::Section;
            tab.items.push_back(std::move(section));
        }
        {
            SettingItem it;
            it.label = i18n.tr("autotheme.day_theme", "Day Theme");
            it.description = i18n.tr("autotheme.day_theme_desc", "Applied during the day.");
            it.type = ItemType::Selector;
            it.options = names;
            it.intVal = hasPresets ? presetIndexOf(m_state.dayPreset) : 0;
            it.onChange = [this](SettingItem& self) {
                if (m_presets.empty())
                    return;
                int idx = std::clamp(self.intVal, 0, static_cast<int>(m_presets.size()) - 1);
                self.intVal = idx;
                m_state.dayPreset = m_presets[idx].id;
                notifyChanged();
            };
            tab.items.push_back(std::move(it));
        }
        {
            SettingItem it;
            it.label = i18n.tr("autotheme.night_theme", "Night Theme");
            it.description = i18n.tr("autotheme.night_theme_desc", "Applied during the night.");
            it.type = ItemType::Selector;
            it.options = names;
            it.intVal = hasPresets ? presetIndexOf(m_state.nightPreset) : 0;
            it.onChange = [this](SettingItem& self) {
                if (m_presets.empty())
                    return;
                int idx = std::clamp(self.intVal, 0, static_cast<int>(m_presets.size()) - 1);
                self.intVal = idx;
                m_state.nightPreset = m_presets[idx].id;
                notifyChanged();
            };
            tab.items.push_back(std::move(it));
        }
    }

    // --- Schedule section (manual mode only) -------------------------------
    if (m_state.mode == 1) {
        {
            SettingItem section;
            section.label = i18n.tr("autotheme.section.schedule", "Schedule");
            section.type = ItemType::Section;
            tab.items.push_back(std::move(section));
        }
        {
            SettingItem it;
            it.label = i18n.tr("autotheme.day_starts", "Day Starts At");
            it.type = ItemType::Selector;
            it.options = hourOptions();
            it.intVal = std::clamp(m_state.dayStartHour, 0, 23);
            it.onChange = [this](SettingItem& self) {
                m_state.dayStartHour = std::clamp(self.intVal, 0, 23);
                notifyChanged();
            };
            tab.items.push_back(std::move(it));
        }
        {
            SettingItem it;
            it.label = i18n.tr("autotheme.night_starts", "Night Starts At");
            it.type = ItemType::Selector;
            it.options = hourOptions();
            it.intVal = std::clamp(m_state.nightStartHour, 0, 23);
            it.onChange = [this](SettingItem& self) {
                m_state.nightStartHour = std::clamp(self.intVal, 0, 23);
                notifyChanged();
            };
            tab.items.push_back(std::move(it));
        }
    }

    // --- Geolocation info (geo mode only) ----------------------------------
    if (m_state.mode == 2) {
        {
            SettingItem section;
            section.label = i18n.tr("autotheme.section.geo", "Geolocation");
            section.type = ItemType::Section;
            tab.items.push_back(std::move(section));
        }
        {
            SettingItem it;
            it.label = i18n.tr("autotheme.geo_location", "Location");
            it.type = ItemType::Info;
            it.infoText = (m_state.geoResolved && !m_state.geoCity.empty())
                ? m_state.geoCity
                : i18n.tr("autotheme.geo_locating", "Locating via internet...");
            tab.items.push_back(std::move(it));
        }
        {
            SettingItem it;
            it.label = i18n.tr("autotheme.geo_sunrise", "Sunrise");
            it.type = ItemType::Info;
            it.infoText = m_state.geoSunrise.empty() ? "--:--" : m_state.geoSunrise;
            tab.items.push_back(std::move(it));
        }
        {
            SettingItem it;
            it.label = i18n.tr("autotheme.geo_sunset", "Sunset");
            it.type = ItemType::Info;
            it.infoText = m_state.geoSunset.empty() ? "--:--" : m_state.geoSunset;
            tab.items.push_back(std::move(it));
        }
    }

    m_tabs.push_back(std::move(tab));

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());
    rebuildTabBar();
    rebuildContentItems();
}

void AutoThemeScreen::onContentUpdate(float dt) {
    TabbedOverlayScreen::onContentUpdate(dt);
    if (m_pendingRebuild) {
        m_pendingRebuild = false;
        buildTabs();
        clampContentIdx();
    }
}

void AutoThemeScreen::updateGeoDisplay(bool resolved, std::string city,
                                       std::string sunrise, std::string sunset) {
    m_state.geoResolved = resolved;
    m_state.geoCity = std::move(city);
    m_state.geoSunrise = std::move(sunrise);
    m_state.geoSunset = std::move(sunset);
    if (m_state.mode == 2)
        requestRebuild();
}

void AutoThemeScreen::drawOverlayHeader(nxui::Renderer& ren,
                                        const nxui::Rect& panel,
                                        float opacity) {
    if (!m_theme || !m_font)
        return;

    auto& i18n = nxui::I18n::instance();
    const float textX = panel.x + 34.f;

    ren.drawText(i18n.tr("autotheme.header", "Automatic Theme"),
                 {textX, panel.y + 34.f}, m_font,
                 m_theme->textPrimary.withAlpha(opacity), 1.f);

    if (m_smallFont) {
        const std::string subtitle =
            i18n.tr("autotheme.header_mode", "Mode: ") + modeSummary();
        ren.drawText(subtitle, {textX, panel.y + 78.f}, m_smallFont,
                     m_theme->textSecondary.withAlpha(opacity), 0.82f);
    }
}
