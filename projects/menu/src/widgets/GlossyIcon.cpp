#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Font.hpp>
#include <cmath>


GlossyIcon::GlossyIcon() {
    m_animScale.setImmediate(0.f);
    m_appearOpacity.setImmediate(0.f);
    m_focusScale.setImmediate(1.f);
    m_focusGlow.setImmediate(0.f);
    setCornerRadius(16.f);
    setPadding(8.f);
    setLiquidGlassEnabled(true);
    setBlurEnabled(false);
}

void GlossyIcon::onFocusGained() {
    m_focused = true;
    m_focusScale.set(1.075f, 0.18f, nxui::Easing::outBack);
    m_focusGlow.set(1.f, 0.16f, nxui::Easing::outCubic);
}

void GlossyIcon::onFocusLost() {
    m_focused = false;
    m_focusScale.set(1.f, 0.20f, nxui::Easing::outCubic);
    m_focusGlow.set(0.f, 0.16f, nxui::Easing::outCubic);
}

void GlossyIcon::startAppear(float delay) {
    m_appearDelay = delay;
    m_appearTimer = 0.f;
    m_appearing   = true;
    m_animScale.setImmediate(0.f);
    m_appearOpacity.setImmediate(0.f);
}

void GlossyIcon::forceVisible() {
    m_appearing = false;
    m_appearDelay = 0.f;
    m_appearTimer = 0.f;
    m_animScale.setImmediate(1.f);
    m_appearOpacity.setImmediate(1.f);
}

void GlossyIcon::onContentUpdate(float dt) {
    if (m_appearing) {
        m_appearTimer += dt;
        if (m_appearTimer >= m_appearDelay) {
            m_appearing = false;
            m_animScale.set(1.f, 0.4f, nxui::Easing::outExpo);
            m_appearOpacity.set(1.f, 0.3f, nxui::Easing::outExpo);
        }
    }
    m_suspendPulse += dt * 2.2f;
}

void GlossyIcon::onRender(nxui::Renderer& ren) {
    float externalScale = scale();
    float focusS = m_focusScale.value();
    float s = m_animScale.value() * externalScale * focusS;
    float a = m_appearOpacity.value();
    if (s < 0.01f || a < 0.01f) return;

    nxui::Rect savedRect = m_rect;
    nxui::Rect drawRect = savedRect;
    if (std::abs(s - 1.f) > 0.001f) {
        float w = savedRect.width * s;
        float h = savedRect.height * s;
        drawRect.x += (savedRect.width - w) * 0.5f;
        drawRect.y += (savedRect.height - h) * 0.5f;
        drawRect.width = w;
        drawRect.height = h;
        m_rect = drawRect;
    }
    setScale(1.f);
    float savedShade = liquidGlassShade();
    setLiquidGlassShade(m_notLaunchable ? 0.58f : 0.0f);

    float savedOp = m_opacity;
    m_opacity = a * savedOp;

    ren.drawRoundedRect({drawRect.x + 1.f, drawRect.y + 6.f,
                         drawRect.width, drawRect.height},
                        nxui::Color(0.02f, 0.04f, 0.06f,
                                    (m_entryKind == GridEntryKind::Empty ? 0.12f : 0.20f) * m_opacity),
                        cornerRadius() + 1.f);
    nxui::GlassWidget::onRender(ren);

    m_opacity = savedOp;
    m_rect = savedRect;
    setLiquidGlassShade(savedShade);
    setScale(externalScale);

    nxui::Rect r = drawRect;
    float rad = cornerRadius();

    float focusGlow = m_focusGlow.value();
    if (focusGlow > 0.01f && s > 0.5f) {
        float breathe = 0.5f + 0.5f * std::sin(m_suspendPulse * 1.8f + 0.4f);
        nxui::Color focusColor = nxui::Color(0.65f, 0.90f, 1.f, (0.08f + 0.04f * breathe) * focusGlow * a);
        ren.drawRoundedRectOutline(r.expanded(5.f * focusGlow), focusColor,
                                   rad + 5.f, 2.f);
    }

    if (m_isGameCard && !m_notLaunchable && s > 0.5f) {
        float badgeW = 66.f * s;
        float badgeH = 48.f * s;
        float badgeX = r.x + 1.f * s;
        float badgeY = r.y + 6.f * s;

        if (m_gameCardTex && m_gameCardTex->valid()) {
            float cardInset = 1.f * s;
            float maxW = badgeW - cardInset * 2;
            float maxH = badgeH - cardInset * 2;
            float aspect = (float)m_gameCardTex->width() / (float)m_gameCardTex->height();
            float texW = maxW;
            float texH = maxH;
            if (aspect > maxW / maxH) {
                texH = maxW / aspect;
            } else {
                texW = maxH * aspect;
            }
            float texX = badgeX + (badgeW - texW) * 0.5f;
            float texY = badgeY + (badgeH - texH) * 0.5f;
            ren.drawTextureRounded(m_gameCardTex, {texX, texY, texW, texH}, 2.f * s,
                                   nxui::Color::white().withAlpha(0.98f * a));
        } else {
            float cardInset = 4.f * s;
            ren.drawRoundedRect({badgeX + cardInset, badgeY + cardInset,
                                 badgeW - cardInset*2, badgeH - cardInset*2},
                                nxui::Color(0.95f, 0.75f, 0.2f, 0.9f * a), 2.f * s);
        }
    }

    if (m_suspended && s > 0.5f) {
        float pulse = 0.5f + 0.5f * std::sin(m_suspendPulse);
        float glowAlpha = 0.35f + 0.25f * pulse;

        nxui::Color glow(0.18f, 0.85f, 0.45f, glowAlpha * a);
        ren.drawRoundedRectOutline(r.expanded(2.f), glow, rad + 2.f, 2.5f);

        float badgeSize = 26.f * s;
        float badgeX = r.x + r.width  - badgeSize - 4.f * s;
        float badgeY = r.y + r.height - badgeSize - 4.f * s;

        nxui::Vec2 badgeCenter = { badgeX + badgeSize * 0.5f, badgeY + badgeSize * 0.5f };
        ren.drawCircle(badgeCenter, badgeSize * 0.5f,
                       nxui::Color(0.1f, 0.1f, 0.1f, 0.85f * a), 16);

        float triH = badgeSize * 0.45f;
        float triW = triH * 0.85f;
        nxui::Vec2 p1 = { badgeCenter.x - triW * 0.35f, badgeCenter.y - triH * 0.5f };
        nxui::Vec2 p2 = { badgeCenter.x - triW * 0.35f, badgeCenter.y + triH * 0.5f };
        nxui::Vec2 p3 = { badgeCenter.x + triW * 0.65f, badgeCenter.y };
        ren.drawTriangle(p1, p2, p3, nxui::Color(0.18f, 0.85f, 0.45f, 0.95f * a));
    }
}

void GlossyIcon::onContentRender(nxui::Renderer& ren) {
    float s = scale();
    float rad = cornerRadius();

    nxui::Rect r = m_rect;
    if (s < 1.f) {
        float w = r.width  * s;
        float h = r.height * s;
        r.x += (r.width  - w) * 0.5f;
        r.y += (r.height - h) * 0.5f;
        r.width  = w;
        r.height = h;
    }

    if (m_entryKind == GridEntryKind::Empty) {
        ren.drawRoundedRectOutline(r.shrunk(2.f * s),
                                   nxui::Color::white().withAlpha(0.42f * m_opacity),
                                   std::max(8.f, rad - 2.f * s), 1.5f * s);
        return;
    }

    if (m_entryKind == GridEntryKind::Folder) {
        static const nxui::Color kFolderColors[] = {
            {0.22f, 0.68f, 0.86f, 1.f}, // light blue
            {0.28f, 0.82f, 0.31f, 1.f}, // green
            {0.98f, 0.77f, 0.12f, 1.f}, // yellow
            {1.00f, 0.49f, 0.13f, 1.f}, // orange
            {0.93f, 0.28f, 0.30f, 1.f}, // red
            {0.94f, 0.30f, 0.64f, 1.f}, // pink
            {0.57f, 0.29f, 0.88f, 1.f}, // purple
            {0.38f, 0.40f, 0.43f, 1.f}, // grey
        };
        const int colorCount = static_cast<int>(sizeof(kFolderColors) / sizeof(kFolderColors[0]));
        const int colorIndex = std::clamp(m_folderColorIndex, 0, colorCount - 1);
        nxui::Color accent = kFolderColors[colorIndex];

        const float inset = 10.f * s;
        const nxui::Rect shell = r.shrunk(inset);
        const float shellRadius = std::max(12.f, rad - 2.f);
        ren.drawRoundedRect({shell.x, shell.y + 4.f * s, shell.width, shell.height},
                            nxui::Color(0.03f, 0.05f, 0.08f, 0.30f * m_opacity),
                            shellRadius);
        ren.drawRoundedRect(shell,
                            nxui::Color(0.94f, 0.97f, 0.96f, 0.96f * m_opacity),
                            shellRadius);
        ren.drawRoundedRect(shell.shrunk(3.f * s),
                            nxui::Color(0.72f, 0.78f, 0.79f, 0.28f * m_opacity),
                            std::max(8.f, shellRadius - 3.f * s));
        ren.drawRoundedRectOutline(shell.shrunk(1.f * s),
                                   nxui::Color::white().withAlpha(0.92f * m_opacity),
                                   std::max(8.f, shellRadius - 1.f * s), 2.f * s);

        const float cell = std::min(shell.width, shell.height) * 0.185f;
        const float gap = cell * 0.18f;
        const float gridSize = cell * 3.f + gap * 2.f;
        const float gridX = shell.x + (shell.width - gridSize) * 0.5f;
        const float gridY = shell.y + (shell.height - gridSize) * 0.5f;
        for (int i = 0; i < 9; ++i) {
            const int col = i % 3;
            const int row = i / 3;
            const nxui::Rect cellRect{gridX + col * (cell + gap),
                                      gridY + row * (cell + gap), cell, cell};
            ren.drawRoundedRect({cellRect.x, cellRect.y + 1.8f * s,
                                 cellRect.width, cellRect.height},
                                nxui::Color(0.05f, 0.08f, 0.10f,
                                            0.16f * m_opacity),
                                cell * 0.20f);
            const float variation = 0.92f + 0.035f * static_cast<float>((i + row) % 3);
            nxui::Color cellColor(
                std::min(1.f, accent.r * variation),
                std::min(1.f, accent.g * variation),
                std::min(1.f, accent.b * variation),
                0.94f * m_opacity);
            ren.drawRoundedRect(cellRect, cellColor, cell * 0.20f);
            ren.drawRoundedRect({cellRect.x + cell * 0.10f,
                                 cellRect.y + cell * 0.08f,
                                 cellRect.width * 0.80f,
                                 std::max(1.f, cellRect.height * 0.13f)},
                                nxui::Color::white().withAlpha(0.18f * m_opacity),
                                cell * 0.08f);
        }

        if (m_focused) {
            ren.drawRoundedRectOutline(shell.expanded(2.f * s),
                                       accent.withAlpha(0.42f * m_opacity),
                                       shellRadius + 2.f * s, 2.f * s);
        }
        return;
    }

    if (!m_tex || !m_tex->valid()) {
        if (m_titleId == 0)
            return;

        const nxui::Vec2 center{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
        const float outerRadius = std::clamp(std::min(r.width, r.height) * 0.105f,
                                             8.f, 16.f) * s;
        const float innerRadius = outerRadius * 0.48f;
        constexpr int kSpokes = 10;
        constexpr float kStep = 6.28318530718f / (float)kSpokes;
        const float angleBase = m_suspendPulse * 2.8f;
        for (int i = 0; i < kSpokes; ++i) {
            const float angle = angleBase + kStep * (float)i;
            const float weight = 1.f - (float)i / (float)kSpokes;
            const nxui::Color color = m_loadingColor.withAlpha(
                (0.14f + 0.72f * weight) * m_opacity);
            ren.drawLine(
                {center.x + std::cos(angle) * innerRadius,
                 center.y + std::sin(angle) * innerRadius},
                {center.x + std::cos(angle) * outerRadius,
                 center.y + std::sin(angle) * outerRadius},
                color,
                std::max(1.f, (2.4f - 0.9f * ((float)i / (float)kSpokes)) * s));
        }
        return;
    }

    float inset = 8.f * s;
    nxui::Rect texRect = r.shrunk(inset);
    nxui::Color iconTint = nxui::Color::white().withAlpha(m_opacity);
    if (m_notLaunchable) {
        iconTint.r = 0.80f;
        iconTint.g = 0.80f;
        iconTint.b = 0.80f;
    }
    ren.drawTextureRounded(m_tex, texRect, rad - 3.f, iconTint);
}
