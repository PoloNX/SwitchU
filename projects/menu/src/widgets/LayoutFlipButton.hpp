#pragma once

#include "AdaptiveIconLoad.hpp"

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/Theme.hpp>
#include <string>

namespace nxui { class Renderer; }

// Compact camera-flip control for toggling grid Γåö single-row (touch + D-pad).
class LayoutFlipButton : public nxui::GlassWidget {
public:
    LayoutFlipButton();

    void setTheme(const nxui::Theme* theme);
    void setSingleRowActive(bool singleRow);
    /// Load icon from disk. Mono glyphs adapt to chrome; colour icons stay as-is
    /// unless colour-contrast adapt forces a black/white silhouette.
    void ensureIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path);
    /// Force a reload (icon pack switch).
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

    // Short glyph pulse when the view actually flips, mirroring the tile morph.
    static constexpr float kPulseDuration = 0.34f;
    float m_pulseT = kPulseDuration;
    bool  m_pulseArmed = false;

    const nxui::Theme* m_theme = nullptr;
    bool m_singleRow = false;
    nxui::Texture m_icon;
    nxui::Texture m_iconContrastMask;
    float m_iconMeanLuma = 0.5f;
    std::string m_iconPath;
    bool m_iconLoadAttempted = false;
    bool m_iconAdaptive = false;
    bool m_colourContrastAdapt = true;
    switchu::icons::HslAdjust m_hslAdjust;
};
