#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Types.hpp>
#include <string>



class AppletButton : public nxui::GlassWidget {
public:
    AppletButton();
    ~AppletButton() override;

    void setIcon(nxui::Texture* tex)        { m_icon = tex; }
    void setIconCircular(bool enabled)      { m_iconCircular = enabled; }
    // When true, chrome tint (textPrimary) is applied; colour icons leave this
    // false so multiply tint stays white (identity).
    void setIconAdaptive(bool adaptive) { m_iconAdaptive = adaptive; syncIconTint(); }
    void setIconChromeTint(const nxui::Color& tint) {
        m_iconChromeTint = tint;
        syncIconTint();
    }
    // Temporary override while a colourful WebP frame is showing.
    void setIconUsesColorArtwork(bool colorArt) {
        m_iconUsesColorArtwork = colorArt;
        syncIconTint();
    }
    // Colour-pack contrast adapt (Theme > Options). Mask is white×alpha twin.
    void setIconMeanLuma(float luma) { m_iconMeanLuma = luma; syncIconTint(); }
    void setIconContrastMask(nxui::Texture* mask) {
        m_iconContrastMask = mask;
        syncIconTint();
    }
    // When true (single-row + hero), colour icons follow chrome textPrimary —
    // same hero-aware light/dark flip as the rest of HOME.
    void setColourContrastAdapt(bool enabled) {
        m_colourContrastAdapt = enabled;
        syncIconTint();
    }
    // Custom tint is baked into the glyph texture on reload; this flag is only
    // used so chrome-follow can still win while a hero is up.
    void setIconCustomTintEnabled(bool enabled) {
        m_iconCustomTint = enabled;
        syncIconTint();
    }
    bool iconAdaptive() const { return m_iconAdaptive; }
    nxui::Texture* icon() const             { return m_icon; }

    void setLabel(const std::string& l);
    void setLabelKey(const std::string& key, const std::string& fallback = "");
    const std::string& label() const    { return m_label; }

    bool hitTest(float sx, float sy) const { return m_rect.contains(sx, sy); }

protected:
    void onContentRender(nxui::Renderer& ren) override;

private:
    void refreshLocalizedLabel();
    void syncIconTint();

    nxui::Texture* m_icon = nullptr;
    nxui::Texture* m_iconContrastMask = nullptr;
    nxui::Color    m_iconTint = nxui::Color::white();
    nxui::Color    m_iconChromeTint = nxui::Color::white();
    float          m_iconMeanLuma = 0.5f;
    bool           m_iconAdaptive = false;
    bool           m_iconUsesColorArtwork = false;
    bool           m_colourContrastAdapt = false;
    bool           m_iconCustomTint = false;
    bool           m_drawContrastMask = false;
    bool           m_iconCircular = false;
    std::string    m_label;
    std::string    m_labelSource;
    std::string    m_labelKey;
    std::string    m_labelFallback;
    bool           m_useLabelKey = false;
    int            m_i18nListenerId = -1;
};
