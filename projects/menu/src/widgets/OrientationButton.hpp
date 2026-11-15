#pragma once

#include "AdaptiveIconLoad.hpp"
#include "core/HomeOrientation.hpp"

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/Theme.hpp>
#include <string>

namespace nxui { class Renderer; }

// Compact HOME chrome control that cycles Flip-Grip / TATE orientations.
class OrientationButton : public nxui::GlassWidget {
public:
    OrientationButton();

    void setTheme(const nxui::Theme* theme);
    void setOrientation(HomeOrientation orientation);
    /// Load icon from disk. Mono glyphs adapt to chrome; colour icons stay as-is
    /// unless colour-contrast adapt forces a black/white silhouette.
    void ensureIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path);
    void reloadIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path);
    void setColourIconContrast(bool enabled);
    void setIconHslAdjust(const switchu::icons::HslAdjust& adj);

protected:
    void onContentRender(nxui::Renderer& ren) override;
    void onUpdate(float dt) override;

private:
    void refreshAccessibility();
    void drawFallbackIcon(nxui::Renderer& ren, const nxui::Color& ink, float scale);
    void resolveDrawStyle(nxui::Color& ink, const nxui::Texture*& tex) const;

    static constexpr float kPulseDuration = 0.34f;
    float m_pulseT = kPulseDuration;
    bool  m_pulseArmed = false;

    const nxui::Theme* m_theme = nullptr;
    HomeOrientation m_orientation = HomeOrientation::Landscape0;
    nxui::Texture m_icon;
    nxui::Texture m_iconContrastMask;
    float m_iconMeanLuma = 0.5f;
    std::string m_iconPath;
    bool m_iconLoadAttempted = false;
    bool m_iconAdaptive = false;
    bool m_colourContrastAdapt = true;
    switchu::icons::HslAdjust m_hslAdjust;
};
