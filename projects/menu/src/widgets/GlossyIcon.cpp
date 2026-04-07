#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Font.hpp>
#include <cmath>


GlossyIcon::GlossyIcon() {
    m_animScale.setImmediate(0.f);
    m_appearOpacity.setImmediate(0.f);
    setCornerRadius(16.f);
    setPadding(8.f);
}

void GlossyIcon::startAppear(float delay) {
    m_appearDelay = delay;
    m_appearTimer = 0.f;
    m_appearing   = true;
    m_animScale.setImmediate(0.f);
    m_appearOpacity.setImmediate(0.f);
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
    if (m_suspended) {
        m_suspendPulse += dt * 2.2f;
    }
}

void GlossyIcon::onRender(nxui::Renderer& ren) {
    float s = m_animScale.value();
    float a = m_appearOpacity.value();
    if (s < 0.01f || a < 0.01f) return;

    setScale(s);

    float savedOp = m_opacity;
    m_opacity = a * savedOp;

    nxui::GlassWidget::onRender(ren);

    m_opacity = savedOp;

    nxui::Rect r = m_rect;
    if (s < 1.f) {
        float w = r.width  * s;
        float h = r.height * s;
        r.x += (r.width  - w) * 0.5f;
        r.y += (r.height - h) * 0.5f;
        r.width  = w;
        r.height = h;
    }
    float rad = cornerRadius();

    if (m_notLaunchable && s > 0.5f) {
        nxui::Color dimColor(0.f, 0.f, 0.f, 0.55f * a);
        ren.drawRoundedRect(r, dimColor, rad);
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
        ren.drawCircle(badgeCenter, badgeSize * 0.5f, nxui::Color(0.1f, 0.1f, 0.1f, 0.85f * a), 16);

        float triH = badgeSize * 0.45f;
        float triW = triH * 0.85f;
        nxui::Vec2 p1 = { badgeCenter.x - triW * 0.35f, badgeCenter.y - triH * 0.5f };
        nxui::Vec2 p2 = { badgeCenter.x - triW * 0.35f, badgeCenter.y + triH * 0.5f };
        nxui::Vec2 p3 = { badgeCenter.x + triW * 0.65f, badgeCenter.y };
        ren.drawTriangle(p1, p2, p3, nxui::Color(0.18f, 0.85f, 0.45f, 0.95f * a));
    }
}

void GlossyIcon::onContentRender(nxui::Renderer& ren) {
    if (!m_tex || !m_tex->valid()) return;

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

    float inset = 8.f * s;
    nxui::Rect texRect = r.shrunk(inset);
    ren.drawTextureRounded(m_tex, texRect, rad - 3.f,
                           nxui::Color::white().withAlpha(m_opacity));
}

