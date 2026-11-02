#pragma once

#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
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
    void show(const std::string& title);
    void setProgress(float overallProgress, const std::string& status,
                     bool cancellable = false);
    void hide();
    bool isActive() const { return m_active; }
    bool isCancellable() const { return m_active && m_cancellable; }
    bool consumeCancelRequest();
    void handleInput(nxui::Input& input);

protected:
    void onRender(nxui::Renderer& renderer) override;

private:
    nxui::Rect cancelRect() const;

    nxui::Font* m_titleFont = nullptr;
    nxui::Font* m_bodyFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    std::string m_title;
    std::string m_status;
    float m_overallProgress = 0.f;
    bool m_cancellable = false;
    bool m_cancelFocused = false;
    bool m_cancelRequested = false;
    bool m_touchOnCancel = false;
    // Ignore A/B until released — Install confirm uses the same A press.
    bool m_awaitButtonRelease = false;
    bool m_active = false;
};

} // namespace switchu::manager
