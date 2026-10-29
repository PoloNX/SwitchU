#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/Theme.hpp>

class PageIndicator : public nxui::GlassWidget {
public:
    PageIndicator();

    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setPageCount(int total);
    void setCurrentPage(int page);

    // Folder view tints the active dot with the folder colour.
    void setActiveColor(const nxui::Color& c) { m_activeColor = c; m_hasActiveColor = true; }
    void clearActiveColor() { m_hasActiveColor = false; }

protected:
    void onContentRender(nxui::Renderer& ren) override;
    void onUpdate(float dt) override;
    nxui::Vec2 computeContentSize() const override;

private:
    void updateGeometryFromContent();

    // Soft pop on the newly active dot when the page changes, so the pill
    // echoes the sliding page instead of silently re-colouring.
    static constexpr float kDotPopDuration = 0.30f;
    float m_popT = kDotPopDuration;

    int m_total   = 1;
    int m_current = 0;
    nxui::Color m_activeColor{};
    bool m_hasActiveColor = false;
    float m_layoutWidth = 1280.f;
    bool m_geometryReady = false;
    const nxui::Theme* m_theme = nullptr;
};
