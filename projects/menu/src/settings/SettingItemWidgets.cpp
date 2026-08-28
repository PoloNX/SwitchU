#include "SettingItemWidgets.hpp"
#include "widgets/ActionButton.hpp"

#include <nxui/core/Renderer.hpp>
#include <nxui/widgets/Label.hpp>
#include <nxui/widgets/GlassBox.hpp>
#include <nxui/widgets/GlassWidget.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace settings::widgets {

namespace {

constexpr size_t kFitCacheLimit = 512;

std::unordered_map<std::string, std::string> g_fitCache;

std::string fitCacheKey(nxui::Font* font, const std::string& text, float scale, float maxWidth) {
    return std::to_string((std::uintptr_t)font)
         + "\n" + std::to_string(font ? font->revision() : 0)
         + "\n" + std::to_string((int)std::lround(maxWidth * 4.f))
         + "\n" + std::to_string((int)std::lround(scale * 1000.f))
         + "\n" + text;
}

void popCodepoint(std::string& s) {
    while (!s.empty()) {
        const unsigned char c = (unsigned char)s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80)
            break;
    }
}

std::string fitTextToWidthUncached(nxui::Font* font, const std::string& text,
                                   float scale, float maxWidth) {
    if (font->measure(text).x * scale <= maxWidth)
        return text;

    constexpr const char* kEllipsis = "...";
    std::string out = text;
    while (!out.empty()) {
        popCodepoint(out); // remove one codepoint at a time
        std::string candidate = out + kEllipsis;
        if (font->measure(candidate).x * scale <= maxWidth)
            return candidate;
    }
    return kEllipsis;
}

// Returns a fitted string, using a cache to avoid repeated measurements of the same text.
std::string fitTextToWidth(nxui::Font* font, const std::string& text, float scale, float maxWidth) {
    if (!font || text.empty() || maxWidth <= 4.f)
        return {};

    std::string key = fitCacheKey(font, text, scale, maxWidth);
    auto cached = g_fitCache.find(key);
    if (cached != g_fitCache.end())
        return cached->second;

    std::string result = fitTextToWidthUncached(font, text, scale, maxWidth);
    if (g_fitCache.size() >= kFitCacheLimit)
        g_fitCache.clear();
    g_fitCache.emplace(std::move(key), result);
    return result;
}

class LoadingSpinnerWidget final : public nxui::Box {
public:
    explicit LoadingSpinnerWidget(const SettingWidgetContext& ctx)
        : nxui::Box(nxui::Axis::ROW)
        , m_ctx(ctx) {
    }

protected:
    void onUpdate(float dt) override {
        m_angle += dt * 5.4f;
        constexpr float kTwoPi = 6.28318530718f;
        if (m_angle >= kTwoPi)
            m_angle = std::fmod(m_angle, kTwoPi);
    }

    void onRender(nxui::Renderer& ren) override {
        if (!isVisible() || opacity() <= 0.01f)
            return;

        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;
        nxui::Color accent = theme ? theme->cursorNormal : nxui::Color::white();
        nxui::Color trail = theme ? theme->textSecondary : nxui::Color(0.8f, 0.8f, 0.8f, 1.f);

        nxui::Rect r = rect();
        nxui::Vec2 center = {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
        float outerRadius = std::max(6.f, std::min(r.width, r.height) * 0.5f - 1.5f);
        float innerRadius = outerRadius * 0.42f;
        constexpr int kSpokes = 10;
        constexpr float kStep = 6.28318530718f / (float)kSpokes;

        for (int i = 0; i < kSpokes; ++i) {
            float angle = m_angle + kStep * (float)i;
            float weight = 1.f - ((float)i / (float)kSpokes);
            nxui::Color spoke(
                trail.r + (accent.r - trail.r) * weight,
                trail.g + (accent.g - trail.g) * weight,
                trail.b + (accent.b - trail.b) * weight,
                (0.16f + 0.72f * weight) * opacity());

            nxui::Vec2 inner = {
                center.x + std::cos(angle) * innerRadius,
                center.y + std::sin(angle) * innerRadius,
            };
            nxui::Vec2 outer = {
                center.x + std::cos(angle) * outerRadius,
                center.y + std::sin(angle) * outerRadius,
            };

            ren.drawLine(inner, outer, spoke, 2.2f - 0.9f * ((float)i / (float)kSpokes));
        }

        ren.drawCircle(center, std::max(1.8f, innerRadius * 0.16f), accent.withAlpha(0.30f * opacity()), 18);
    }

private:
    SettingWidgetContext m_ctx;
    float m_angle = 0.f;
};

class SettingRowBase : public nxui::Box {
public:
    SettingRowBase(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : nxui::Box(nxui::Axis::ROW)
        , m_item(item)
        , m_ctx(ctx) {
        m_left = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
        addChild(m_left);

        m_label = std::make_shared<nxui::Label>(item.label);
        m_label->setMultiline(item.wrapLabel);
        m_label->setScale(0.94f);
        m_label->setHAlign(nxui::Label::HAlign::Left);
        m_label->setVAlign(nxui::Label::VAlign::Top);
        m_left->addChild(m_label);

        m_desc = std::make_shared<nxui::Label>(item.description);
        m_desc->setScale(0.74f);
        m_desc->setHAlign(nxui::Label::HAlign::Left);
        m_desc->setVAlign(nxui::Label::VAlign::Top);
        m_left->addChild(m_desc);

        m_right = std::make_shared<nxui::Box>(nxui::Axis::ROW);
        addChild(m_right);
    }

protected:
    void onUpdate(float dt) override {
        (void)dt;
    }

    void onRender(nxui::Renderer& ren) override {
        prepareLayout();
        nxui::Box::onRender(ren);
    }

    virtual float preferredRightWidth(float rowWidth) const {
        (void)rowWidth;
        return 0.f;
    }

    virtual void syncRight(const nxui::Rect& rightRect) {
        (void)rightRect;
    }

    nxui::Font* font() const {
        return (m_ctx.font && *m_ctx.font) ? *m_ctx.font : nullptr;
    }

    nxui::Font* smallFont() const {
        return (m_ctx.smallFont && *m_ctx.smallFont) ? *m_ctx.smallFont : nullptr;
    }

    const nxui::Theme* theme() const {
        return (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;
    }

    void prepareLayout() {
        syncCommon();

        nxui::Rect row = rect();
        float innerX = row.x + kHorizontalInset;
        float innerW = std::max(0.f, row.width - kHorizontalInset * 2.f);
        float rightW = m_right->isVisible() ? std::clamp(preferredRightWidth(row.width), 0.f, innerW) : 0.f;
        float centerGap = rightW > 0.f ? kColumnGap : 0.f;
        float leftW = std::max(0.f, innerW - rightW - centerGap);

        nxui::Rect leftRect = {innerX, row.y, leftW, row.height};
        nxui::Rect rightRect = {innerX + leftW + centerGap, row.y, rightW, row.height};

        m_left->setRect(leftRect);
        m_right->setRect(rightRect);

        if (m_cachedShowDesc && smallFont()) {
            const std::string fitted = fitTextToWidth(
                smallFont(), m_item.description, 0.74f, leftRect.width);
            if (fitted != m_displayedDescription) {
                m_displayedDescription = fitted;
                m_desc->setText(m_displayedDescription);
                m_descMeasure = m_desc->measureText();
            }
        }

        if (m_item.wrapLabel)
            m_labelMeasure = m_label->measureWrappedText(std::max(1.f, leftRect.width));

        float totalTextH = m_labelMeasure.y;
        if (m_cachedShowDesc)
            totalTextH += kLabelGap + m_descMeasure.y;
        float textTop = leftRect.y + std::max(0.f, (leftRect.height - totalTextH) * 0.5f);

        m_label->setRect({leftRect.x, textTop, leftRect.width, m_labelMeasure.y});
        if (m_cachedShowDesc) {
            m_desc->setRect({leftRect.x, textTop + m_labelMeasure.y + kLabelGap,
                             leftRect.width, m_descMeasure.y});
        } else {
            m_desc->setRect({leftRect.x, textTop, leftRect.width, 0.f});
        }

        syncRight(rightRect);
    }

    void syncCommon() {
        nxui::Font* rowFont = font();
        nxui::Font* rowSmallFont = smallFont();
        const nxui::Theme* rowTheme = theme();
        const bool isSection = (m_item.type == SettingsScreen::ItemType::Section);
        const bool showDesc = !m_item.description.empty() && !isSection;
        const float labelScale = isSection ? 0.82f : 0.94f;

        if (rowFont != m_cachedFont) {
            m_cachedFont = rowFont;
            if (rowFont)
                m_label->setFont(rowFont);
            m_labelMeasure = m_label->measureText();
        }
        if (rowSmallFont != m_cachedSmallFont) {
            m_cachedSmallFont = rowSmallFont;
            if (rowSmallFont)
                m_desc->setFont(rowSmallFont);
            m_descMeasure = m_desc->measureText();
        }

        if (m_cachedLabelText != m_item.label) {
            m_cachedLabelText = m_item.label;
            m_label->setText(m_cachedLabelText);
            m_labelMeasure = m_label->measureText();
        }
        if (m_cachedDescText != m_item.description) {
            m_cachedDescText = m_item.description;
            m_displayedDescription.clear();
            m_desc->setText(m_cachedDescText);
            m_descMeasure = m_desc->measureText();
        }
        if (m_cachedShowDesc != showDesc) {
            m_cachedShowDesc = showDesc;
            m_desc->setVisible(showDesc);
        }
        if (std::abs(m_cachedLabelScale - labelScale) > 0.001f) {
            m_cachedLabelScale = labelScale;
            m_label->setScale(labelScale);
            m_labelMeasure = m_label->measureText();
        }

        if (rowTheme != m_cachedTheme || isSection != m_cachedIsSection) {
            m_cachedTheme = rowTheme;
            m_cachedIsSection = isSection;
            if (rowTheme) {
                m_label->setTextColor(isSection ? rowTheme->textSecondary : rowTheme->textPrimary);
                m_desc->setTextColor(rowTheme->textSecondary);
            }
        }

        m_label->setOpacity(opacity());
        m_desc->setOpacity(opacity());
    }

    SettingsScreen::SettingItem& m_item;
    SettingWidgetContext m_ctx;

    std::shared_ptr<nxui::Box> m_left;
    std::shared_ptr<nxui::Box> m_right;
    std::shared_ptr<nxui::Label> m_label;
    std::shared_ptr<nxui::Label> m_desc;

private:
    static constexpr float kHorizontalInset = 10.f;
    static constexpr float kColumnGap = 12.f;
    static constexpr float kLabelGap = 2.f;

    nxui::Font* m_cachedFont = nullptr;
    nxui::Font* m_cachedSmallFont = nullptr;
    const nxui::Theme* m_cachedTheme = nullptr;
    std::string m_cachedLabelText;
    std::string m_cachedDescText;
    std::string m_displayedDescription;
    float m_cachedLabelScale = -1.f;
    bool m_cachedShowDesc = false;
    bool m_cachedIsSection = false;
    nxui::Vec2 m_labelMeasure = {0.f, 0.f};
    nxui::Vec2 m_descMeasure = {0.f, 0.f};
};

class SectionRowWidget final : public SettingRowBase {
public:
    using SettingRowBase::SettingRowBase;
protected:
    void syncRight(const nxui::Rect&) override {
        m_right->setVisible(false);
    }
};

class InfoRowWidget final : public SettingRowBase {
public:
    InfoRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_value = std::make_shared<nxui::Label>(item.infoText);
        m_value->setScale(0.84f);
        m_value->setHAlign(nxui::Label::HAlign::Right);
        m_value->setVAlign(nxui::Label::VAlign::Center);
        m_right->addChild(m_value);
    }
protected:
    float preferredRightWidth(float rowWidth) const override {
        if (m_item.infoText.empty()) return 0.f;
        return std::max(120.f, rowWidth * 0.42f);
    }

    void syncRight(const nxui::Rect& rightRect) override {
        nxui::Font* rowSmallFont = smallFont();
        const nxui::Theme* rowTheme = theme();

        if (rowSmallFont != m_cachedValueFont) {
            m_cachedValueFont = rowSmallFont;
            if (rowSmallFont)
                m_value->setFont(rowSmallFont);
        }
        if (rowTheme != m_cachedValueTheme && rowTheme) {
            m_cachedValueTheme = rowTheme;
            m_value->setTextColor(rowTheme->textSecondary);
        }
        std::string fitted = fitTextToWidth(rowSmallFont, m_item.infoText, 0.84f, rightRect.width);
        if (m_cachedValueText != fitted) {
            m_cachedValueText = fitted;
            m_value->setText(m_cachedValueText);
        }
        m_value->setOpacity(opacity());
        m_value->setRect(rightRect);
    }
private:
    std::shared_ptr<nxui::Label> m_value;
    nxui::Font* m_cachedValueFont = nullptr;
    const nxui::Theme* m_cachedValueTheme = nullptr;
    std::string m_cachedValueText;
};

class ToggleRowWidget final : public SettingRowBase {
public:
    ToggleRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_track = std::make_shared<nxui::GlassBox>(nxui::Axis::ROW);
        m_track->setSize(64.f, 32.f);
        m_track->setPadding(4.f);
        m_track->setAlignItems(nxui::AlignItems::CENTER);
        m_track->setJustifyContent(nxui::JustifyContent::FLEX_START);
        m_track->setCornerRadius(16.f);
        m_track->setBorderWidth(0.f);

        m_knob = std::make_shared<nxui::GlassBox>();
        m_knob->setSize(24.f, 24.f);
        m_knob->setCornerRadius(12.f);
        m_knob->setBorderWidth(0.f);

        m_track->addChild(m_knob);
        m_right->addChild(m_track);
    }
protected:
    float preferredRightWidth(float rowWidth) const override {
        return std::max(120.f, rowWidth * 0.42f);
    }

    void syncRight(const nxui::Rect& rightRect) override {
        const nxui::Theme* rowTheme = theme();

        float t = std::clamp(m_item.anim01, 0.f, 1.f);
        if (rowTheme) {
            nxui::Color offC = nxui::Color(0.45f, 0.45f, 0.5f, 1.f);
            nxui::Color onC = nxui::Color(0.2f, 0.8f, 0.4f, 1.f);
            nxui::Color bg = nxui::Color(
                offC.r + (onC.r - offC.r) * t,
                offC.g + (onC.g - offC.g) * t,
                offC.b + (onC.b - offC.b) * t,
                1.f
            );
            m_track->setBaseColor(bg.withAlpha(opacity()));
            m_knob->setBaseColor(nxui::Color(1.f, 1.f, 1.f, opacity()));
        }
        float travel = 64.f - 8.f - 24.f;
        nxui::Rect trackRect = {
            rightRect.right() - 64.f,
            rightRect.y + (rightRect.height - 32.f) * 0.5f,
            64.f,
            32.f
        };
        m_track->setRect(trackRect);
        m_knob->setRect({
            trackRect.x + 4.f + travel * t,
            trackRect.y + 4.f,
            24.f,
            24.f
        });
    }
private:
    std::shared_ptr<nxui::GlassBox> m_track;
    std::shared_ptr<nxui::GlassBox> m_knob;
};

class SliderRowWidget final : public SettingRowBase {
public:
    SliderRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_track = std::make_shared<nxui::GlassBox>(nxui::Axis::ROW);
        m_track->setAlignItems(nxui::AlignItems::CENTER);
        m_track->setJustifyContent(nxui::JustifyContent::FLEX_START);
        m_track->setGap(0.f);
        m_track->setPadding(0.f);
        m_track->setCornerRadius(6.f);
        m_track->setBorderWidth(0.f);

        m_fill = std::make_shared<nxui::GlassBox>();
        m_fill->setCornerRadius(4.f);
        m_fill->setBorderWidth(0.f);
        m_track->addChild(m_fill);

        m_knob = std::make_shared<nxui::GlassBox>();
        m_knob->setSize(kKnobW, kKnobW);
        m_knob->setCornerRadius(kKnobW * 0.5f);
        m_knob->setBorderWidth(0.f);
        m_track->addChild(m_knob);

        m_pct = std::make_shared<nxui::Label>("0%");
        m_pct->setScale(0.84f);
        m_pct->setHAlign(nxui::Label::HAlign::Right);
        m_pct->setVAlign(nxui::Label::VAlign::Center);
        m_pct->setMarginLeft(10.f);

        m_right->addChild(m_track);
        m_right->addChild(m_pct);
    }

protected:
    float preferredRightWidth(float rowWidth) const override {
        return std::max(170.f, rowWidth * 0.42f);
    }

    void syncRight(const nxui::Rect& rightRect) override {
        nxui::Font* rowSmallFont = smallFont();
        const nxui::Theme* rowTheme = theme();

        float t = std::clamp(m_item.anim01, 0.f, 1.f);
        float pctW = 44.f;
        float trackW = std::clamp(rightRect.width - pctW - 10.f, 110.f, 260.f);

        float fillW = (trackW - kKnobW) * t;

        nxui::Rect pctRect = {
            rightRect.right() - pctW,
            rightRect.y,
            pctW,
            rightRect.height
        };
        nxui::Rect trackRect = {
            pctRect.x - 10.f - trackW,
            rightRect.y + (rightRect.height - 14.f) * 0.5f,
            trackW,
            14.f
        };

        if (rowTheme) {
            m_track->setBaseColor(nxui::Color(0.3f, 0.3f, 0.35f, 0.5f * opacity()));
            m_fill->setBaseColor(rowTheme->cursorNormal.withAlpha(0.9f * opacity()));
            m_knob->setBaseColor(nxui::Color(1.f, 1.f, 1.f, opacity()));
            m_pct->setTextColor(rowTheme->textPrimary);
        }

        int pct = (int)std::round(t * 100.f);
        std::string pctText = m_item.infoText.empty() ? (std::to_string(pct) + "%") : m_item.infoText;
        if (pctText != m_cachedPctText) {
            m_cachedPctText = pctText;
            m_pct->setText(m_cachedPctText);
        }
        if (rowSmallFont != m_cachedPctFont) {
            m_cachedPctFont = rowSmallFont;
            if (rowSmallFont)
                m_pct->setFont(rowSmallFont);
        }

        m_track->setRect(trackRect);
        m_fill->setRect({trackRect.x, trackRect.y + (trackRect.height - kFillH) * 0.5f, fillW, kFillH});
        m_knob->setRect({trackRect.x + fillW, trackRect.y + (trackRect.height - kKnobW) * 0.5f, kKnobW, kKnobW});
        m_pct->setOpacity(opacity());
        m_pct->setRect(pctRect);
    }

private:
    static constexpr float kKnobW = 18.f;
    static constexpr float kFillH = 10.f;

    std::shared_ptr<nxui::GlassBox> m_track;
    std::shared_ptr<nxui::GlassBox> m_fill;
    std::shared_ptr<nxui::GlassBox> m_knob;
    std::shared_ptr<nxui::Label> m_pct;
    nxui::Font* m_cachedPctFont = nullptr;
    std::string m_cachedPctText;
};

class ProgressRowWidget final : public SettingRowBase {
public:
    ProgressRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_spinner = std::make_shared<LoadingSpinnerWidget>(ctx);
        m_spinner->setSize(26.f, 26.f);

        m_track = std::make_shared<nxui::GlassBox>(nxui::Axis::ROW);
        m_track->setAlignItems(nxui::AlignItems::CENTER);
        m_track->setJustifyContent(nxui::JustifyContent::FLEX_START);
        m_track->setGap(0.f);
        m_track->setPadding(0.f);
        m_track->setCornerRadius(6.f);
        m_track->setBorderWidth(0.f);

        m_fill = std::make_shared<nxui::GlassBox>();
        m_fill->setCornerRadius(4.f);
        m_fill->setBorderWidth(0.f);
        m_track->addChild(m_fill);

        m_pct = std::make_shared<nxui::Label>("0%");
        m_pct->setScale(0.78f);
        m_pct->setHAlign(nxui::Label::HAlign::Right);
        m_pct->setVAlign(nxui::Label::VAlign::Center);
        m_pct->setMarginLeft(10.f);

        m_right->addChild(m_spinner);
        m_right->addChild(m_track);
        m_right->addChild(m_pct);
    }
protected:
    float preferredRightWidth(float rowWidth) const override {
        return std::max(170.f, rowWidth * 0.42f);
    }

    void syncRight(const nxui::Rect& rightRect) override {
        nxui::Font* rowSmallFont = smallFont();
        const nxui::Theme* rowTheme = theme();

        bool indeterminate = m_item.floatVal < 0.f;
        m_spinner->setVisible(indeterminate);
        m_track->setVisible(!indeterminate);
        m_pct->setVisible(!indeterminate);

        if (indeterminate) {
            m_spinner->setRect({
                rightRect.right() - 26.f,
                rightRect.y + (rightRect.height - 26.f) * 0.5f,
                26.f,
                26.f
            });
            return;
        }

        float t = std::clamp(m_item.floatVal, 0.f, 1.f);
        float pctW = 44.f;
        float trackW = std::clamp(rightRect.width - pctW - 10.f, 110.f, 260.f);
        float fillW = trackW * t;

        nxui::Rect pctRect = {
            rightRect.right() - pctW,
            rightRect.y,
            pctW,
            rightRect.height
        };
        nxui::Rect trackRect = {
            pctRect.x - 10.f - trackW,
            rightRect.y + (rightRect.height - 12.f) * 0.5f,
            trackW,
            12.f
        };

        if (rowTheme) {
            m_track->setBaseColor(nxui::Color(0.3f, 0.3f, 0.35f, 0.5f * opacity()));
            m_fill->setBaseColor(rowTheme->cursorNormal.withAlpha(0.9f * opacity()));
            m_pct->setTextColor(rowTheme->textPrimary);
        }

        int pct = (int)std::round(t * 100.f);
        std::string pctText = std::to_string(pct) + "%";
        if (pctText != m_cachedPctText) {
            m_cachedPctText = pctText;
            m_pct->setText(m_cachedPctText);
        }
        if (rowSmallFont != m_cachedPctFont) {
            m_cachedPctFont = rowSmallFont;
            if (rowSmallFont)
                m_pct->setFont(rowSmallFont);
        }

        m_track->setRect(trackRect);
        m_fill->setRect({trackRect.x, trackRect.y, fillW, trackRect.height});
        m_pct->setOpacity(opacity());
        m_pct->setRect(pctRect);
    }

private:
    std::shared_ptr<LoadingSpinnerWidget> m_spinner;
    std::shared_ptr<nxui::GlassBox> m_track;
    std::shared_ptr<nxui::GlassBox> m_fill;
    std::shared_ptr<nxui::Label> m_pct;
    nxui::Font* m_cachedPctFont = nullptr;
    std::string m_cachedPctText;
};

class SelectorRowWidget final : public SettingRowBase {
public:
    SelectorRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_pill = std::make_shared<nxui::GlassBox>(nxui::Axis::ROW);
        m_pill->setPadding(8.f, 12.f, 8.f, 12.f);
        m_pill->setAlignItems(nxui::AlignItems::CENTER);
        m_pill->setJustifyContent(nxui::JustifyContent::SPACE_BETWEEN);
        m_pill->setCornerRadius(11.f);

        m_value = std::make_shared<nxui::Label>("");
        m_value->setScale(0.82f);
        m_value->setGrow(1.f);
        m_value->setHAlign(nxui::Label::HAlign::Left);
        m_value->setVAlign(nxui::Label::VAlign::Center);

        m_pill->addChild(m_value);
        m_right->addChild(m_pill);
    }
protected:
    float preferredRightWidth(float rowWidth) const override {
        return std::max(170.f, rowWidth * 0.38f);
    }

    void syncRight(const nxui::Rect& rightRect) override {
        nxui::Font* rowSmallFont = smallFont();
        const nxui::Theme* rowTheme = theme();

        int idx = std::clamp(m_item.intVal, 0, std::max(0, (int)m_item.options.size() - 1));
        float w = std::max(170.f, rightRect.width);
        float h = std::max(36.f, rect().height - 22.f);
        nxui::Rect pillRect = {
            rightRect.right() - w,
            rightRect.y + (rightRect.height - h) * 0.5f,
            w,
            h
        };

        std::string text = m_item.options.empty() ? std::string() : m_item.options[idx];
        text = fitTextToWidth(rowSmallFont, text, 0.82f, std::max(0.f, pillRect.width - 24.f));
        if (text != m_cachedValueText) {
            m_cachedValueText = text;
            m_value->setText(m_cachedValueText);
        }
        if (rowSmallFont != m_cachedFont) {
            m_cachedFont = rowSmallFont;
            if (rowSmallFont)
                m_value->setFont(rowSmallFont);
        }
        if (rowTheme) {
            m_pill->setBaseColor(rowTheme->panelBase.withAlpha(0.42f * opacity()));
            m_pill->setBorderColor(rowTheme->panelBorder.withAlpha(0.5f * opacity()));
            m_value->setTextColor(rowTheme->textPrimary);
        }
        m_pill->setRect(pillRect);
        m_value->setOpacity(opacity());
        m_value->setRect({pillRect.x + 12.f, pillRect.y, std::max(0.f, pillRect.width - 24.f), pillRect.height});
    }
private:
    std::shared_ptr<nxui::GlassBox> m_pill;
    std::shared_ptr<nxui::Label> m_value;
    nxui::Font* m_cachedFont = nullptr;
    std::string m_cachedValueText;
};

class ActionRowWidget final : public SettingRowBase {
public:
    ActionRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_btn = std::make_shared<ActionButton>();
        m_btn->setCornerRadius(9.f);

        m_btnLabel = std::make_shared<nxui::Label>(item.effectiveButtonLabel());
        m_btnLabel->setScale(0.84f);
        m_btnLabel->setHAlign(nxui::Label::HAlign::Center);
        m_btnLabel->setVAlign(nxui::Label::VAlign::Center);
        m_btnLabel->setGrow(1.f);
        m_btn->addChild(m_btnLabel);
        m_right->addChild(m_btn);
    }
protected:
    float preferredRightWidth(float rowWidth) const override {
        return std::max(160.f, rowWidth * 0.42f);
    }

    void syncRight(const nxui::Rect& rightRect) override {
        nxui::Font* rowSmallFont = smallFont();
        const nxui::Theme* rowTheme = theme();

        if (rowSmallFont != m_cachedLabelFont) {
            m_cachedLabelFont = rowSmallFont;
            if (rowSmallFont)
                m_btnLabel->setFont(rowSmallFont);
        }
        const std::string& buttonText = m_item.effectiveButtonLabel();
        if (m_cachedButtonText != buttonText) {
            m_cachedButtonText = buttonText;
            m_btnLabel->setText(m_cachedButtonText);
        }

        m_btn->setTheme(rowTheme);
        m_btn->setVisualState(opacity(), m_item.anim01, 1.f);
        if (rowTheme) {
            m_btnLabel->setTextColor(rowTheme->textPrimary);
        }
        float btnW = std::max(140.f, std::min(rightRect.width, rect().width * 0.30f));
        float btnH = std::max(30.f, rect().height - 16.f);
        nxui::Rect buttonRect = {
            rightRect.right() - btnW,
            rightRect.y + (rightRect.height - btnH) * 0.5f,
            btnW,
            btnH
        };
        m_btn->setRect(buttonRect);
        m_btnLabel->setOpacity(opacity());
        m_btnLabel->setRect({buttonRect.x + 12.f, buttonRect.y + 4.f,
                             std::max(0.f, buttonRect.width - 24.f),
                             std::max(0.f, buttonRect.height - 8.f)});
    }
private:
    std::shared_ptr<ActionButton> m_btn;
    std::shared_ptr<nxui::Label> m_btnLabel;
    nxui::Font* m_cachedLabelFont = nullptr;
    std::string m_cachedButtonText;
};

}

std::shared_ptr<nxui::Box> createSettingItemWidget(SettingsScreen::SettingItem& item,
                                                    const SettingWidgetContext& ctx) {
    using IT = SettingsScreen::ItemType;
    switch (item.type) {
        case IT::Section:     return std::make_shared<SectionRowWidget>(item, ctx);
        case IT::Info:        return std::make_shared<InfoRowWidget>(item, ctx);
        case IT::Toggle:      return std::make_shared<ToggleRowWidget>(item, ctx);
        case IT::Slider:      return std::make_shared<SliderRowWidget>(item, ctx);
        case IT::Progress:    return std::make_shared<ProgressRowWidget>(item, ctx);
        case IT::Selector:    return std::make_shared<SelectorRowWidget>(item, ctx);
        case IT::Action:      return std::make_shared<ActionRowWidget>(item, ctx);
        default:              return std::make_shared<InfoRowWidget>(item, ctx);
    }
}

}
