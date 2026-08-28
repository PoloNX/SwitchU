#pragma once

#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/widgets/Widget.hpp>

#include <string>

namespace switchu::manager {

class UpdateProgressDialog final : public nxui::Widget {
public:
    UpdateProgressDialog();

    void setFonts(nxui::Font* titleFont, nxui::Font* bodyFont) {
        m_titleFont = titleFont;
        m_bodyFont = bodyFont;
    }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setLabels(const std::string& downloadLabel, const std::string& installLabel) {
        m_downloadLabel = downloadLabel;
        m_installLabel = installLabel;
    }
    void show(const std::string& title);
    void setProgress(float downloadProgress, float installProgress,
                     const std::string& status);
    void hide();
    bool isActive() const { return m_active; }

protected:
    void onRender(nxui::Renderer& renderer) override;

private:
    void drawProgressBar(nxui::Renderer& renderer, const nxui::Rect& track,
                         const std::string& label, float progress) const;

    nxui::Font* m_titleFont = nullptr;
    nxui::Font* m_bodyFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    std::string m_title;
    std::string m_status;
    std::string m_downloadLabel = "Download";
    std::string m_installLabel = "Installation";
    float m_downloadProgress = 0.f;
    float m_installProgress = 0.f;
    bool m_active = false;
};

} // namespace switchu::manager
