#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/Theme.hpp>
#include <string>

namespace nxui { class Renderer; }

// Compact camera-flip control for toggling grid ↔ single-row (touch + D-pad).
class LayoutFlipButton : public nxui::GlassWidget {
public:
    // The three views the button cycles through. Single-row has two flavours:
    // square tiles and tall SteamGridDB posters.
    enum class FlipTarget { Grid, LineSquares, LinePosters };

    LayoutFlipButton();

    void setTheme(const nxui::Theme* theme);
    /// Current view + what the next activation switches to (drives the hint).
    void setFlipTarget(FlipTarget target);
    FlipTarget flipTarget() const { return m_flipTarget; }
    /// Load white+alpha mask from disk (theme-tinted at draw time).
    /// Reloads when `path` differs from the one already loaded, so an icon
    /// pack switch is picked up without rebuilding the widget.
    void ensureIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path);

protected:
    void onContentRender(nxui::Renderer& ren) override;

private:
    void refreshAccessibility();
    void drawFallbackIcon(nxui::Renderer& ren, const nxui::Color& ink);

    const nxui::Theme* m_theme = nullptr;
    FlipTarget m_flipTarget = FlipTarget::Grid;
    nxui::Texture m_icon;
    std::string m_iconPath;
    bool m_iconLoadAttempted = false;
};
