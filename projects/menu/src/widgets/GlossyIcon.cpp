#include "GlossyIcon.hpp"
#include "FolderPalette.hpp"
#include "BatteryDrawing.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Font.hpp>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cmath>
#include <filesystem>
#ifdef SWITCHU_MENU
#include <switch.h>
#endif

namespace {

void drawBatteryRing(nxui::Renderer& ren, const nxui::Vec2& center,
                     float radius, int percent, bool charging,
                     bool console, nxui::Texture* deviceIcon,
                     nxui::Font* font, float opacity) {
    constexpr int segments = 96;
    constexpr float tau = 6.28318530718f;
    constexpr float startAngle = -1.57079632679f;
    const float thickness = std::max(4.f, radius * 0.13f);
    const float progress = std::clamp(percent / 100.f, 0.f, 1.f);
    const nxui::Color activeColor = percent <= 20
        ? nxui::Color(1.f, 0.25f, 0.22f, opacity)
        : nxui::Color(0.25f, 0.92f, 0.48f, opacity);
    for (int i = 0; i < segments; ++i) {
        const float a0 = startAngle + tau * static_cast<float>(i) / segments;
        const float a1 = startAngle + tau * static_cast<float>(i + 1) / segments;
        ren.drawLine({center.x + std::cos(a0) * radius,
                      center.y + std::sin(a0) * radius},
                     {center.x + std::cos(a1) * radius,
                      center.y + std::sin(a1) * radius},
                     nxui::Color::white().withAlpha(0.13f * opacity), thickness);
    }
    if (progress > 0.f) {
        const int activeSegments = std::max(1,
            static_cast<int>(std::ceil(progress * segments)));
        for (int i = 0; i < activeSegments; ++i) {
            const float t0 = std::min(progress, static_cast<float>(i) / segments);
            const float t1 = std::min(progress, static_cast<float>(i + 1) / segments);
            const float a0 = startAngle + tau * t0;
            const float a1 = startAngle + tau * t1;
            ren.drawLine({center.x + std::cos(a0) * radius,
                          center.y + std::sin(a0) * radius},
                         {center.x + std::cos(a1) * radius,
                          center.y + std::sin(a1) * radius},
                         activeColor, thickness);
        }
        const float endAngle = startAngle + tau * progress;
        ren.drawCircle({center.x, center.y - radius}, thickness * 0.5f,
                       activeColor, 20);
        ren.drawCircle({center.x + std::cos(endAngle) * radius,
                        center.y + std::sin(endAngle) * radius},
                       thickness * 0.5f, activeColor, 20);
    }

    const nxui::Color glyph = nxui::Color::white().withAlpha(0.88f * opacity);
    if (deviceIcon && deviceIcon->valid()) {
        const float maxWidth = radius * (console ? 0.86f : 0.48f);
        const float maxHeight = radius * (console ? 0.55f : 0.78f);
        const float aspect = static_cast<float>(deviceIcon->width()) /
            std::max(1.f, static_cast<float>(deviceIcon->height()));
        nxui::Rect iconRect{center.x - maxWidth * 0.5f,
                            center.y - maxHeight * 0.58f,
                            maxWidth, maxHeight};
        if (aspect > maxWidth / maxHeight) {
            iconRect.height = maxWidth / aspect;
            iconRect.y += (maxHeight - iconRect.height) * 0.5f;
        } else {
            iconRect.width = maxHeight * aspect;
            iconRect.x += (maxWidth - iconRect.width) * 0.5f;
        }
        ren.drawTexture(deviceIcon, iconRect,
                        nxui::Color::white().withAlpha(0.95f * opacity));
    } else if (console) {
        const nxui::Rect body{center.x - radius * 0.22f,
                              center.y - radius * 0.30f,
                              radius * 0.44f, radius * 0.56f};
        ren.drawRoundedRectOutline(body, glyph, radius * 0.07f,
                                   std::max(1.4f, radius * 0.045f));
        ren.drawLine({body.x + body.width * 0.35f, body.bottom() - radius * 0.07f},
                     {body.x + body.width * 0.65f, body.bottom() - radius * 0.07f},
                     glyph, std::max(1.f, radius * 0.025f));
    } else {
        ren.drawCircle({center.x - radius * 0.16f, center.y - radius * 0.05f},
                       radius * 0.13f, glyph, 18);
        ren.drawCircle({center.x + radius * 0.16f, center.y - radius * 0.05f},
                       radius * 0.13f, glyph, 18);
        ren.drawRoundedRect({center.x - radius * 0.22f,
                             center.y - radius * 0.09f,
                             radius * 0.44f, radius * 0.18f}, glyph,
                            radius * 0.08f);
    }
    if (charging) {
        const nxui::Rect bolt{center.x + radius * 0.18f,
                              center.y - radius * 0.52f,
                              radius * 0.25f, radius * 0.34f};
        switchu::battery_drawing::drawLightningBolt(
            ren, bolt, nxui::Color(1.f, 0.86f, 0.18f, opacity),
            nxui::Color(1.f, 0.64f, 0.08f, opacity * 0.72f),
            std::max(0.8f, radius * 0.018f));
    }
    if (font) {
        const std::string text = std::to_string(percent) + "%";
        const float scale = std::max(0.34f, radius / 72.f * 0.50f);
        const auto measured = font->measure(text);
        ren.drawText(text,
                     {center.x - measured.x * scale * 0.5f,
                      center.y + radius * 0.28f}, font,
                     nxui::Color::white().withAlpha(0.92f * opacity), scale);
    }
}

} // namespace


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

void GlossyIcon::setWidgetData(switchu::widgets::WidgetType type,
                               int columns, int rows,
                               std::string primary, std::string secondary,
                               const std::string& assetPath,
                               nxui::GpuDevice* gpu, nxui::Renderer* renderer) {
    m_widgetType = type;
    m_widgetColumns = std::max(1, columns);
    m_widgetRows = std::max(1, rows);
    m_widgetPrimary = std::move(primary);
    m_widgetSecondary = std::move(secondary);
    m_widgetTexture.reset();
    m_widgetAnimation.reset();
    m_widgetExternalTexture = nullptr;
    if (assetPath.empty() || !gpu || !renderer) return;

    std::string extension = assetPath;
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if ((extension.ends_with(".webp") || extension.ends_with(".gif")) &&
        m_widgetAnimation.load(*gpu, *renderer, assetPath)) {
        if (m_widgetAnimation.frameCount() > 0) return;
    }

    auto texture = std::make_unique<nxui::Texture>();
    if (texture->loadFromFile(*gpu, *renderer, assetPath, 512))
        m_widgetTexture = std::move(texture);
}

void GlossyIcon::setWidgetGameAssets(std::uint64_t titleId,
                                     const std::string& heroPath,
                                     const std::string& logoPath,
                                     const std::vector<std::uint8_t>& iconData,
                                     nxui::GpuDevice* gpu,
                                     nxui::Renderer* renderer) {
    m_widgetGameTitleId = titleId;
    m_widgetHeroTexture.reset();
    m_widgetLogoTexture.reset();
    m_widgetGameIconTexture.reset();
    m_widgetHero = nullptr;
    m_widgetLogo = nullptr;
    m_widgetGameIcon = nullptr;
    if (titleId == 0 || !gpu || !renderer) return;

    std::error_code error;
    if (!heroPath.empty() && std::filesystem::is_regular_file(heroPath, error)) {
        auto hero = std::make_unique<nxui::Texture>();
        if (hero->loadFromFile(*gpu, *renderer, heroPath, 640)) {
            m_widgetHeroTexture = std::move(hero);
            m_widgetHero = m_widgetHeroTexture.get();
        }
    }
    error.clear();
    if (!logoPath.empty() && std::filesystem::is_regular_file(logoPath, error)) {
        auto logo = std::make_unique<nxui::Texture>();
        if (logo->loadFromFile(*gpu, *renderer, logoPath, 384)) {
            m_widgetLogoTexture = std::move(logo);
            m_widgetLogo = m_widgetLogoTexture.get();
        }
    }
    if (!iconData.empty()) {
        auto gameIcon = std::make_unique<nxui::Texture>();
        if (gameIcon->loadFromMemory(*gpu, *renderer,
                                     iconData.data(), iconData.size(), 192)) {
            m_widgetGameIconTexture = std::move(gameIcon);
            m_widgetGameIcon = m_widgetGameIconTexture.get();
            m_tex = m_widgetGameIcon;
        }
    }
}

void GlossyIcon::setWidgetGameTextures(std::uint64_t titleId,
                                       nxui::Texture* hero,
                                       nxui::Texture* logo,
                                       nxui::Texture* gameIcon) {
    m_widgetGameTitleId = titleId;
    m_widgetHero = hero;
    m_widgetLogo = logo;
    m_widgetGameIcon = gameIcon;
    if (gameIcon) m_tex = gameIcon;
}

void GlossyIcon::copyWidgetPresentationFrom(GlossyIcon& source) {
    m_entryKind = source.m_entryKind;
    m_widgetType = source.m_widgetType;
    m_widgetColumns = source.m_widgetColumns;
    m_widgetRows = source.m_widgetRows;
    m_widgetPrimary = source.m_widgetPrimary;
    m_widgetSecondary = source.m_widgetSecondary;
    m_widgetHeader = source.m_widgetHeader;
    m_consoleBatteryPercent = source.m_consoleBatteryPercent;
    m_consoleBatteryCharging = source.m_consoleBatteryCharging;
    m_controllerBatteries = source.m_controllerBatteries;
    m_batteryConsoleIcon = source.m_batteryConsoleIcon;
    m_batteryJoyconLeftIcon = source.m_batteryJoyconLeftIcon;
    m_batteryJoyconRightIcon = source.m_batteryJoyconRightIcon;
    m_widgetGameTitleId = source.m_widgetGameTitleId;
    m_widgetHero = source.m_widgetHero;
    m_widgetLogo = source.m_widgetLogo;
    m_widgetGameIcon = source.m_widgetGameIcon;
    m_wideGameHero = source.m_wideGameHero;
    m_wideGameLogo = source.m_wideGameLogo;
    m_widgetExternalTexture = source.m_widgetAnimation.hasFrames()
        ? source.m_widgetAnimation.currentFrame()
        : source.m_widgetTexture.get();
    m_font = source.m_font;
    m_loadingColor = source.m_loadingColor;
    m_tex = source.m_tex;
    setBaseColor(source.baseColor());
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
    if (m_entryKind == GridEntryKind::Widget && m_widgetAnimation.hasFrames())
        m_widgetAnimation.update(dt, true);
#ifdef SWITCHU_MENU
    if (m_entryKind == GridEntryKind::Widget &&
        m_widgetType == switchu::widgets::WidgetType::Batteries) {
        m_batteryRefreshTimer += dt;
        if (m_batteryRefreshTimer >= 1.f || m_controllerBatteries.empty()) {
            m_batteryRefreshTimer = 0.f;
            m_controllerBatteries.clear();
            // Attached Joy-Con are reported on the Handheld npad, not on a
            // numbered wireless-player slot.
            const u32 handheldStyle = hidGetNpadStyleSet(HidNpadIdType_Handheld);
            if (handheldStyle != 0) {
                HidPowerInfo left{}, right{};
                hidGetNpadPowerInfoSplit(HidNpadIdType_Handheld, &left, &right);
                m_controllerBatteries.push_back({
                    static_cast<int>(left.battery_level) * 25,
                    left.is_charging, "L"});
                m_controllerBatteries.push_back({
                    static_cast<int>(right.battery_level) * 25,
                    right.is_charging, "R"});
            }
            for (int player = 0; player < 8 && m_controllerBatteries.size() < 3; ++player) {
                const auto id = static_cast<HidNpadIdType>(HidNpadIdType_No1 + player);
                const u32 style = hidGetNpadStyleSet(id);
                if (style == 0 || (style & HidNpadStyleTag_NpadHandheld)) continue;
                if (style & HidNpadStyleTag_NpadJoyDual) {
                    HidPowerInfo left{}, right{};
                    hidGetNpadPowerInfoSplit(id, &left, &right);
                    if (m_controllerBatteries.size() < 3)
                        m_controllerBatteries.push_back({
                            static_cast<int>(left.battery_level) * 25,
                            left.is_charging, "L"});
                    if (m_controllerBatteries.size() < 3)
                        m_controllerBatteries.push_back({
                            static_cast<int>(right.battery_level) * 25,
                            right.is_charging, "R"});
                } else {
                    HidPowerInfo info{};
                    hidGetNpadPowerInfoSingle(id, &info);
                    m_controllerBatteries.push_back({
                        static_cast<int>(info.battery_level) * 25,
                        info.is_charging, std::to_string(player + 1)});
                }
            }
        }
    }
#endif
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

    if (m_entryKind == GridEntryKind::WidgetContinuation)
        return;

    if (m_entryKind == GridEntryKind::Widget) {
        const nxui::Rect inner = r.shrunk(std::max(8.f, 10.f * s));
        if (m_widgetType == switchu::widgets::WidgetType::RecentlyPlayed) {
            // Match Recent Playtime's inset card, with the hero replacing its
            // blue content rectangle.
            const nxui::Rect card = inner;
            const float contentRadius = std::max(8.f, rad - 5.f);
            if (m_widgetHero && m_widgetHero->valid()) {
                ren.drawTextureRounded(m_widgetHero, card,
                    contentRadius, nxui::Color::white().withAlpha(m_opacity));
            } else {
                ren.drawRoundedRect(card, m_loadingColor.withAlpha(0.20f * m_opacity),
                                    contentRadius);
            }
            if (m_font && !m_widgetHeader.empty()) {
                const float headerScale = 0.44f * s;
                ren.drawText(m_widgetHeader,
                    {card.x + 12.f * s, card.y + 9.f * s}, m_font,
                    nxui::Color::white().withAlpha(0.92f * m_opacity),
                    headerScale);
            }
            const nxui::Rect logoArea{card.x + 12.f * s,
                                      card.y + card.height * 0.58f,
                                      card.width * 0.58f,
                                      card.height * 0.32f};
            if (m_widgetLogo && m_widgetLogo->valid()) {
                const float aspect = static_cast<float>(m_widgetLogo->width()) /
                    std::max(1.f, static_cast<float>(m_widgetLogo->height()));
                nxui::Rect logoRect = logoArea;
                if (aspect > logoArea.width / logoArea.height) {
                    logoRect.height = logoArea.width / aspect;
                    logoRect.y += (logoArea.height - logoRect.height) * 0.5f;
                } else {
                    logoRect.width = logoArea.height * aspect;
                    logoRect.y += 0.f;
                }
                ren.drawTexture(m_widgetLogo, logoRect,
                    nxui::Color::white().withAlpha(0.98f * m_opacity));
            } else if (m_widgetGameIcon && m_widgetGameIcon->valid()) {
                const float iconSide = std::min(logoArea.height, 54.f * s);
                ren.drawTextureRounded(m_widgetGameIcon,
                    {logoArea.x, logoArea.y + logoArea.height - iconSide,
                     iconSide, iconSide}, 10.f * s,
                    nxui::Color::white().withAlpha(m_opacity));
            } else if (m_font && !m_widgetPrimary.empty()) {
                const nxui::Vec2 measured = m_font->measure(m_widgetPrimary);
                float scale = 0.70f * s;
                if (measured.x > 0.f)
                    scale = std::min(scale, logoArea.width / measured.x);
                ren.drawText(m_widgetPrimary,
                    {logoArea.x, logoArea.y + (logoArea.height - measured.y * scale) * 0.5f},
                    m_font, nxui::Color::white().withAlpha(0.96f * m_opacity), scale);
            }
            return;
        }

        if (m_widgetType == switchu::widgets::WidgetType::RecentPlaytime) {
            ren.drawRoundedRect(inner, m_loadingColor.withAlpha(0.13f * m_opacity),
                                std::max(9.f, rad - 5.f));
            if (!m_font) return;

            const float headerScale = 0.48f * s;
            ren.drawText(m_widgetHeader, {inner.x + 10.f * s, inner.y + 8.f * s},
                         m_font, nxui::Color::white().withAlpha(0.68f * m_opacity),
                         headerScale);
            const float headerHeight = m_font->measure(m_widgetHeader).y * headerScale;
            const float bodyTop = inner.y + 12.f * s + headerHeight;
            const float bodyHeight = std::max(28.f, inner.bottom() - bodyTop - 8.f * s);
            const float iconSide = std::min(bodyHeight, inner.width * 0.30f);
            const nxui::Rect iconRect{inner.x + 10.f * s,
                                      bodyTop + (bodyHeight - iconSide) * 0.5f,
                                      iconSide, iconSide};
            if (m_widgetGameIcon && m_widgetGameIcon->valid()) {
                ren.drawTextureRounded(m_widgetGameIcon, iconRect,
                    std::max(8.f, 12.f * s),
                    nxui::Color::white().withAlpha(m_opacity));
            } else {
                ren.drawRoundedRect(iconRect,
                    nxui::Color::white().withAlpha(0.10f * m_opacity),
                    std::max(8.f, 12.f * s));
            }

            const float textX = iconRect.right() + 12.f * s;
            const float textWidth = std::max(20.f, inner.right() - textX - 10.f * s);
            const nxui::Vec2 nameMeasure = m_font->measure(m_widgetPrimary);
            float nameScale = 0.62f * s;
            if (nameMeasure.x > 0.f)
                nameScale = std::min(nameScale, textWidth / nameMeasure.x);
            nameScale = std::max(0.38f * s, nameScale);
            const nxui::Vec2 timeMeasure = m_font->measure(m_widgetSecondary);
            float timeScale = 0.52f * s;
            if (timeMeasure.x > 0.f)
                timeScale = std::min(timeScale, textWidth / timeMeasure.x);
            const float nameHeight = nameMeasure.y * nameScale;
            const float timeHeight = timeMeasure.y * timeScale;
            const float textTop = bodyTop +
                (bodyHeight - nameHeight - timeHeight - 4.f * s) * 0.5f;
            ren.drawText(m_widgetPrimary, {textX, textTop}, m_font,
                         nxui::Color::white().withAlpha(0.96f * m_opacity), nameScale);
            ren.drawText(m_widgetSecondary,
                         {textX, textTop + nameHeight + 4.f * s}, m_font,
                         nxui::Color::white().withAlpha(0.72f * m_opacity), timeScale);
            return;
        }

        if (m_widgetType == switchu::widgets::WidgetType::Batteries) {
            ren.drawRoundedRect(inner,
                nxui::Color(0.035f, 0.045f, 0.075f, 0.72f * m_opacity),
                std::max(12.f, rad - 5.f));
            const int capacity = m_widgetRows >= 2 ? 4 : 3;
            const int count = std::min(capacity,
                1 + static_cast<int>(m_controllerBatteries.size()));
            const int columns = capacity == 4 ? 2 : 3;
            const int rows = capacity == 4 ? 2 : 1;
            const float cellW = inner.width / columns;
            const float cellH = inner.height / rows;
            const float radius = std::max(24.f,
                std::min(cellW, cellH) * (capacity == 4 ? 0.31f : 0.36f));
            for (int i = 0; i < count; ++i) {
                const int column = i % columns;
                const int row = i / columns;
                const nxui::Vec2 center{
                    inner.x + cellW * (column + 0.5f),
                    inner.y + cellH * (row + 0.47f)};
                if (i == 0) {
                    drawBatteryRing(ren, center, radius,
                        m_consoleBatteryPercent, m_consoleBatteryCharging,
                        true, m_batteryConsoleIcon, m_font, m_opacity);
                } else {
                    const auto& controller =
                        m_controllerBatteries[static_cast<std::size_t>(i - 1)];
                    nxui::Texture* controllerIcon = controller.label == "L"
                        ? m_batteryJoyconLeftIcon
                        : (controller.label == "R"
                            ? m_batteryJoyconRightIcon : nullptr);
                    drawBatteryRing(ren, center, radius, controller.percent,
                        controller.charging, false, controllerIcon,
                        m_font, m_opacity);
                }
            }
            return;
        }

        const bool imageWidget = m_widgetType == switchu::widgets::WidgetType::ImagePin
            || m_widgetType == switchu::widgets::WidgetType::RandomScreenshot;
        nxui::Texture* image = m_widgetExternalTexture
            ? m_widgetExternalTexture
            : (m_widgetAnimation.hasFrames()
                ? m_widgetAnimation.currentFrame() : m_widgetTexture.get());
        if (imageWidget && image && image->valid()) {
            const float sourceAspect = static_cast<float>(image->width()) /
                std::max(1.f, static_cast<float>(image->height()));
            const float targetAspect = inner.width / std::max(1.f, inner.height);
            nxui::Rect imageRect = inner;
            if (sourceAspect > targetAspect) {
                imageRect.height = inner.width / sourceAspect;
                imageRect.y += (inner.height - imageRect.height) * 0.5f;
            } else {
                imageRect.width = inner.height * sourceAspect;
                imageRect.x += (inner.width - imageRect.width) * 0.5f;
            }
            ren.drawTextureRounded(image, imageRect,
                                   std::max(8.f, rad - 6.f),
                                   nxui::Color::white().withAlpha(m_opacity));
            if (m_focused)
                ren.drawRoundedRectOutline(inner.expanded(2.f),
                    nxui::Color::white().withAlpha(0.42f * m_opacity),
                    std::max(8.f, rad - 4.f), 2.f);
            return;
        }

        nxui::Color accent = m_loadingColor;
        ren.drawRoundedRect(inner, accent.withAlpha(0.13f * m_opacity),
                            std::max(9.f, rad - 5.f));
        if (!m_font) return;

        std::string primary = m_widgetPrimary;
        std::string secondary = m_widgetSecondary;
        if (m_widgetType == switchu::widgets::WidgetType::Clock) {
            std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_r(&now, &local);
            char timeBuffer[16]{};
            char dateBuffer[48]{};
            std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &local);
            std::strftime(dateBuffer, sizeof(dateBuffer), "%a %d %b", &local);
            primary = timeBuffer;
            if (m_widgetColumns > 1) secondary = dateBuffer;
        }

        const nxui::Vec2 primaryMeasure = m_font->measure(primary);
        float primaryScale = m_widgetType == switchu::widgets::WidgetType::Clock
            ? (m_widgetColumns > 1 ? 1.55f : 1.18f) : 0.78f;
        if (primaryMeasure.x > 0.f)
            primaryScale = std::min(primaryScale,
                (inner.width - 18.f) / primaryMeasure.x);
        primaryScale = std::max(0.48f, primaryScale);
        const float primaryHeight = primaryMeasure.y * primaryScale;
        float totalHeight = primaryHeight;
        float secondaryScale = 0.62f;
        nxui::Vec2 secondaryMeasure{};
        if (!secondary.empty()) {
            secondaryMeasure = m_font->measure(secondary);
            if (secondaryMeasure.x > 0.f)
                secondaryScale = std::min(secondaryScale,
                    (inner.width - 18.f) / secondaryMeasure.x);
            secondaryScale = std::max(0.42f, secondaryScale);
            totalHeight += 7.f + secondaryMeasure.y * secondaryScale;
        }
        float textY = inner.y + (inner.height - totalHeight) * 0.5f;
        ren.drawText(primary,
            {inner.x + (inner.width - primaryMeasure.x * primaryScale) * 0.5f, textY},
            m_font, nxui::Color::white().withAlpha(0.98f * m_opacity), primaryScale);
        if (!secondary.empty()) {
            textY += primaryHeight + 7.f;
            ren.drawText(secondary,
                {inner.x + (inner.width - secondaryMeasure.x * secondaryScale) * 0.5f,
                 textY},
                m_font, nxui::Color::white().withAlpha(0.72f * m_opacity),
                secondaryScale);
        }
        return;
    }

    if (m_entryKind == GridEntryKind::Folder) {
        nxui::Color accent = switchu::folders::colorForIndex(m_folderColorIndex);

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

        const bool named = m_font && !m_title.empty();

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

        if (named) {
            const nxui::Vec2 measured = m_font->measure(m_title);
            const float room = std::max(8.f, shell.width - 6.f * s);
            float textScale = 1.05f * s;
            if (measured.x > 0.f)
                textScale = std::min(textScale, room / measured.x);
            textScale = std::max(0.38f * s, textScale);

            const float textW = measured.x * textScale;
            const float textH = measured.y * textScale;
            const nxui::Vec2 textPos{shell.x + (shell.width - textW) * 0.5f,
                                     shell.y + (shell.height - textH) * 0.5f};

            const float halo = std::max(1.f, 1.5f * s);
            const nxui::Color shadow(0.05f, 0.16f, 0.26f, 0.34f * m_opacity);
            const nxui::Vec2 offsets[8] = {
                {-halo, 0.f}, {halo, 0.f}, {0.f, -halo}, {0.f, halo},
                {-halo, -halo}, {halo, -halo}, {-halo, halo}, {halo, halo}};
            for (const nxui::Vec2& off : offsets)
                ren.drawText(m_title, {textPos.x + off.x, textPos.y + off.y},
                             m_font, shadow, textScale);

            ren.drawText(m_title,
                         {textPos.x, textPos.y + halo * 0.7f},
                         m_font,
                         nxui::Color(0.04f, 0.14f, 0.24f, 0.30f * m_opacity),
                         textScale);

            ren.drawText(m_title, textPos, m_font,
                         nxui::Color::white().withAlpha(0.98f * m_opacity),
                         textScale);
        }

        if (m_focused) {
            ren.drawRoundedRectOutline(shell.expanded(2.f * s),
                                       accent.withAlpha(0.42f * m_opacity),
                                       shellRadius + 2.f * s, 2.f * s);
        }
        return;
    }

    if (m_entryKind == GridEntryKind::Application && m_widgetRows > 1) {
        const nxui::Rect card = r.shrunk(std::max(8.f, 10.f * s));
        const float contentRadius = std::max(8.f, rad - 5.f);
        if (m_tex && m_tex->valid()) {
            ren.drawTextureRounded(m_tex, card, contentRadius,
                                   nxui::Color::white().withAlpha(m_opacity));
        } else {
            ren.drawRoundedRect(card,
                m_loadingColor.withAlpha(0.18f * m_opacity), contentRadius);
        }
        return;
    }

    if (m_entryKind == GridEntryKind::Application && m_widgetColumns > 1) {
        const nxui::Rect card = r.shrunk(std::max(8.f, 10.f * s));
        const float contentRadius = std::max(8.f, rad - 5.f);
        if (m_wideGameHero && m_wideGameHero->valid()) {
            ren.drawTextureRounded(m_wideGameHero, card, contentRadius,
                                   nxui::Color::white().withAlpha(m_opacity));
        } else if (m_tex && m_tex->valid()) {
            ren.drawTextureRounded(m_tex, card, contentRadius,
                                   nxui::Color::white().withAlpha(m_opacity));
        } else {
            ren.drawRoundedRect(card,
                m_loadingColor.withAlpha(0.18f * m_opacity), contentRadius);
        }
        const nxui::Rect logoArea{card.x + 12.f * s,
                                  card.y + card.height * 0.58f,
                                  card.width * 0.58f,
                                  card.height * 0.32f};
        if (m_wideGameLogo && m_wideGameLogo->valid()) {
            const float aspect = static_cast<float>(m_wideGameLogo->width()) /
                std::max(1.f, static_cast<float>(m_wideGameLogo->height()));
            nxui::Rect logoRect = logoArea;
            if (aspect > logoArea.width / logoArea.height) {
                logoRect.height = logoArea.width / aspect;
                logoRect.y += (logoArea.height - logoRect.height) * 0.5f;
            } else {
                logoRect.width = logoArea.height * aspect;
            }
            ren.drawTexture(m_wideGameLogo, logoRect,
                            nxui::Color::white().withAlpha(0.98f * m_opacity));
        } else if (m_font && !m_title.empty()) {
            const auto measured = m_font->measure(m_title);
            float textScale = 0.68f * s;
            if (measured.x > 0.f)
                textScale = std::min(textScale, logoArea.width / measured.x);
            ren.drawText(m_title,
                {logoArea.x, logoArea.y + (logoArea.height - measured.y * textScale) * 0.5f},
                m_font, nxui::Color::white().withAlpha(0.96f * m_opacity),
                textScale);
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
