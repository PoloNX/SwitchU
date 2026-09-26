#pragma once

#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/Theme.hpp>

#include <memory>
#include <string>

// Large HOME tile showing which profile games launch with. Activating it
// opens the full-screen profile carousel.
class ProfileLockButton : public nxui::GlassWidget {
public:
    ProfileLockButton();

    void setTheme(const nxui::Theme* theme);
    void setLockedProfile(std::shared_ptr<nxui::Texture> avatar, const std::string& nickname);
    void clearLockedProfile();

    bool hasLockedProfile() const { return m_locked; }
    // Text for the title pill and screen reader.
    const std::string& statusText() const { return m_statusText; }

protected:
    void onContentRender(nxui::Renderer& ren) override;

private:
    void refreshAccessibility();

    const nxui::Theme* m_theme = nullptr;
    std::shared_ptr<nxui::Texture> m_avatar;
    std::string m_nickname;
    std::string m_statusText;
    bool m_locked = false;
};
