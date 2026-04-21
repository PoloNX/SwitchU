#include "GameInfoPanel.hpp"
#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include <switch.h>
#ifndef SWITCHU_HOMEBREW
#include <nxtc.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace {

std::string formatBytes(uint64_t bytes) {
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        double value = bytes / (1024.0 * 1024.0 * 1024.0);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f GB", value);
        return buf;
    }
    if (bytes >= 1024ull * 1024ull) {
        double value = bytes / (1024.0 * 1024.0);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f MB", value);
        return buf;
    }
    if (bytes >= 1024ull) {
        double value = bytes / 1024.0;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f KB", value);
        return buf;
    }
    return std::to_string(bytes) + " B";
}

uint64_t queryApplicationSize(uint64_t titleId) {
    NsApplicationOccupiedSize occ{};
    if (R_FAILED(nsCalculateApplicationOccupiedSize(titleId, &occ)))
        return 0;

    uint64_t totalSize = 0;
    for (size_t offset = 0; offset + sizeof(uint64_t) <= sizeof(occ.unk_x0); offset += sizeof(uint64_t)) {
        uint64_t value = 0;
        std::memcpy(&value, &occ.unk_x0[offset], sizeof(value));
        if (value > 0 && value < (1ull << 40)) {
            totalSize += value;
        }
    }

    return totalSize;
}

float easeOutCubic(float t) {
    float f = t - 1.f;
    return f * f * f + 1.f;
}

float easeInOutQuad(float t) {
    return t < 0.5f ? 2.f * t * t : -1.f + (4.f - 2.f * t) * t;
}

}

GameInfoPanel::GameInfoPanel() {
    setTag("game_info_panel");
    setFocusable(true);
    setFocusable(true);
    setVisible(false);
}

GameInfoPanel::~GameInfoPanel() {
}

void GameInfoPanel::setGame(GlossyIcon* icon) {
    if (!icon) return;

    m_title = icon->title();
    m_iconTex = icon->texture();
    uint64_t titleId = icon->titleId();
    uint64_t size = queryApplicationSize(titleId);
    m_sizeText = formatBytes(size);

    // Get version
    NsApplicationControlData controlData;
    size_t controlSize;
    m_version = "Unknown";
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, titleId, &controlData, sizeof(controlData), &controlSize))) {
        if (controlData.nacp.display_version[0] != '\0') {
            m_version = controlData.nacp.display_version;
        }
    }

    rebuildContent();
    setupActions();
}

void GameInfoPanel::setupActions() {
    clearActions();
    addAction(static_cast<uint64_t>(nxui::Button::A), []() {
        // Consume A while panel is active so underlying UI does not react.
    });
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() { onPressB(); });
    addAction(static_cast<uint64_t>(nxui::Button::Plus), []() {
        // Consume Plus while panel is active.
    });
    addAction(static_cast<uint64_t>(nxui::Button::X), []() {
        // Consume X while panel is active.
    });
    addAction(static_cast<uint64_t>(nxui::Button::Minus), []() {
        // Consume Minus while panel is active.
    });
    addDirectionAction(nxui::FocusDirection::LEFT, []() {
        // Block navigation while panel is open.
    });
    addDirectionAction(nxui::FocusDirection::RIGHT, []() {
        // Block navigation while panel is open.
    });
    addDirectionAction(nxui::FocusDirection::UP, []() {
        // Block navigation while panel is open.
    });
    addDirectionAction(nxui::FocusDirection::DOWN, []() {
        // Block navigation while panel is open.
    });
}

void GameInfoPanel::onPressB() {
    if (!m_active || m_animating) return;
    if (m_closeSfxCb) m_closeSfxCb();
    hide();
}

void GameInfoPanel::show() {
    if (m_active) return;
    m_showing = true;
    m_animating = true;
    m_animT = 0.f;
    m_bgAnimT = 0.f;
    m_active = true;
    setVisible(true);
}

void GameInfoPanel::hide() {
    if (!m_active) return;
    m_showing = false;
    m_animating = true;
    m_animT = 1.f;
}

void GameInfoPanel::update(float dt) {
    if (!m_animating) {
        m_bgAnimT += dt;
        return;
    }

    m_animT += dt / kAnimDuration;
    m_bgAnimT += dt;
    
    if (m_animT >= 1.f) {
        m_animT = 1.f;
        m_animating = false;
        if (!m_showing) {
            m_active = false;
            setVisible(false);
            if (m_closedCb) m_closedCb();
        }
    }
}

std::string GameInfoPanel::truncateText(const std::string& text, float maxWidth) {
    if (!m_font) return text;
    
    nxui::Vec2 textSize = m_font->measure(text);
    if (textSize.x <= maxWidth) return text;
    
    std::string result = text;
    const std::string ellipsis = "...";
    nxui::Vec2 ellipsisSize = m_font->measure(ellipsis);
    
    while (!result.empty() && m_font->measure(result + ellipsis).x > maxWidth) {
        result.pop_back();
    }
    
    return result + ellipsis;
}

void GameInfoPanel::renderBackgroundEffect(nxui::Renderer& ren, const nxui::Rect& panel, float animScale) {
    float easeScale = easeOutCubic(animScale);
    ren.drawRect({0, 0, 1280, 720}, nxui::Color(0.f, 0.f, 0.f, 0.72f * animScale));

    nxui::Color accentBase = m_theme ? m_theme->panelHighlight : nxui::Color(0.25f, 0.55f, 1.f, 1.f);
    nxui::Color panelBase(0.08f, 0.10f, 0.18f, 0.96f);
    nxui::Color panelInner(0.10f, 0.14f, 0.26f, 0.88f);
    nxui::Color lineGlow(1.f, 1.f, 1.f, 0.14f);

    nxui::Rect outerPanel = {
        std::round(panel.x),
        std::round(panel.y),
        std::round(panel.width),
        std::round(panel.height)
    };

    // Main panel body
    ren.drawRoundedRect(outerPanel, panelBase, kPanelRadius);
    nxui::Rect innerPanel = outerPanel;
    innerPanel.x += 2.f;
    innerPanel.y += 2.f;
    innerPanel.width -= 4.f;
    innerPanel.height -= 4.f;
    ren.drawRoundedRect(innerPanel, panelInner, kPanelRadius - 2.f);

    // Crisp border with subtle glow
    ren.drawRoundedRectOutline(outerPanel, accentBase.withAlpha(0.55f * animScale), kPanelRadius, 2.f);
    ren.drawRoundedRectOutline(innerPanel, accentBase.withAlpha(0.25f), kPanelRadius - 2.f, 1.f);

    // Large PS5-inspired accent beams
    float beamH = outerPanel.height * 0.64f;
    float beamY = outerPanel.y + outerPanel.height * 0.18f;
    ren.drawRoundedRect({outerPanel.x + 28.f, beamY, 18.f, beamH}, accentBase.withAlpha(0.09f), 9.f);
    ren.drawRoundedRect({outerPanel.x + 68.f, beamY + 18.f, 12.f, beamH * 0.78f}, accentBase.withAlpha(0.07f), 6.f);
    ren.drawRoundedRect({outerPanel.x + outerPanel.width - 30.f, outerPanel.y + outerPanel.height * 0.22f, 10.f, beamH * 0.56f}, accentBase.withAlpha(0.06f), 5.f);

    // Soft upper band and underline
    ren.drawGradientRect({outerPanel.x + 20.f, outerPanel.y + 12.f, outerPanel.width - 40.f, 4.f},
                        accentBase.withAlpha(0.45f * animScale),
                        nxui::Color(accentBase.r, accentBase.g, accentBase.b, 0.f));
    ren.drawRoundedRect({outerPanel.x + 18.f, outerPanel.y + outerPanel.height - 16.f, outerPanel.width - 36.f, 6.f},
                        accentBase.withAlpha(0.08f + 0.04f * std::sin(m_bgAnimT * 2.1f)), 3.f);

    // Fine moving streaks for PS5-like motion
    auto drawStreak = [&](float x, float y, float h, float alpha, float phase) {
        ren.drawRect({x, y, 2.f, h}, accentBase.withAlpha(alpha));
        ren.drawRect({x + 5.f, y + std::fmod(phase + 8.f, 12.f), 1.f, h * 0.5f}, lineGlow.withAlpha(alpha * 0.4f));
    };
    drawStreak(outerPanel.x + 36.f, outerPanel.y + 34.f, beamH - 56.f, 0.24f, std::sin(m_bgAnimT * 1.2f) * 6.f);
    drawStreak(outerPanel.x + 52.f, outerPanel.y + 52.f, beamH - 72.f, 0.16f, std::cos(m_bgAnimT * 1.5f) * 6.f);

    // Subtle wave details inside the panel
    const int segments = 18;
    float waveBaseY = outerPanel.y + outerPanel.height * 0.40f;
    float waveWidth = outerPanel.width - 72.f;
    float waveStartX = outerPanel.x + 40.f;
    for (int i = 0; i < 3; ++i) {
        float phase = m_bgAnimT * (0.9f + i * 0.2f) + i * 1.2f;
        float alpha = 0.10f - i * 0.02f;
        float amplitude = 12.f - i * 3.f;
        float y = waveBaseY + i * 22.f;
        float prevX = waveStartX;
        float prevY = y + std::sin(phase) * amplitude;
        for (int j = 1; j <= segments; ++j) {
            float t = static_cast<float>(j) / segments;
            float x = waveStartX + waveWidth * t;
            float ny = y + std::sin(phase + t * 3.14f) * amplitude;
            ren.drawLine({prevX, prevY}, {x, ny}, accentBase.withAlpha(alpha), 1.f);
            prevX = x;
            prevY = ny;
        }
    }
}

void GameInfoPanel::renderContent(nxui::Renderer& ren, const nxui::Rect& panel) {
    float innerX = panel.x + kInnerPad;
    float innerY = panel.y + kInnerPad;
    float innerW = panel.width - 2.f * kInnerPad;
    
    // Icon container with shadow and rounded corners
    if (m_iconTex) {
        nxui::Rect iconRect = {
            std::round(innerX),
            std::round(innerY),
            std::round(kIconSize),
            std::round(kIconSize)
        };
        
        nxui::Color shadowColor = m_theme ? m_theme->background : nxui::Color(0.0f, 0.1f, 0.25f, 1.f);
        nxui::Color glowColor = m_theme ? m_theme->panelHighlight : nxui::Color(0.5f, 0.7f, 1.f, 1.f);
        
        ren.drawRoundedRect({iconRect.x - 8.f, iconRect.y - 8.f, iconRect.width + 16.f, iconRect.height + 16.f},
                            shadowColor.withAlpha(0.24f), 20.f);
        ren.drawTextureRounded(m_iconTex, iconRect, 18.f);
        ren.drawRoundedRectOutline(iconRect, glowColor.withAlpha(0.45f), 18.f, 3.f);
        
        nxui::Rect innerIconBorder = iconRect;
        innerIconBorder.x += 2.f;
        innerIconBorder.y += 2.f;
        innerIconBorder.width -= 4.f;
        innerIconBorder.height -= 4.f;
        ren.drawRoundedRectOutline(innerIconBorder, glowColor.withAlpha(0.28f), 16.f, 1.f);
    }
    
    float infoX = std::round(innerX + kIconSize + 26.f);
    float infoY = std::round(innerY + 6.f);
    float infoMaxW = innerW - kIconSize - 26.f - 10.f;
    
    if (m_font) {
        m_titleDisplay = truncateText(m_title, infoMaxW);
        
        nxui::Color titleBgColor = m_theme ? nxui::Color(m_theme->panelBase.r * 0.7f, m_theme->panelBase.g * 0.7f,
                                                          m_theme->panelBase.b * 0.7f, 0.6f)
                                          : nxui::Color(0.12f, 0.18f, 0.32f, 0.6f);
        nxui::Color titleBorderColor = m_theme ? m_theme->panelHighlight.withAlpha(0.25f)
                                              : nxui::Color(0.3f, 0.5f, 0.8f, 0.25f);
        nxui::Color titleTextColor = m_theme ? m_theme->textPrimary : nxui::Color(1.f, 1.f, 1.f, 1.f);
        
        nxui::Rect titleBox = {infoX - 12.f, infoY - 6.f, infoMaxW + 24.f, 46.f};
        ren.drawRoundedRect(titleBox, titleBgColor, 8.f);
        ren.drawRoundedRectOutline(titleBox, titleBorderColor, 8.f, 1.f);
        ren.drawText(m_titleDisplay, {infoX, infoY + 6.f}, m_font, titleTextColor, 1.f);
    }
    
    // Version and Size info containers
    if (m_smallFont) {
        float versionY = std::round(infoY + 58.f);
        float sizeY = std::round(versionY + 42.f);
        
        // Get theme colors (with fallbacks)
        nxui::Color accentColor = m_theme ? m_theme->panelHighlight : nxui::Color(0.4f, 0.6f, 0.9f, 1.f);
        nxui::Color bgColor = m_theme ? nxui::Color(m_theme->panelBase.r * 0.6f, m_theme->panelBase.g * 0.6f, 
                                                     m_theme->panelBase.b * 0.6f, 0.7f) : nxui::Color(0.08f, 0.14f, 0.26f, 0.7f);
        nxui::Color textColor = m_theme ? m_theme->textSecondary : nxui::Color(0.5f, 0.75f, 1.f, 1.f);
        
        // Version info background with border
        ren.drawRoundedRect({infoX - 12.f, versionY - 4.f, infoMaxW + 24.f, 34.f},
                            bgColor, 6.f);
        ren.drawRoundedRectOutline({infoX - 12.f, versionY - 4.f, infoMaxW + 24.f, 34.f},
                                   accentColor.withAlpha(0.3f), 6.f, 1.f);
        
        // Version label with i18n
        std::string versionStr = m_i18n ? m_i18n->tr("common.version", "Version") : "Version";
        std::string versionLabel = "v" + m_version;
        ren.drawText(versionStr + ": " + versionLabel, {infoX, versionY}, m_smallFont,
                     textColor, 1.f);
        
        // Size info background with border
        ren.drawRoundedRect({infoX - 12.f, sizeY - 4.f, infoMaxW + 24.f, 34.f},
                            bgColor, 6.f);
        ren.drawRoundedRectOutline({infoX - 12.f, sizeY - 4.f, infoMaxW + 24.f, 34.f},
                                   accentColor.withAlpha(0.3f), 6.f, 1.f);
        
        // Size label with i18n
        std::string sizeStr = m_i18n ? m_i18n->tr("common.size", "Size") : "Size";
        ren.drawText(sizeStr + ": " + m_sizeText, {infoX, sizeY}, m_smallFont,
                     textColor, 1.f);
    }
}

void GameInfoPanel::render(nxui::Renderer& ren) {
    if (!m_active) return;

    float scale = m_showing ? easeOutCubic(m_animT) : (1.f - easeOutCubic(m_animT));
    nxui::Rect panel = panelRect(scale);

    // Draw background with animated effects
    renderBackgroundEffect(ren, panel, scale);

    if (scale > 0.95f) {
        // Render content when mostly visible
        renderContent(ren, panel);
    }
}

void GameInfoPanel::handleTouch(nxui::Input& input) {
    // Close on touch (only when fully visible to avoid accidental closes)
    if (input.touchDown() && isFullyVisible()) {
        if (m_closeSfxCb) m_closeSfxCb();
        hide();
    }
}

void GameInfoPanel::rebuildContent() {
    // Truncate title for display
    if (m_font) {
        float maxTitleWidth = kPanelWidth - kIconSize - 50.f;
        m_titleDisplay = truncateText(m_title, maxTitleWidth);
    }
}

nxui::Rect GameInfoPanel::panelRect() const {
    return panelRect(1.f);
}

nxui::Rect GameInfoPanel::panelRect(float scale) const {
    float screenW = 1280.f;
    float screenH = 720.f;
    float panelW = kPanelWidth * scale;
    float panelH = kPanelHeight * scale;
    float centerX = screenW * 0.5f;
    float centerY = screenH * 0.5f;
    
    return {
        centerX - panelW * 0.5f,
        centerY - panelH * 0.5f,
        panelW,
        panelH
    };
}