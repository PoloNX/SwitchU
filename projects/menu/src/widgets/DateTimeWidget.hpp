#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Types.hpp>
#include <string>


class DateTimeWidget : public nxui::GlassWidget {
public:
    DateTimeWidget() = default;
    void setFont(nxui::Font* f) { m_font = f; }
    void setSmallFont(nxui::Font* sf) { m_smallFont = sf; }
    void setTextColor(const nxui::Color& c) { m_textColor = c; }
    void setSecondaryTextColor(const nxui::Color& c) { m_secondaryColor = c; }
    void setUse12HourClock(bool enabled);

protected:
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;

private:
    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    float m_timer = 0.f;
    std::string m_timeStr;
    std::string m_dateStr;
    bool m_use12HourClock = false;
    nxui::Color m_textColor      {1.f, 1.f, 1.f, 1.f};
    nxui::Color m_secondaryColor {0.7f, 0.7f, 0.8f, 0.8f};
};
