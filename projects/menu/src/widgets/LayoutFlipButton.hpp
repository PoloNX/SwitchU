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
    LayoutFlipButton();

    void setTheme(const nxui::Theme* theme);
    void setSingleRowActive(bool singleRow);
    /// Load white+alpha mask from disk (theme-tinted at draw time).
    void ensureIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path);

protected:
    void onContentRender(nxui::Renderer& ren) override;

private:
    void refreshAccessibility();
    void drawFallbackIcon(nxui::Renderer& ren, const nxui::Color& ink);

    const nxui::Theme* m_theme = nullptr;
    bool m_singleRow = false;
    nxui::Texture m_icon;
    bool m_iconLoadAttempted = false;
};
