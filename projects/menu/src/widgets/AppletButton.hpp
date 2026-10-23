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
    nxui::Color    m_iconTint = nxui::Color::white();
    nxui::Color    m_iconChromeTint = nxui::Color::white();
    bool           m_iconAdaptive = false;
    bool           m_iconUsesColorArtwork = false;
    bool           m_iconCircular = false;
    std::string    m_label;
    std::string    m_labelSource;
    std::string    m_labelKey;
    std::string    m_labelFallback;
    bool           m_useLabelKey = false;
    int            m_i18nListenerId = -1;
};

