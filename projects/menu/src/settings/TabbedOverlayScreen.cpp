#include "TabbedOverlayScreen.hpp"
#include "SettingItemWidgets.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/widgets/GlassBox.hpp>
#include <nxui/widgets/Label.hpp>
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
static constexpr float kSettingsBlurRadius = 6.0f;
static constexpr int kSettingsBlurIter = 1;

namespace {

static constexpr float kTabCardGap = 10.f;
static constexpr float kContentCardInsetX = 18.f;
static constexpr float kContentCardInsetY = 8.f;

constexpr size_t kWrapHeightCacheLimit = 256;
std::unordered_map<std::string, float> g_wrapHeightCache;

std::string wrapHeightCacheKey(nxui::Font* font, const std::string& label, float labelWidth) {
    return std::to_string((std::uintptr_t)font)
         + "\n" + std::to_string(font ? font->revision() : 0)
         + "\n" + std::to_string((int)std::lround(labelWidth * 4.f))
         + "\n" + label;
}

static void drawTabGlyph(nxui::Renderer& ren,
                         TabbedOverlayScreen::TabGlyph glyph,
                         nxui::Vec2 c,
                         const nxui::Color& color,
                         float scale) {
    using Glyph = TabbedOverlayScreen::TabGlyph;
    const float s = scale;
    const float t = 1.7f * s;
    switch (glyph) {
    case Glyph::System: {
        ren.drawCircle(c, 7.2f * s, color.withAlpha(color.a * 0.18f));
        ren.drawCircle(c, 3.2f * s, color);
        for (int i = 0; i < 6; ++i) {
            const float a = (float)i * (3.14159265f / 3.f);
            const nxui::Vec2 a0 = {c.x + std::cos(a) * 5.2f * s, c.y + std::sin(a) * 5.2f * s};
            const nxui::Vec2 a1 = {c.x + std::cos(a) * 9.0f * s, c.y + std::sin(a) * 9.0f * s};
            ren.drawLine(a0, a1, color, t);
        }
        break;
    }
    case Glyph::Accessibility: {
        ren.drawCircle({c.x, c.y - 6.0f * s}, 2.4f * s, color);
        ren.drawLine({c.x, c.y - 3.2f * s}, {c.x, c.y + 2.5f * s}, color, t);
        ren.drawLine({c.x - 5.5f * s, c.y - 0.5f * s}, {c.x + 5.5f * s, c.y - 0.5f * s}, color, t);
        ren.drawLine({c.x, c.y + 2.5f * s}, {c.x - 4.0f * s, c.y + 8.5f * s}, color, t);
        ren.drawLine({c.x, c.y + 2.5f * s}, {c.x + 4.0f * s, c.y + 8.5f * s}, color, t);
        break;
    }
    case Glyph::Storage: {
        ren.drawRoundedRect({c.x - 8.5f * s, c.y - 7.5f * s, 17.f * s, 5.2f * s}, color, 1.6f * s);
        ren.drawRoundedRect({c.x - 8.5f * s, c.y - 1.6f * s, 17.f * s, 5.2f * s},
                            color.withAlpha(color.a * 0.78f), 1.6f * s);
        ren.drawRoundedRect({c.x - 8.5f * s, c.y + 4.3f * s, 17.f * s, 5.2f * s},
                            color.withAlpha(color.a * 0.55f), 1.6f * s);
        break;
    }
    case Glyph::Audio: {
        ren.drawRoundedRect({c.x - 8.0f * s, c.y - 2.8f * s, 4.8f * s, 5.6f * s}, color, 1.2f * s);
        ren.drawTriangle({c.x - 3.4f * s, c.y - 6.2f * s},
                         {c.x + 2.8f * s, c.y},
                         {c.x - 3.4f * s, c.y + 6.2f * s}, color);
        for (float r : {5.5f * s, 8.5f * s}) {
            ren.drawLine({c.x + 3.5f * s, c.y - r * 0.55f},
                         {c.x + 3.5f * s + r * 0.35f, c.y - r}, color, t);
            ren.drawLine({c.x + 3.5f * s, c.y + r * 0.55f},
                         {c.x + 3.5f * s + r * 0.35f, c.y + r}, color, t);
        }
        break;
    }
    case Glyph::Display: {
        ren.drawRoundedRect({c.x - 9.0f * s, c.y - 6.5f * s, 18.f * s, 12.5f * s},
                            color.withAlpha(color.a * 0.22f), 2.2f * s);
        ren.drawRoundedRectOutline({c.x - 9.0f * s, c.y - 6.5f * s, 18.f * s, 12.5f * s},
                                   color, 2.2f * s, t);
        ren.drawLine({c.x - 3.5f * s, c.y + 8.0f * s}, {c.x + 3.5f * s, c.y + 8.0f * s}, color, t);
        ren.drawLine({c.x, c.y + 6.0f * s}, {c.x, c.y + 8.0f * s}, color, t);
        break;
    }
    case Glyph::Internet: {
        ren.drawCircle(c, 1.6f * s, color);
        for (float r : {5.0f * s, 8.5f * s}) {
            constexpr int steps = 7;
            nxui::Vec2 prev;
            for (int i = 0; i <= steps; ++i) {
                const float deg = 225.f + (90.f * (float)i / (float)steps);
                const float rad = deg * (3.14159265f / 180.f);
                const nxui::Vec2 pt = {c.x + r * std::cos(rad), c.y + r * std::sin(rad)};
                if (i > 0) ren.drawLine(prev, pt, color, t);
                prev = pt;
            }
        }
        break;
    }
    case Glyph::SteamGridDb: {
        const float cell = 5.2f * s;
        const float gap = 1.6f * s;
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 2; ++col) {
                ren.drawRoundedRect({
                    c.x - cell - gap * 0.5f + (float)col * (cell + gap),
                    c.y - cell - gap * 0.5f + (float)row * (cell + gap),
                    cell, cell
                }, color, 1.2f * s);
            }
        }
        break;
    }
    case Glyph::Controllers: {
        const nxui::Rect body = {c.x - 10.f * s, c.y - 5.5f * s, 20.f * s, 11.f * s};
        ren.drawRoundedRect(body, color.withAlpha(color.a * 0.18f), 4.5f * s);
        ren.drawRoundedRectOutline(body, color, 4.5f * s, t);
        ren.drawCircle({c.x - 4.2f * s, c.y}, 1.7f * s, color);
        ren.drawCircle({c.x + 3.6f * s, c.y - 1.8f * s}, 1.2f * s, color);
        ren.drawCircle({c.x + 5.6f * s, c.y}, 1.2f * s, color);
        ren.drawCircle({c.x + 3.6f * s, c.y + 1.8f * s}, 1.2f * s, color);
        break;
    }
    case Glyph::Bluetooth: {
        ren.drawLine({c.x, c.y - 8.5f * s}, {c.x, c.y + 8.5f * s}, color, t);
        ren.drawLine({c.x, c.y - 8.5f * s}, {c.x + 5.5f * s, c.y - 3.5f * s}, color, t);
        ren.drawLine({c.x + 5.5f * s, c.y - 3.5f * s}, {c.x - 5.0f * s, c.y + 3.0f * s}, color, t);
        ren.drawLine({c.x - 5.0f * s, c.y - 3.0f * s}, {c.x + 5.5f * s, c.y + 3.5f * s}, color, t);
        ren.drawLine({c.x + 5.5f * s, c.y + 3.5f * s}, {c.x, c.y + 8.5f * s}, color, t);
        break;
    }
    case Glyph::Sleep: {
        const int R = (int)std::round(7.0f * s);
        const float r = 5.8f * s;
        const float dx = 2.4f * s;
        for (int yOff = -R; yOff <= R; ++yOff) {
            const float valOut = (float)(R * R - yOff * yOff);
            if (valOut < 0.f) continue;
            const float xOutL = -std::sqrt(valOut);
            const float xOutR = std::sqrt(valOut);
            const float valIn = r * r - (float)(yOff * yOff);
            const float xInL = (valIn >= 0.f) ? (dx - std::sqrt(valIn)) : xOutR;
            const float x1 = c.x + xOutL;
            const float x2 = c.x + std::min(xOutR, xInL);
            if (x2 > x1)
                ren.drawLine({x1, c.y + (float)yOff}, {x2, c.y + (float)yOff}, color, 1.f);
        }
        break;
    }
    case Glyph::About: {
        ren.drawCircle(c, 9.0f * s, color.withAlpha(color.a * 0.16f));
        ren.drawRoundedRectOutline({c.x - 9.0f * s, c.y - 9.0f * s, 18.f * s, 18.f * s},
                                   color, 9.0f * s, t);
        ren.drawCircle({c.x, c.y - 4.2f * s}, 1.5f * s, color);
        ren.drawRoundedRect({c.x - 1.2f * s, c.y - 0.8f * s, 2.4f * s, 7.2f * s}, color, 1.1f * s);
        break;
    }
    case Glyph::Themes: {
        ren.drawCircle({c.x - 3.2f * s, c.y - 1.5f * s}, 5.2f * s, color.withAlpha(color.a * 0.55f));
        ren.drawCircle({c.x + 3.2f * s, c.y - 1.5f * s}, 5.2f * s, color.withAlpha(color.a * 0.75f));
        ren.drawCircle({c.x, c.y + 3.2f * s}, 5.2f * s, color);
        break;
    }
    case Glyph::ThemeShop: {
        ren.drawRoundedRect({c.x - 7.5f * s, c.y - 2.0f * s, 15.f * s, 9.5f * s},
                            color.withAlpha(color.a * 0.22f), 2.0f * s);
        ren.drawRoundedRectOutline({c.x - 7.5f * s, c.y - 2.0f * s, 15.f * s, 9.5f * s},
                                   color, 2.0f * s, t);
        ren.drawLine({c.x - 4.5f * s, c.y - 2.0f * s}, {c.x - 2.5f * s, c.y - 7.0f * s}, color, t);
        ren.drawLine({c.x + 4.5f * s, c.y - 2.0f * s}, {c.x + 2.5f * s, c.y - 7.0f * s}, color, t);
        ren.drawLine({c.x - 2.5f * s, c.y - 7.0f * s}, {c.x + 2.5f * s, c.y - 7.0f * s}, color, t);
        break;
    }
    case Glyph::ThemeIcons: {
        const float cell = 4.6f * s;
        const float gap = 1.5f * s;
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 2; ++col) {
                ren.drawRoundedRect({
                    c.x - cell - gap * 0.5f + (float)col * (cell + gap),
                    c.y - cell - gap * 0.5f + (float)row * (cell + gap),
                    cell, cell
                }, color, 1.1f * s);
            }
        }
        break;
    }
    case Glyph::IconShop: {
        ren.drawRoundedRect({c.x - 7.5f * s, c.y - 1.2f * s, 15.f * s, 8.8f * s},
                            color.withAlpha(color.a * 0.20f), 2.0f * s);
        ren.drawRoundedRectOutline({c.x - 7.5f * s, c.y - 1.2f * s, 15.f * s, 8.8f * s},
                                   color, 2.0f * s, t);
        ren.drawRoundedRect({c.x - 3.2f * s, c.y - 7.8f * s, 6.4f * s, 6.4f * s}, color, 1.2f * s);
        break;
    }
    case Glyph::Sounds: {
        ren.drawRoundedRect({c.x - 8.0f * s, c.y - 2.8f * s, 4.8f * s, 5.6f * s}, color, 1.2f * s);
        ren.drawTriangle({c.x - 3.4f * s, c.y - 6.2f * s},
                         {c.x + 2.8f * s, c.y},
                         {c.x - 3.4f * s, c.y + 6.2f * s}, color);
        for (float r : {5.5f * s, 8.5f * s}) {
            ren.drawLine({c.x + 3.5f * s, c.y - r * 0.55f},
                         {c.x + 3.5f * s + r * 0.35f, c.y - r}, color, t);
            ren.drawLine({c.x + 3.5f * s, c.y + r * 0.55f},
                         {c.x + 3.5f * s + r * 0.35f, c.y + r}, color, t);
        }
        break;
    }
    case Glyph::Interface: {
        ren.drawRoundedRect({c.x - 8.5f * s, c.y - 6.5f * s, 17.f * s, 3.2f * s}, color, 1.4f * s);
        ren.drawRoundedRect({c.x - 8.5f * s, c.y - 1.6f * s, 11.f * s, 3.2f * s},
                            color.withAlpha(color.a * 0.75f), 1.4f * s);
        ren.drawRoundedRect({c.x - 8.5f * s, c.y + 3.3f * s, 14.f * s, 3.2f * s},
                            color.withAlpha(color.a * 0.55f), 1.4f * s);
        break;
    }
    case Glyph::LockScreen: {
        ren.drawRoundedRect({c.x - 6.5f * s, c.y - 1.0f * s, 13.f * s, 9.5f * s},
                            color.withAlpha(color.a * 0.22f), 2.0f * s);
        ren.drawRoundedRectOutline({c.x - 6.5f * s, c.y - 1.0f * s, 13.f * s, 9.5f * s},
                                   color, 2.0f * s, t);
        ren.drawRoundedRectOutline({c.x - 3.5f * s, c.y - 7.5f * s, 7.f * s, 7.f * s},
                                   color, 3.5f * s, t);
        ren.drawCircle({c.x, c.y + 3.2f * s}, 1.4f * s, color);
        break;
    }
    case Glyph::None:
    default:
        break;
    }
}

class SettingsTabWidget final : public nxui::GlassBox {
public:
    explicit SettingsTabWidget(const std::string& text)
        : nxui::GlassBox(nxui::Axis::ROW) {
        setCornerRadius(18.f);
        setBorderWidth(1.f);
        setWireframeEnabled(false);

        m_label = std::make_shared<nxui::Label>(text);
        m_label->setHAlign(nxui::Label::HAlign::Left);
        m_label->setVAlign(nxui::Label::VAlign::Center);
        addChild(m_label);
    }

    void setTopRail(bool topRail) { m_topRail = topRail; }
    void setIconOnly(bool iconOnly) { m_iconOnly = iconOnly; }
    void setGlyph(TabbedOverlayScreen::TabGlyph glyph) { m_glyph = glyph; }

    void sync(const std::string& text,
              nxui::Font* font,
              const nxui::Theme* theme,
              bool selected,
              bool focused,
              float uiTime,
              float accentWidth) {
        (void)uiTime;
        m_selected = selected;
        m_focused = focused;
        m_accentWidth = accentWidth;
        m_accentColor = theme ? theme->cursorNormal : nxui::Color::white();
        m_iconColor = theme
            ? (selected ? theme->textPrimary : theme->textSecondary)
            : nxui::Color::white();

        if (font != m_cachedFont) {
            m_cachedFont = font;
            if (font) {
                m_label->setFont(font);
            }
        }
        if (m_cachedText != text) {
            m_cachedText = text;
            m_label->setText(m_cachedText);
        }

        if (m_iconOnly) {
            m_label->setOpacity(0.f);
            m_label->setRect({rect().x, rect().y, 0.f, 0.f});
        } else {
            m_label->setHAlign(m_topRail ? nxui::Label::HAlign::Center
                                         : nxui::Label::HAlign::Left);

            const float padX = m_topRail ? 10.f : 22.f;
            const float padTotal = padX * 2.f;
            float textScale = selected ? 0.91f : 0.87f;
            if (font && rect().width > padTotal + 4.f) {
                const float measured = font->measure(text).x;
                if (measured > 0.f)
                    textScale = std::min(textScale, (rect().width - padTotal) / measured);
                textScale = std::max(0.55f, textScale);
            }
            if (std::abs(m_cachedTextScale - textScale) > 0.001f) {
                m_cachedTextScale = textScale;
                m_label->setScale(textScale);
            }
            m_label->setOpacity(opacity());
            m_label->setRect({rect().x + padX, rect().y,
                              std::max(0.f, rect().width - padTotal), rect().height});
        }

        if (theme) {
            nxui::Color textColor = selected ? theme->textPrimary : theme->textSecondary;
            nxui::Color baseColor = theme->panelBase.withAlpha(selected ? 0.94f : 0.88f);
            nxui::Color borderColor = selected
                ? theme->cursorNormal.withAlpha(focused ? 0.70f : 0.46f)
                : theme->panelBorder.withAlpha(focused ? 0.34f : 0.22f);
            nxui::Color hiColor = theme->panelHighlight.withAlpha(selected ? 0.12f : 0.06f);

            setBaseColor(baseColor);
            setBorderColor(borderColor);
            setHighlightColor(hiColor);
            setBorderWidth(selected || focused ? 1.2f : 1.f);
            setCornerRadius(m_iconOnly ? 14.f : 18.f);
            setScale(selected ? 1.01f : 1.f);

            m_label->setTextColor(textColor);
        }
    }

protected:
    void onRender(nxui::Renderer& ren) override {
        nxui::GlassBox::onRender(ren);

        nxui::Rect r = rect();
        if (m_iconOnly && m_glyph != TabbedOverlayScreen::TabGlyph::None && opacity() > 0.01f) {
            const float iconScale = std::clamp(std::min(r.width, r.height) / 42.f, 0.72f, 1.05f);
            drawTabGlyph(ren, m_glyph,
                         {r.x + r.width * 0.5f, r.y + r.height * 0.48f},
                         m_iconColor.withAlpha(m_iconColor.a * opacity()),
                         iconScale);
        }

        if (!m_selected || opacity() <= 0.01f)
            return;

        if (m_topRail) {
            const float underlineW = m_iconOnly
                ? std::max(14.f, r.width * 0.42f)
                : std::max(20.f, r.width * 0.55f);
            nxui::Rect underline = {
                r.x + (r.width - underlineW) * 0.5f,
                r.bottom() - 5.f,
                underlineW,
                3.f
            };
            ren.drawRoundedRect(underline, m_accentColor.withAlpha(0.92f * opacity()), 1.5f);
            return;
        }

        float accentH = std::max(16.f, r.height * 0.46f);
        float accentY = r.y + (r.height - accentH) * 0.5f;
        float accentW = std::clamp(m_accentWidth, 2.f, std::max(2.f, r.width * 0.12f));
        nxui::Rect accent = {r.x + 8.f, accentY, accentW, accentH};
        ren.drawRoundedRect(accent, m_accentColor.withAlpha(0.92f * opacity()), 2.f);

        nxui::Rect underline = {r.x + 16.f, r.bottom() - 5.f, std::max(24.f, r.width - 32.f), 2.f};
        ren.drawRoundedRect(underline, m_accentColor.withAlpha(0.18f * opacity()), 1.f);
    }

private:
    std::shared_ptr<nxui::Label> m_label;
    bool m_selected = false;
    bool m_focused = false;
    bool m_topRail = false;
    bool m_iconOnly = false;
    TabbedOverlayScreen::TabGlyph m_glyph = TabbedOverlayScreen::TabGlyph::None;
    float m_accentWidth = 3.f;
    nxui::Color m_accentColor = nxui::Color::white();
    nxui::Color m_iconColor = nxui::Color::white();
    nxui::Font* m_cachedFont = nullptr;
    std::string m_cachedText;
    float m_cachedTextScale = -1.f;
};

class SettingsItemCard final : public nxui::GlassBox {
public:
    SettingsItemCard(TabbedOverlayScreen::SettingItem& item,
                     std::shared_ptr<nxui::Box> content)
        : nxui::GlassBox(nxui::Axis::ROW)
        , m_item(item)
        , m_content(std::move(content)) {
        setAlignItems(nxui::AlignItems::CENTER);
        setJustifyContent(nxui::JustifyContent::FLEX_START);
        setWireframeEnabled(false);
        addChild(m_content);
    }

    void sync(const nxui::Theme* theme, bool selected, float alpha) {
        const bool isSection = (m_item.type == TabbedOverlayScreen::ItemType::Section);
        const bool isActionLike = m_item.type == TabbedOverlayScreen::ItemType::Action
            || m_item.type == TabbedOverlayScreen::ItemType::Selector;

        setCornerRadius(isSection ? 14.f : 18.f);
        setBorderWidth(isSection ? 0.f : 1.f);
        setScale(selected ? 1.008f : 1.f);

        if (theme) {
            float baseAlpha = isSection ? 0.0f
                : selected ? 0.95f
                : isActionLike ? 0.91f
                : m_item.focusable() ? 0.89f : 0.86f;
            float borderAlpha = isSection ? 0.0f
                : selected ? 0.58f
                : isActionLike ? 0.30f : 0.24f;
            float hiAlpha = selected ? 0.12f : (isSection ? 0.0f : 0.06f);

            setBaseColor(theme->panelBase.withAlpha(baseAlpha));
            setBorderColor((selected ? theme->cursorNormal : theme->panelBorder).withAlpha(borderAlpha));
            setHighlightColor(theme->panelHighlight.withAlpha(hiAlpha));
        }

        float insetX = isSection ? 6.f : 14.f;
        float insetY = isSection ? 4.f : 6.f;
        m_content->setRect({
            rect().x + insetX,
            rect().y + insetY,
            std::max(0.f, rect().width - insetX * 2.f),
            std::max(0.f, rect().height - insetY * 2.f)
        });
        m_content->setOpacity(alpha);
    }

private:
    TabbedOverlayScreen::SettingItem& m_item;
    std::shared_ptr<nxui::Box> m_content;
};

} // namespace


void TabbedOverlayScreen::setCanvasSize(float width, float height) {
    m_canvasW = width > 1.f ? width : 1280.f;
    m_canvasH = height > 1.f ? height : 720.f;
    applyCanvasChrome();
    if (m_tabBar) {
        rebuildTabBar();
        rebuildContentItems();
    }
}

void TabbedOverlayScreen::applyCanvasChrome() {
    if (useTopTabRail()) {
        constexpr float kMarginX = 16.f;
        constexpr float kMarginY = 28.f;
        setRect({kMarginX, kMarginY,
                 m_canvasW - 2.f * kMarginX,
                 m_canvasH - 2.f * kMarginY});
        return;
    }
    if (m_mode == ScreenMode::GameOptions || m_mode == ScreenMode::FolderOptions) {
        constexpr float kWidth = 920.f;
        const float width = std::min(kWidth, m_canvasW - 2.f * kPanelMargin);
        const float height = std::min(656.f, m_canvasH - 64.f);
        setRect({(m_canvasW - width) * 0.5f, 32.f, width, height});
        return;
    }
    setRect({kPanelMargin, kPanelMargin,
             m_canvasW - 2.f * kPanelMargin,
             m_canvasH - 2.f * kPanelMargin});
}

void TabbedOverlayScreen::layoutTabChildren(const nxui::Rect& tr) {
    auto& tabChildren = m_tabBar->children();
    const bool iconOnly = useIconOnlyTopTabs();
    const float rowH = iconOnly ? 52.f : kTabRowHeight;
    const float tabH = rowH - kTabCardGap;
    if (useTopTabRail()) {
        const int n = std::max(1, (int)tabChildren.size());
        const float gap = iconOnly ? 4.f : 8.f;
        const float avail = std::max(0.f, tr.width - kTabRailInset * 2.f
            - gap * static_cast<float>(n - 1));
        const float tabW = avail / static_cast<float>(n);
        float x = tr.x + kTabRailInset;
        const float y = tr.y + kTabRailInset;
        for (int i = 0; i < (int)tabChildren.size(); ++i) {
            tabChildren[i]->setRect({x, y, tabW, tabH});
            x += tabW + gap;
        }
        return;
    }

    float tabY = tr.y + kTabRailInset - m_tabScrollY;
    const float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);
    for (int i = 0; i < (int)tabChildren.size(); ++i) {
        tabChildren[i]->setRect({tr.x + kTabRailInset, tabY, tabW, tabH});
        tabY += tabH + kTabCardGap + tabBranchExtraHeight(i);
    }
}

TabbedOverlayScreen::TabbedOverlayScreen(ScreenMode mode)
    : m_mode(mode) {
    setFrameworkTouchEnabled(false);
    setRect({kPanelMargin, kPanelMargin,
             1280.f - 2.f * kPanelMargin, 720.f - 2.f * kPanelMargin});
    setVisible(false);
    setOpacity(0.001f);
    setScale(0.92f);
    setCornerRadius(kPanelRadius);
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setBlurEnabled(false);
    setBlurRadius(kSettingsBlurRadius);
    setBlurPasses(kSettingsBlurIter);
    setPanelOpacity(0.96f);

    m_focusCursor.setBorderWidth(2.6f);
    m_focusCursor.setCornerRadius(10.f);
    m_tabReveal.setImmediate(1.f);
    m_dropdownAnim.setImmediate(0.f);
    m_trackToastAnim.setImmediate(0.f);
    m_backgroundPeek.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_contentSlideAnim.setImmediate(1.f);
    m_tabAccentW.setImmediate(3.f);

    m_tabBar = std::make_shared<nxui::GlassBox>(nxui::Axis::COLUMN);
    m_tabBar->setTag("tabBar");
    m_tabBar->setWireframeEnabled(false);

    m_tabContent = std::make_shared<nxui::GlassBox>(nxui::Axis::COLUMN);
    m_tabContent->setTag("tabContent");
    m_tabContent->setWireframeEnabled(false);

    rebuildTabBar();
    rebuildContentItems();

    m_i18nListenerId = nxui::I18n::instance().addLanguageChangedListener([this]() {
        m_deferredRefresh = true;
    });
}
TabbedOverlayScreen::~TabbedOverlayScreen() {
    nxui::I18n::instance().removeLanguageChangedListener(m_i18nListenerId);
}

float TabbedOverlayScreen::itemHeight(const SettingItem& item, float contentWidth) const {
    if (item.hidden) return 0.f;
    if (item.type == ItemType::Section) return kSectionHeight;

    float height = kRowHeight;
    if (item.wrapLabel) {
        // Content card inset (36), card content inset (28), row inset (40).
        const float labelWidth = std::max(1.f, contentWidth - 104.f);

        std::string key = wrapHeightCacheKey(m_font, item.label, labelWidth);
        auto cached = g_wrapHeightCache.find(key);
        if (cached != g_wrapHeightCache.end()) {
            height = cached->second;
        } else {
            nxui::Label probe(item.label);
            if (m_font) probe.setFont(m_font);
            probe.setScale(0.94f);
            probe.setMultiline(true);
            height = std::max(kRowHeight, probe.measureWrappedText(labelWidth).y + 34.f);

            if (g_wrapHeightCache.size() >= kWrapHeightCacheLimit)
                g_wrapHeightCache.clear();
            g_wrapHeightCache.emplace(std::move(key), height);
        }
    }
    return height + std::max(0.f, item.extraHeight);
}

void TabbedOverlayScreen::setTheme(const nxui::Theme* t) {
    m_theme = t;
    if (!m_theme)
        return;

    setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f));
    setBorderColor(m_theme->panelBorder.withAlpha(0.42f));
    setHighlightColor(m_theme->panelHighlight.withAlpha(0.10f));
    setLiquidGlassShade(m_theme->mode == nxui::ThemeMode::Dark ? 0.08f : -0.03f);
    invalidateBackdropCache();

    // Row widgets cache the colours they took from the theme (label/description
    // text, values), and they key that cache on the theme pointer. The menu
    // recolours one and the same Theme object in place, so those colours would
    // otherwise survive a theme switch: Theme Shop -> Sounds / Interface kept
    // black text over a black theme (or white on white). Drop the cache for
    // every tab, including the ones a custom layout renders itself, so the
    // settings-style tabs rebuild on next use.
    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());

    if (!usesCustomContentLayout() && isActive())
        rebuildContentItems();
}

void TabbedOverlayScreen::show() {
    if (m_active) return;
    DebugLog::log("[settings] show()");
    if (m_tabs.empty())
        warmup();
    m_active    = true;
    m_animating = true;
    m_showing   = true;
    m_animT     = 0.f;
    m_focusArea  = FocusArea::Tabs;
    m_tabIndex   = 0;
    m_contentIdx = 0;
    m_scrollY    = 0.f;
    m_scrollTarget = 0.f;
    m_tabScrollY = 0.f;
    m_tabScrollTarget = 0.f;
    m_tabReveal.setImmediate(0.f);
    m_tabReveal.set(1.f, 0.24f, nxui::Easing::outCubic);
    m_dropdownOpen = false;
    m_dropdownClosing = false;
    m_dropdownRawIdx = -1;
    m_dropdownHover = 0;
    m_dropdownAnim.setImmediate(0.f);
    m_touchDirectControl = false;
    m_ignoreInitialTouchRelease = true;
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_contentSlideAnim.setImmediate(1.f);
    // Opening the panel already fades the content in via m_tabReveal; the
    // cascade is reserved for tab switches.
    m_contentStaggerT = kContentStaggerDone;
    m_tabAccentW.setImmediate(3.f);
    if (m_tabBar) rebuildTabBar();
    if (m_tabContent) rebuildContentItems();
    invalidateBackdropCache();

    setVisible(true);
    syncPanelState(0.f);
    setFocusable(true);
    setupActions();
    announceCurrentFocus();
}

void TabbedOverlayScreen::hide() {
    if (!m_active) return;
    DebugLog::log("[settings] hide()");
    if (m_closeSfxCb) m_closeSfxCb();
    closeDropdown(false);
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_animating = true;
    m_showing   = false;
    m_animT     = 0.f;

    setFocusable(false);
    clearActions();
}

TabbedOverlayScreen::DropdownGeom TabbedOverlayScreen::layoutDropdown(const nxui::Rect& cr) const {
    DropdownGeom geom;
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size() || m_dropdownRawIdx < 0)
        return geom;

    auto& items = m_tabs[m_tabIndex].items;
    if (m_dropdownRawIdx >= (int)items.size())
        return geom;

    auto& item = items[m_dropdownRawIdx];
    if (item.type != ItemType::Selector || item.options.empty())
        return geom;

    constexpr float kContentTopPad = 16.f;
    constexpr float kGap = 6.f;
    constexpr float kListPad = 16.f;
    constexpr float kEdgePad = 4.f;
    constexpr int kMaxVisible = 6;

    float rowY = cr.y + kContentTopPad - m_scrollY;
    for (int i = 0; i < m_dropdownRawIdx; ++i)
        rowY += itemHeight(items[i], cr.width);
    float rowH = itemHeight(item, cr.width);

    geom.ctrlX = cr.x + cr.width * 0.40f;
    geom.ctrlW = cr.width * 0.60f;
    geom.optH = 46.f;

    int total = (int)item.options.size();
    float topLimit = cr.y + kEdgePad;
    float bottomLimit = cr.bottom() - kEdgePad;
    float spaceBelow = bottomLimit - (rowY + rowH + kGap);
    float spaceAbove = (rowY - kGap) - topLimit;
    float avail = std::max(0.f, bottomLimit - topLimit);

    auto heightFor = [&](int vis) {
        return (float)vis * geom.optH + kListPad;
    };

    int visible = std::min(total, kMaxVisible);
    float fitSpace = std::max(spaceBelow, spaceAbove);
    while (visible > 1 && heightFor(visible) > fitSpace + 0.5f)
        --visible;
    if (heightFor(visible) > avail + 0.5f)
        visible = std::max(1, (int)std::floor((avail - kListPad) / geom.optH));
    visible = std::clamp(visible, 1, std::min(total, kMaxVisible));

    geom.visible = visible;
    geom.listH = heightFor(visible);

    bool placeBelow = spaceBelow >= geom.listH || spaceBelow >= spaceAbove;
    geom.dy = placeBelow ? (rowY + rowH + kGap) : (rowY - geom.listH - kGap);
    if (geom.dy + geom.listH > bottomLimit)
        geom.dy = bottomLimit - geom.listH;
    if (geom.dy < topLimit)
        geom.dy = topLimit;

    geom.valid = true;
    return geom;
}

int TabbedOverlayScreen::dropdownVisibleCount(int optionCount) const {
    if (!m_dropdownOpen && !m_dropdownClosing)
        return std::min(optionCount, 6);
    if (m_dropdownVisible > 0)
        return std::min(optionCount, m_dropdownVisible);
    return std::min(optionCount, 6);
}

void TabbedOverlayScreen::openDropdown(int rawIdx) {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
        return;

    auto& items = m_tabs[m_tabIndex].items;
    if (rawIdx < 0 || rawIdx >= (int)items.size())
        return;

    auto& item = items[rawIdx];
    if (item.type != ItemType::Selector || item.options.empty())
        return;

    m_dropdownOpen = true;
    m_dropdownClosing = false;
    m_dropdownRawIdx = rawIdx;
    m_dropdownHover = std::clamp(item.intVal, 0, std::max(0, (int)item.options.size() - 1));

    DropdownGeom geom = layoutDropdown(contentRect());
    int visible = geom.valid ? geom.visible : std::min((int)item.options.size(), 6);
    m_dropdownVisible = visible;
    m_dropdownVisualStart = (item.options.size() > (size_t)visible)
        ? (float)std::clamp(m_dropdownHover - visible / 2, 0, (int)item.options.size() - visible)
        : 0.f;
    m_dropdownAnim.set(1.f, 0.18f, nxui::Easing::outCubic);
}

void TabbedOverlayScreen::closeDropdown(bool animated) {
    m_dropdownOpen = false;
    m_dropdownClosing = animated && m_dropdownRawIdx >= 0;
    if (animated && m_dropdownRawIdx >= 0) {
        m_dropdownAnim.set(0.f, 0.16f, nxui::Easing::outCubic);
    } else {
        m_dropdownClosing = false;
        m_dropdownRawIdx = -1;
        m_dropdownAnim.setImmediate(0.f);
    }
}


float TabbedOverlayScreen::tabRailContentHeight() const {
    float height = kTabRailInset * 2.f + kTabRowHeight * (float)m_tabs.size();
    for (int i = 0; i < (int)m_tabs.size(); ++i)
        height += tabBranchExtraHeight(i);
    return height;
}

void TabbedOverlayScreen::activateTopRailSlot(int slot) {
    if (slot < 0 || slot >= (int)m_tabs.size())
        return;
    m_focusArea = FocusArea::Tabs;
    if (m_tabIndex != slot) {
        m_tabSwitchDir = (slot > m_tabIndex) ? 1 : -1;
        m_tabIndex = slot;
        m_contentIdx = 0;
        m_scrollY = 0.f;
        m_scrollTarget = 0.f;
        m_tabReveal.setImmediate(0.f);
        m_tabReveal.set(1.f, 0.24f, nxui::Easing::outCubic);
        m_contentSlideAnim.setImmediate(0.f);
        m_contentSlideAnim.set(1.f, 0.28f, nxui::Easing::outCubic);
        m_contentStaggerT = 0.f;
        m_tabAccentW.setImmediate(1.f);
        m_tabAccentW.set(3.f, 0.32f, nxui::Easing::outExpo);
        closeDropdown(false);
        rebuildContentItems();
    }
    onTabRailParentActivated(slot);
}

void TabbedOverlayScreen::rebuildTabBar() {
    m_tabBar->clearChildren();
    nxui::Rect tr = tabsRect();
    m_tabBar->setRect(tr);

    const bool topRail = useTopTabRail();
    const bool iconOnly = useIconOnlyTopTabs();
    const int slotCount = topRail ? topRailSlotCount() : (int)m_tabs.size();
    for (int i = 0; i < slotCount; ++i) {
        const std::string title = topRail ? topRailSlotTitle(i)
            : (i < (int)m_tabs.size() ? m_tabs[(size_t)i].name : std::string{});
        auto tabBox = std::make_shared<SettingsTabWidget>(title);
        tabBox->setTag(title);
        tabBox->setTopRail(topRail);
        tabBox->setIconOnly(iconOnly);
        tabBox->setGlyph(topRail ? topRailSlotGlyph(i)
            : (i < (int)m_tabs.size() ? m_tabs[(size_t)i].glyph : TabGlyph::None));
        m_tabBar->addChild(tabBox);
    }
    layoutTabChildren(tr);
}

void TabbedOverlayScreen::rebuildContentItems() {
    m_tabContent->clearChildren();
    nxui::Rect cr = contentRect();
    m_tabContent->setRect(cr);

    if (usesCustomContentLayout())
        return;

    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    // Screens that build tabs after setTheme() can leave this cache at size 0
    // while m_tabs is already populated (Theme Shop Sounds/Interface path).
    if (m_cachedTabContentWidgets.size() != m_tabs.size())
        m_cachedTabContentWidgets.resize(m_tabs.size());
    ensureTabLoaded(m_tabIndex);
    auto& items = m_tabs[m_tabIndex].items;
    auto& cache = m_cachedTabContentWidgets[(size_t)m_tabIndex];
    // DebugLog::log("[settings] rebuildContent tab=%d items=%d cache=%s",
    //               m_tabIndex, (int)items.size(), cache.empty() ? "miss" : "hit");

    if (cache.empty()) {
        cache.reserve(items.size());

        float y = 0.f;
        for (int i = 0; i < (int)items.size(); ++i) {
            float h = itemHeight(items[i], cr.width);
            float insetY = (items[i].type == ItemType::Section) ? 1.f : kContentCardInsetY;
            float cardH = std::max(0.f, h - (items[i].type == ItemType::Section ? 2.f : 6.f));
            auto itemBox = makeItemWidget(items[i]);
            itemBox->setTag(items[i].label);
            itemBox->setRect({cr.x + kContentCardInsetX, cr.y + y + insetY,
                              std::max(0.f, cr.width - kContentCardInsetX * 2.f), cardH});
            cache.push_back(itemBox);
            y += h;
        }
    }

    for (auto& itemBox : cache) {
        m_tabContent->addChild(itemBox);
    }
}

std::shared_ptr<nxui::Box> TabbedOverlayScreen::makeItemWidget(SettingItem& item) {
    settings::widgets::SettingWidgetContext ctx;
    ctx.font = &m_font;
    ctx.smallFont = &m_smallFont;
    ctx.theme = &m_theme;
    auto content = settings::widgets::createSettingItemWidget(item, ctx);
    return std::make_shared<SettingsItemCard>(item, content);
}

void TabbedOverlayScreen::onRender(nxui::Renderer& ren) {
    if (!m_active && !m_animating)
        return;

    float opacity = visibilityProgress();
    nxui::Rect p = panelRect(scale());

    if (m_theme)
        m_focusCursor.setColor(m_theme->cursorNormal);
    m_focusCursor.setOpacity(opacity);

    if (m_tabBar) m_tabBar->setRect(tabsRect(p));
    if (m_tabContent) m_tabContent->setRect(contentRect(p));

    const float peek = backgroundPeekAmount();
    const float chrome = 1.f - peek;
    drawBackground(ren, p, opacity * 0.72f * chrome);

    if (opacity > 0.01f && chrome > 0.01f) {
        const nxui::Color panel = m_theme ? m_theme->panelBase : m_base;
        const nxui::Color border = m_theme
            ? m_theme->panelBorder
            : nxui::Color::white().withAlpha(0.18f);
        const nxui::Color highlight = m_theme
            ? m_theme->panelHighlight
            : nxui::Color::white().withAlpha(0.08f);
        ren.drawRoundedRect({p.x, p.y + 7.f, p.width, p.height},
                            nxui::Color(0.f, 0.f, 0.f, 0.20f * opacity * chrome),
                            kPanelRadius);
        ren.drawRoundedRect(p, panel.withAlpha(0.98f * opacity * chrome), kPanelRadius);
        ren.drawRoundedRectOutline(p.shrunk(1.f),
                                   highlight.withAlpha(0.28f * opacity * chrome),
                                   std::max(0.f, kPanelRadius - 1.f), 1.f);
        ren.drawRoundedRectOutline(p, border.withAlpha(0.72f * opacity * chrome),
                                   kPanelRadius, 1.5f);
    }

    onContentRender(ren);

    if (ren.boxWireframeEnabled()) {
        syncDebugWireframeRects(p);
        if (m_tabBar) m_tabBar->render(ren);
        if (m_tabContent) m_tabContent->render(ren);
    }

    m_focusCursor.render(ren);
}

void TabbedOverlayScreen::onContentRender(nxui::Renderer& ren) {
    float opacity = visibilityProgress();
    nxui::Rect p = panelRect(scale());
    float textOp = m_showing ? opacity : opacity * opacity;
    const float peek = backgroundPeekAmount();
    const float chrome = 1.f - peek;
    // Tabs stay faintly readable so you can still see which page you are on.
    const float tabOp = textOp * (0.18f + 0.82f * chrome);

    ren.pushClipRect(p);
    drawOverlayHeader(ren, p, textOp * chrome);
    drawTabs(ren, p, tabOp);
    drawContent(ren, p, textOp);
    drawDropdown(ren, p, textOp);
    drawTrackChangedToast(ren, p, textOp);
    ren.popClipRect();
}

void TabbedOverlayScreen::syncDebugWireframeRects(const nxui::Rect& panel) {
    if (!m_tabBar || !m_tabContent) return;

    nxui::Rect tr = tabsRect(panel);
    nxui::Rect cr = contentRect(panel);

    m_tabBar->setRect(tr);
    layoutTabChildren(tr);

    m_tabContent->setRect(cr);
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    auto& items = m_tabs[m_tabIndex].items;
    auto& itemChildren = m_tabContent->children();

    float slideT = std::clamp(m_contentSlideAnim.value(), 0.f, 1.f);
    float slideOffset = (1.f - slideT) * 24.f * (float)m_tabSwitchDir;

    float y = cr.y - m_scrollY + slideOffset;
    float x = cr.x;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = itemHeight(items[i], cr.width);
        float insetY = (items[i].type == ItemType::Section) ? 1.f : kContentCardInsetY;
        float cardH = std::max(0.f, h - (items[i].type == ItemType::Section ? 2.f : 6.f));
        itemChildren[i]->setRect({x + kContentCardInsetX, y + insetY,
                                  std::max(0.f, cr.width - kContentCardInsetX * 2.f), cardH});
        y += h;
    }
}

void TabbedOverlayScreen::drawBackground(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || opacity <= 0.01f)
        return;

    nxui::Rect screen = {0.f, 0.f, (float)ren.width(), (float)ren.height()};
    nxui::Color scrim = nxui::Color::lerp(m_theme->background, nxui::Color::black(),
                                          m_theme->mode == nxui::ThemeMode::Dark ? 0.72f : 0.28f)
        .withAlpha((m_theme->mode == nxui::ThemeMode::Dark ? 0.14f : 0.10f) * opacity);

    ren.drawRect(screen, scrim);
}

void TabbedOverlayScreen::drawTabs(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_theme || !m_tabBar) return;
    nxui::Rect tr = tabsRect(panel);
    auto* tabPanel = static_cast<nxui::GlassBox*>(m_tabBar.get());
    const bool topRail = useTopTabRail();

    tabPanel->setRect(tr);
    tabPanel->setOpacity(opacity);
    tabPanel->setCornerRadius(topRail ? 20.f : 24.f);
    tabPanel->setBorderWidth(1.f);
    tabPanel->setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.94f : 0.96f));
    tabPanel->setBorderColor(m_theme->panelBorder.withAlpha(0.30f));
    tabPanel->setHighlightColor(m_theme->panelHighlight.withAlpha(0.08f));
    tabPanel->setPanelOpacity(1.f);

    auto& tabChildren = m_tabBar->children();
    float reveal = std::clamp(m_tabReveal.value(), 0.f, 1.f);
    float tabH = kTabRowHeight - kTabCardGap;
    float rowOpacity = opacity * reveal;
    float rowYOffset = (1.f - reveal) * 6.f;
    struct BranchDraw {
        int index = -1;
        nxui::Rect rect;
    };
    std::vector<BranchDraw> branchDraws;

    if (topRail) {
        const bool iconOnly = useIconOnlyTopTabs();
        const int focusSlot = topRailFocusIndex();
        layoutTabChildren(tr);
        for (int i = 0; i < (int)tabChildren.size() && i < topRailSlotCount(); ++i) {
            auto* tab = static_cast<SettingsTabWidget*>(tabChildren[i].get());
            nxui::Rect r = tab->rect();
            r.y += rowYOffset;
            tab->setRect(r);
            tab->setTopRail(true);
            tab->setIconOnly(iconOnly);
            tab->setGlyph(topRailSlotGlyph(i));
            tab->setOpacity(rowOpacity);
            tab->sync(topRailSlotTitle(i),
                      m_font,
                      m_theme,
                      i == focusSlot,
                      m_focusArea == FocusArea::Tabs && i == focusSlot,
                      m_uiTime,
                      m_tabAccentW.value());
        }
    } else {
        float tabY = tr.y + kTabRailInset - m_tabScrollY;
        float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);

        for (int i = 0; i < (int)tabChildren.size() && i < (int)m_tabs.size(); ++i) {
            auto* tab = static_cast<SettingsTabWidget*>(tabChildren[i].get());
            tab->setTopRail(false);
            tab->setIconOnly(false);
            tab->setGlyph(TabbedOverlayScreen::TabGlyph::None);
            tab->setRect({tr.x + kTabRailInset, tabY + rowYOffset, tabW, tabH});
            tab->setOpacity(rowOpacity);
            const bool branchFocus = (i == m_tabIndex) && tabBranchConsumesFocus();
            // When a nested shop child owns focus, the parent is not the active tab.
            tab->sync(m_tabs[i].name,
                      m_font,
                      m_theme,
                      i == m_tabIndex && !branchFocus,
                      m_focusArea == FocusArea::Tabs && i == m_tabIndex && !branchFocus,
                      m_uiTime,
                      m_tabAccentW.value());
            tabY += tabH + kTabCardGap;
            const float extra = tabBranchExtraHeight(i);
            if (extra > 0.5f) {
                // Same card height as parent tabs (pitch minus gap), just indented.
                const float branchH = std::max(0.f, std::min(tabH, extra - kTabCardGap));
                branchDraws.push_back({
                    i,
                    {
                        tr.x + kTabRailInset + 20.f,
                        tabY + rowYOffset,
                        std::max(0.f, tabW - 20.f),
                        branchH
                    }
                });
                tabY += extra;
            }
        }
    }

    if (m_focusArea == FocusArea::Tabs) {
        const int focusSlot = topRail ? topRailFocusIndex() : m_tabIndex;
        if (focusSlot >= 0 && focusSlot < (int)tabChildren.size()) {
            nxui::Rect focusRect = tabChildren[focusSlot]->rect().expanded(1.f);
            (void)tabFocusCursorRect(focusRect);
            m_focusCursor.moveTo(focusRect, 16.f, 0.08f);
        }
    }

    ren.pushClipRect(tr);
    m_tabBar->render(ren);
    for (const auto& branch : branchDraws)
        drawTabBranch(ren, branch.index, branch.rect, rowOpacity);
    ren.popClipRect();

    if (topRail) {
        // Icon-only Settings / Theme Shop: active tab name as a centered glass chip.
        if (useIconOnlyTopTabs() && m_font && opacity > 0.01f) {
            const float titleH = selectedTabTitleHeight();
            const std::string title = selectedOverlayTabTitle();
            if (title.empty()) {
                // Still draw tabs above; only the pill is skipped.
            } else {
            const float scale = 0.88f;
            const nxui::Vec2 ts = m_font->measure(title);
            const float tw = ts.x * scale;
            const float th = std::max(18.f, ts.y * scale);
            constexpr float kPillPadX = 28.f;
            constexpr float kPillH = 44.f;
            const float pillW = std::clamp(tw + kPillPadX * 2.f, 120.f,
                                           std::max(120.f, tr.width));
            const float pillX = tr.x + (tr.width - pillW) * 0.5f;
            const float pillY = tr.bottom() + std::max(6.f, (titleH - kPillH) * 0.42f);
            const nxui::Rect pill = {pillX, pillY, pillW, kPillH};

            ren.drawRoundedRect(pill,
                                m_theme->panelBase.withAlpha(0.94f * opacity), 18.f);
            ren.drawRoundedRectOutline(
                pill,
                m_theme->cursorNormal.withAlpha(0.52f * opacity),
                18.f, 1.2f);
            ren.drawRoundedRectOutline(
                pill.shrunk(1.f),
                m_theme->panelHighlight.withAlpha(0.22f * opacity),
                17.f, 1.f);

            // Same top-left drawText convention as nxui::Label (not baseline).
            constexpr float kUnderlineReserve = 7.f;
            const float textBandH = std::max(th, pill.height - kUnderlineReserve);
            const float tx = pill.x + (pill.width - tw) * 0.5f;
            const float ty = pill.y + (textBandH - th) * 0.5f;
            ren.drawText(title, {tx, ty}, m_font,
                         m_theme->textPrimary.withAlpha(0.96f * opacity), scale);

            const float underlineW = std::max(28.f, pillW * 0.42f);
            ren.drawRoundedRect(
                {pill.x + (pill.width - underlineW) * 0.5f, pill.bottom() - 6.f,
                 underlineW, 3.f},
                m_theme->cursorNormal.withAlpha(0.90f * opacity), 1.5f);
            }
        }
        return;
    }

    const float totalHeight = tabRailContentHeight();
    const float maxScroll = std::max(0.f, totalHeight - tr.height);
    if (maxScroll > 0.f) {
        const float trackH = std::max(34.f, tr.height * tr.height / totalHeight);
        const float travel = std::max(0.f, tr.height - trackH - 16.f);
        const float thumbY = tr.y + 8.f + travel * std::clamp(m_tabScrollY / maxScroll, 0.f, 1.f);
        ren.drawRoundedRect({tr.right() - 7.f, thumbY, 3.f, trackH},
                            m_theme->cursorNormal.withAlpha(0.42f * opacity), 1.5f);
    }
}

void TabbedOverlayScreen::drawContent(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_smallFont || !m_theme) return;
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    if (!m_tabContent) return;

    nxui::Rect cr = contentRect(panel);
    const float peek = backgroundPeekAmount();
    const float chrome = 1.f - peek;
    auto* contentPanel = static_cast<nxui::GlassBox*>(m_tabContent.get());
    contentPanel->setRect(cr);
    contentPanel->setOpacity(opacity);
    contentPanel->setCornerRadius(26.f);
    contentPanel->setBorderWidth(chrome > 0.01f ? 1.f : 0.f);
    contentPanel->setBaseColor(m_theme->panelBase.withAlpha(
        (m_theme->mode == nxui::ThemeMode::Dark ? 0.94f : 0.96f) * chrome));
    contentPanel->setBorderColor(m_theme->panelBorder.withAlpha(0.30f * chrome));
    contentPanel->setHighlightColor(m_theme->panelHighlight.withAlpha(0.08f * chrome));
    contentPanel->setPanelOpacity(chrome);

    if (usesCustomContentLayout()) {
        contentPanel->render(ren);
        ren.pushClipRect(cr);
        drawCustomContent(ren, panel, cr, opacity);
        ren.popClipRect();
        return;
    }

    auto& items = m_tabs[m_tabIndex].items;
    auto& itemChildren = m_tabContent->children();
    int focusedRawIdx = (m_focusArea == FocusArea::Content && focusableCount() > 0)
        ? rawIndexFromFocusable(m_contentIdx) : -1;

    const float reveal = opacity * std::clamp(m_tabReveal.value(), 0.f, 1.f);

    float y = cr.y + 16.f - m_scrollY;
    float x = cr.x;
    int visibleRank = 0;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = itemHeight(items[i], cr.width);
        if (items[i].hidden || h <= 0.f) {
            items[i].contentFocused = false;
            itemChildren[i]->setVisible(false);
            continue;
        }
        float insetY = (items[i].type == ItemType::Section) ? 1.f : kContentCardInsetY;
        float cardH = std::max(0.f, h - (items[i].type == ItemType::Section ? 2.f : 6.f));
        nxui::Rect itemRect = {
            x + kContentCardInsetX,
            y + insetY,
            std::max(0.f, cr.width - kContentCardInsetX * 2.f),
            cardH
        };
        bool visible = itemRect.bottom() >= cr.y - 8.f && itemRect.y <= cr.bottom() + 8.f;
        const bool selected = (i == focusedRawIdx);
        items[i].contentFocused = selected;

        itemChildren[i]->setVisible(visible);
        if (!visible) {
            y += h;
            continue;
        }

        // Rows cascade in one after another, top to bottom. Ranking by visible
        // position rather than item index keeps the first row on screen leading
        // the cascade even when the tab was left scrolled down.
        const float rowT = std::clamp(
            (m_contentStaggerT - (float)visibleRank * kRowStaggerDelay) / kRowRevealDur,
            0.f, 1.f);
        const float rowEase = nxui::Easing::outCubic(rowT);
        ++visibleRank;

        itemRect.y += (1.f - rowEase) * 18.f * (float)m_tabSwitchDir;
        float rowOpacity = reveal * rowEase;
        if (!selected)
            rowOpacity *= chrome;

        itemChildren[i]->setRect(itemRect);
        itemChildren[i]->setOpacity(rowOpacity);

        auto* card = static_cast<SettingsItemCard*>(itemChildren[i].get());
        // Dual-button action rows (Select + ×): suppress the full-row card
        // highlight so L/R focus on the buttons themselves stays readable.
        const bool multiButton = selected
            && items[i].type == ItemType::Action
            && items[i].hasSecondaryAction();
        card->sync(m_theme, multiButton ? false : selected, rowOpacity);

        if (selected) {
            if (multiButton) {
                const nxui::Rect& hit = items[i].actionButtonFocus == 1
                    ? items[i].secondaryHit : items[i].primaryHit;
                if (hit.width > 1.f && hit.height > 1.f)
                    m_focusCursor.moveTo(hit.expanded(3.f), 12.f, 0.08f);
                else
                    m_focusCursor.moveTo(itemChildren[i]->rect().expanded(1.f), 18.f, 0.08f);
            } else {
                m_focusCursor.moveTo(itemChildren[i]->rect().expanded(1.f),
                                     items[i].type == ItemType::Section ? 14.f : 18.f,
                                     0.08f);
            }
        }
        y += h;
    }

    ren.pushClipRect(cr);
    m_tabContent->render(ren);
    ren.popClipRect();

    float totalH = contentTotalHeight() + 16.f;
    if (totalH > cr.height + 1.f) {
        float maxScroll = std::max(1.f, totalH - cr.height + 20.f);
        float trackH = std::max(42.f, cr.height * std::clamp(cr.height / totalH, 0.12f, 1.f));
        float trackX = cr.right() - 8.f;
        float trackY = cr.y + 16.f;
        float trackAreaH = std::max(1.f, cr.height - 32.f);
        float thumbY = trackY + (trackAreaH - trackH) * std::clamp(m_scrollY / maxScroll, 0.f, 1.f);
        nxui::Rect rail = {trackX, trackY, 3.f, trackAreaH};
        nxui::Rect thumb = {trackX - 0.5f, thumbY, 4.f, trackH};
        ren.drawRoundedRect(rail, m_theme->panelBorder.withAlpha(0.16f * opacity * chrome), 1.5f);
        ren.drawRoundedRect(thumb, m_theme->cursorNormal.withAlpha(0.46f * opacity * chrome), 2.f);
    }
}

void TabbedOverlayScreen::drawDropdown(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || !m_smallFont || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    float open = m_dropdownAnim.value();
    if (open <= 0.01f) return;

    auto& items = m_tabs[m_tabIndex].items;
    if (m_dropdownRawIdx < 0 || m_dropdownRawIdx >= (int)items.size()) return;
    auto& item = items[m_dropdownRawIdx];
    if (item.type != ItemType::Selector || item.options.empty()) return;

    nxui::Rect cr = contentRect(panel);
    DropdownGeom geom = layoutDropdown(cr);
    if (!geom.valid) return;

    int total = (int)item.options.size();
    int visible = geom.visible;
    float optH = geom.optH;
    float listH = geom.listH;
    float ctrlX = geom.ctrlX;
    float ctrlW = geom.ctrlW;
    float dy = geom.dy;
    m_dropdownVisible = visible;

    float visualStart = 0.f;
    if (total > visible)
        visualStart = std::clamp(m_dropdownVisualStart, 0.f, (float)(total - visible));
    int start = std::clamp((int)std::floor(visualStart), 0, std::max(0, total - visible));
    float rowOffset = (float)start - visualStart;

    float scale = 0.965f + 0.035f * open;
    float w = ctrlW * scale;
    float h = listH * scale;
    float dx = ctrlX + (ctrlW - w) * 0.5f;
    float fy = dy + (listH - h) * 0.5f + (1.f - open) * 8.f;

    nxui::Rect pop = { dx, fy, w, h };

    float a = opacity * open;
    nxui::Color bg = m_theme->mode == nxui::ThemeMode::Dark
        ? nxui::Color::lerp(m_theme->panelBase, nxui::Color(0.055f, 0.060f, 0.075f, 1.f), 0.36f).withAlpha(0.95f * a)
        : nxui::Color::lerp(m_theme->panelBase, nxui::Color(0.98f, 0.985f, 1.f, 1.f), 0.30f).withAlpha(0.96f * a);
    float radius = 15.f;

    nxui::Rect contact = pop;
    contact.y += 6.f;
    ren.drawRoundedRect(contact.expanded(3.f), nxui::Color::black().withAlpha(0.12f * a), radius + 3.f);
    ren.drawRoundedRect(pop.expanded(1.f), m_theme->panelHighlight.withAlpha(0.08f * a), radius + 1.f);
    ren.drawRoundedRect(pop, bg, radius);
    ren.drawRoundedRectOutline(pop,
                               m_theme->panelBorder.withAlpha(0.70f * a),
                               radius, 1.4f);
    ren.drawRoundedRectOutline(pop.shrunk(2.f),
                               m_theme->panelHighlight.withAlpha(0.10f * a),
                               std::max(0.f, radius - 2.f), 1.f);

    nxui::Rect listClip = pop.shrunk(6.f);
    ren.pushClipRect(listClip);

    for (int i = 0; i < visible + 1; ++i) {
        int idx = start + i;
        if (idx < 0 || idx >= total)
            continue;
        float rowReveal = std::clamp((open - i * 0.025f) / 0.25f, 0.f, 1.f);
        float ry = listClip.y + 3.f + (rowOffset + (float)i) * optH + (1.f - rowReveal) * 5.f;
        nxui::Rect rr = { listClip.x + 3.f, ry, listClip.width - 6.f, optH - 2.f };
        if (rr.bottom() < listClip.y || rr.y > listClip.bottom())
            continue;

        bool hovered = idx == m_dropdownHover;
        bool active = idx == item.intVal;
        float rowAlpha = a * rowReveal;
        if (active) {
            ren.drawRoundedRect(rr,
                                m_theme->panelHighlight.withAlpha(0.065f * rowAlpha),
                                10.f);
            ren.drawRoundedRectOutline(rr.shrunk(1.f),
                                       m_theme->panelHighlight.withAlpha(0.075f * rowAlpha),
                                       9.f,
                                       1.f);
        }
        if (hovered) {
            float pulse = 0.9f + 0.1f * (std::sin(m_uiTime * 5.2f) * 0.5f + 0.5f);
            nxui::Color hi = m_theme->cursorNormal.withAlpha(0.20f * pulse * rowAlpha);
            ren.drawRoundedRect(rr, hi, 10.f);
            ren.drawRoundedRectOutline(rr,
                                       m_theme->cursorNormal.withAlpha(0.42f * rowAlpha),
                                       10.f,
                                       1.2f);
            if (m_dropdownOpen)
                m_focusCursor.moveTo(rr.shrunk(0.5f), 10.f, 0.08f);
        }

        nxui::Color tc = active ? m_theme->textPrimary : m_theme->textSecondary;
        std::string displayText = item.options[idx];
        float maxWidth = std::max(0.f, rr.width - 16.f);
        if (!displayText.empty()) {
            nxui::Vec2 tsz = m_smallFont->measure(displayText);
            if (tsz.x > maxWidth) {
                std::string ellipsis = "...";
                while (!displayText.empty()) {
                    displayText.pop_back();
                    tsz = m_smallFont->measure(displayText + ellipsis);
                    if (tsz.x <= maxWidth || displayText.empty())
                        break;
                }
                displayText += ellipsis;
            }
        }
        nxui::Vec2 tsz = m_smallFont->measure(displayText);
        float tx = rr.x + 14.f;
        float ty = rr.y + (rr.height - tsz.y * 0.84f) * 0.5f;
        ren.drawText(displayText, {tx, ty}, m_smallFont, tc.withAlpha(rowAlpha), 0.84f);
    }

    ren.popClipRect();

    if (total > visible) {
        float railH = std::max(1.f, pop.height - 24.f);
        float thumbH = std::max(24.f, railH * ((float)visible / (float)total));
        float maxStart = (float)std::max(1, total - visible);
        float thumbY = pop.y + 12.f + (railH - thumbH) * (visualStart / maxStart);
        nxui::Rect rail = {pop.right() - 10.f, pop.y + 12.f, 3.f, railH};
        nxui::Rect thumb = {pop.right() - 10.5f, thumbY, 4.f, thumbH};
        ren.drawRoundedRect(rail, m_theme->panelBorder.withAlpha(0.22f * a), 1.5f);
        ren.drawRoundedRect(thumb, m_theme->cursorNormal.withAlpha(0.56f * a), 2.f);
    }
}

void TabbedOverlayScreen::drawTrackChangedToast(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_smallFont || !m_theme) return;
    float t = m_trackToastAnim.value();
    if (t <= 0.01f || m_toastText.empty()) return;

    std::string displayText = m_toastText;
    float maxTextWidth = 420.f - 40.f;
    nxui::Vec2 tsz = m_smallFont->measure(displayText);
    if (tsz.x * 0.78f > maxTextWidth) {
        while (!displayText.empty() && m_smallFont->measure(displayText + "...").x * 0.78f > maxTextWidth)
            displayText.pop_back();
        displayText += "...";
        tsz = m_smallFont->measure(displayText);
    }

    float textWidth = std::min(maxTextWidth, tsz.x * 0.78f);
    float scale = 0.96f + 0.04f * t;
    float w = std::clamp(textWidth + 40.f, 220.f, 420.f) * scale;
    float h = 40.f * scale;
    float x = panel.right() - 24.f - w;
    float y = panel.y + 18.f;
    nxui::Rect r = {x, y, w, h};

    nxui::Color bg = (m_theme->mode == nxui::ThemeMode::Dark)
        ? nxui::Color(0.10f, 0.14f, 0.20f, 0.92f * t * opacity)
        : nxui::Color(0.90f, 0.95f, 1.00f, 0.94f * t * opacity);
    nxui::Color bd = m_theme->cursorNormal.withAlpha(0.65f * t * opacity);

    ren.drawRoundedRect(r, bg, 10.f);
    ren.drawRoundedRectOutline(r, bd, 10.f, 1.5f);

    float tx = r.x + 12.f;
    float ty = r.y + (r.height - tsz.y * 0.72f) * 0.5f;
    ren.drawText(displayText, {tx, ty}, m_smallFont, m_theme->textPrimary.withAlpha(t * opacity), 0.78f);
}
