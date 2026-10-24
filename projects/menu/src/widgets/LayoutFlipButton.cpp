#include "LayoutFlipButton.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>

LayoutFlipButton::LayoutFlipButton() {
    setTag("layoutFlip");
    setFocusable(true);
    setActivateOnFirstTouch(true);
    setSize(52.f, 52.f);
    setMinWidth(52.f);
    setMinHeight(52.f);
    setShrink(0.f);
    setCornerRadius(26.f);
    setPadding(10.f);
    setLiquidGlassEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
    setPanelOpacity(0.90f);
    setBorderWidth(1.6f);
    setBaseColor(nxui::Color(0.20f, 0.22f, 0.28f, 0.94f));
    setBorderColor(nxui::Color::white().withAlpha(0.24f));
    setHighlightColor(nxui::Color::white().withAlpha(0.08f));
    refreshAccessibility();
}

void LayoutFlipButton::setTheme(const nxui::Theme* theme) {
    m_theme = theme;
    if (!theme)
        return;
    setBaseColor(theme->iconDefault.withAlpha(
        theme->mode == nxui::ThemeMode::Dark ? 0.92f : 0.94f));
    setBorderColor(theme->panelBorder);
    setHighlightColor(theme->panelHighlight);
}

void LayoutFlipButton::setSingleRowActive(bool singleRow) {
    if (m_singleRow == singleRow)
        return;
    m_singleRow = singleRow;
    refreshAccessibility();
}

void LayoutFlipButton::ensureIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const std::string& path) {
    if (m_iconLoadAttempted && m_iconPath == path)
        return;
    m_iconLoadAttempted = true;
    m_iconPath = path;
    m_icon = nxui::Texture();
    m_iconContrastMask = nxui::Texture();
    m_iconAdaptive = false;
    if (path.empty())
        return;
    float meanLuma = 0.5f;
    if (!switchu::icons::loadMaybeAdaptiveIcon(m_icon, gpu, ren, path,
                                               &m_iconAdaptive, 128,
                                               &meanLuma, &m_iconContrastMask,
                                               m_hslAdjust.enabled ? &m_hslAdjust : nullptr)) {
        m_iconAdaptive = false;
        return;
    }
    m_iconMeanLuma = meanLuma;
}

void LayoutFlipButton::reloadIcon(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const std::string& path) {
    m_iconLoadAttempted = false;
    m_iconAdaptive = false;
    ensureIcon(gpu, ren, path);
}

void LayoutFlipButton::setColourIconContrast(bool enabled) {
    m_colourContrastAdapt = enabled;
}

void LayoutFlipButton::setIconHslAdjust(const switchu::icons::HslAdjust& adj) {
    m_hslAdjust = adj;
}

void LayoutFlipButton::refreshAccessibility() {
    auto& i18n = nxui::I18n::instance();
    setAccessibilityLabel(i18n.tr("hint.switch_layout", "Switch view"));
    setAccessibilityRole(i18n.tr("accessibility.roles.button", "button"));
    setAccessibilityHint(m_singleRow
        ? i18n.tr("layout.flip_to_grid", "Switch to grid view.")
        : i18n.tr("layout.flip_to_line", "Switch to single-row view."));
}

void LayoutFlipButton::drawFallbackIcon(nxui::Renderer& ren, const nxui::Color& ink) {
    nxui::Rect c = glassContentRect();
    const float side = std::min(c.width, c.height);
    const float cx = c.x + c.width * 0.5f;
    const float cy = c.y + c.height * 0.5f;
    const float r = side * 0.34f;
    // Two opposing arcs with chevron heads (camera flip).
    constexpr int kSegs = 14;
    for (int pass = 0; pass < 2; ++pass) {
        const float a0 = pass == 0 ? (-40.f * 3.14159265f / 180.f)
                                   : (140.f * 3.14159265f / 180.f);
        const float a1 = pass == 0 ? (140.f * 3.14159265f / 180.f)
                                   : (320.f * 3.14159265f / 180.f);
        nxui::Vec2 prev{cx + std::cos(a0) * r, cy + std::sin(a0) * r};
        for (int i = 1; i <= kSegs; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kSegs);
            const float a = a0 + (a1 - a0) * t;
            nxui::Vec2 p{cx + std::cos(a) * r, cy + std::sin(a) * r};
            ren.drawLine(prev, p, ink, 2.4f);
            prev = p;
        }
        const float tip = side * 0.14f;
        const float ang = a1;
        const float tx = cx + std::cos(ang) * r;
        const float ty = cy + std::sin(ang) * r;
        const float ox = -std::sin(ang);
        const float oy = std::cos(ang);
        const float bx = std::cos(ang);
        const float by = std::sin(ang);
        ren.drawLine({tx, ty},
                     {tx - bx * tip + ox * tip * 0.55f, ty - by * tip + oy * tip * 0.55f},
                     ink, 2.4f);
        ren.drawLine({tx, ty},
                     {tx - bx * tip - ox * tip * 0.55f, ty - by * tip - oy * tip * 0.55f},
                     ink, 2.4f);
    }
}

void LayoutFlipButton::resolveDrawStyle(nxui::Color& ink,
                                        const nxui::Texture*& tex) const {
    const float a = opacity();
    tex = m_icon.valid() ? &m_icon : nullptr;

    if (m_iconAdaptive && m_theme) {
        ink = m_theme->textPrimary.withAlpha(0.92f * a);
        return;
    }

    // Single-row + hero: follow HOME chrome like mono glyphs.
    if (m_colourContrastAdapt && !m_iconAdaptive && m_iconContrastMask.valid() && m_theme) {
        tex = &m_iconContrastMask;
        ink = m_theme->textPrimary.withAlpha(0.92f * a);
        return;
    }

    // Custom tint is baked into m_icon via HSL adjust on load.
    ink = nxui::Color::white().withAlpha(0.92f * a);
}

void LayoutFlipButton::onContentRender(nxui::Renderer& ren) {
    nxui::Color ink = nxui::Color::white();
    const nxui::Texture* tex = nullptr;
    resolveDrawStyle(ink, tex);

    if (tex && tex->valid()) {
        nxui::Rect c = glassContentRect();
        const float edge = std::min(c.width, c.height) * 0.92f;
        const float cx = c.x + c.width * 0.5f;
        const float cy = c.y + c.height * 0.5f;
        ren.drawTexture(tex,
                        {cx - edge * 0.5f, cy - edge * 0.5f, edge, edge},
                        ink);
        return;
    }
    drawFallbackIcon(ren, ink);
}
