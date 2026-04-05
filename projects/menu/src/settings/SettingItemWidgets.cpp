#include "SettingItemWidgets.hpp"

#include <nxui/widgets/Label.hpp>
#include <nxui/widgets/GlassBox.hpp>
#include <algorithm>
#include <cmath>

namespace settings::widgets {

namespace {

class SettingRowBase : public nxui::Box {
public:
    SettingRowBase(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : nxui::Box(nxui::Axis::ROW)
        , m_item(item)
        , m_ctx(ctx) {
        setAlignItems(nxui::AlignItems::CENTER);
        setJustifyContent(nxui::JustifyContent::SPACE_BETWEEN);
        setPadding(0.f, 10.f, 0.f, 10.f);

        m_left = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
        m_left->setJustifyContent(nxui::JustifyContent::FLEX_START);
        m_left->setAlignItems(nxui::AlignItems::FLEX_START);
        m_left->setGap(2.f);
        m_left->setGrow(1.f);
        addChild(m_left);

        m_label = std::make_shared<nxui::Label>(item.label);
        m_label->setScale(0.82f);
        m_left->addChild(m_label);

        m_desc = std::make_shared<nxui::Label>(item.description);
        m_desc->setScale(0.64f);
        m_left->addChild(m_desc);

        m_right = std::make_shared<nxui::Box>(nxui::Axis::ROW);
        m_right->setAlignItems(nxui::AlignItems::CENTER);
        m_right->setJustifyContent(nxui::JustifyContent::FLEX_END);
        addChild(m_right);
    }

protected:
    void onUpdate(float dt) override {
        (void)dt;
        prepareLayout();
    }

    void onRender(nxui::Renderer& ren) override {
        prepareLayout();
        nxui::Box::onRender(ren);
    }

    virtual void syncRight() {}

    void prepareLayout() {
        syncCommon();
        syncRight();

        float rowW = rect().width;
        float rowH = rect().height;

        float rightW = std::clamp(m_right->rect().width, 120.f, std::max(120.f, rowW * 0.48f));
        m_right->setSize(rightW, rowH);
        m_left->setSize(std::max(0.f, rowW - rightW), rowH);

        layout();
        m_left->layout();
        m_right->layout();
    }

    void syncCommon() {
        nxui::Font* font = (m_ctx.font && *m_ctx.font) ? *m_ctx.font : nullptr;
        nxui::Font* smallFont = (m_ctx.smallFont && *m_ctx.smallFont) ? *m_ctx.smallFont : nullptr;
        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;

        if (font) m_label->setFont(font);
        if (smallFont) m_desc->setFont(smallFont);

        m_label->setText(m_item.label);
        m_desc->setText(m_item.description);
        bool showDesc = !m_item.description.empty() && m_item.type != SettingsScreen::ItemType::Section;
        m_desc->setVisible(showDesc);

        const bool isSection = (m_item.type == SettingsScreen::ItemType::Section);
        if (theme) {
            m_label->setTextColor(isSection ? theme->textSecondary : theme->textPrimary);
            m_desc->setTextColor(theme->textSecondary);
        }
        m_label->setScale(isSection ? 0.72f : 0.82f);

        m_label->sizeToFit();
        m_desc->sizeToFit();

        m_left->setJustifyContent(nxui::JustifyContent::CENTER);
    }

    SettingsScreen::SettingItem& m_item;
    SettingWidgetContext m_ctx;

    std::shared_ptr<nxui::Box> m_left;
    std::shared_ptr<nxui::Box> m_right;
    std::shared_ptr<nxui::Label> m_label;
    std::shared_ptr<nxui::Label> m_desc;
};

class SectionRowWidget final : public SettingRowBase {
public:
    using SettingRowBase::SettingRowBase;
protected:
    void syncRight() override {
        m_right->setVisible(false);
    }
};

class InfoRowWidget final : public SettingRowBase {
public:
    InfoRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_value = std::make_shared<nxui::Label>(item.infoText);
        m_value->setScale(0.76f);
        m_value->setHAlign(nxui::Label::HAlign::Right);
        m_value->setVAlign(nxui::Label::VAlign::Center);
        m_right->addChild(m_value);
    }
protected:
    void syncRight() override {
        nxui::Font* smallFont = (m_ctx.smallFont && *m_ctx.smallFont) ? *m_ctx.smallFont : nullptr;
        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;

        if (smallFont) m_value->setFont(smallFont);
        m_value->setText(m_item.infoText);
        if (theme) m_value->setTextColor(theme->textSecondary);
        float rightW = std::max(120.f, rect().width * 0.42f);
        m_right->setSize(rightW, rect().height);
        m_value->setSize(rightW, rect().height);
    }
private:
    std::shared_ptr<nxui::Label> m_value;
};

class ToggleRowWidget final : public SettingRowBase {
public:
    ToggleRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_track = std::make_shared<nxui::GlassBox>(nxui::Axis::ROW);
        m_track->setSize(50.f, 24.f);
        m_track->setPadding(3.f);
        m_track->setAlignItems(nxui::AlignItems::CENTER);
        m_track->setJustifyContent(nxui::JustifyContent::FLEX_START);
        m_track->setCornerRadius(12.f);
        m_track->setBorderWidth(0.f);

        m_knob = std::make_shared<nxui::GlassBox>();
        m_knob->setSize(18.f, 18.f);
        m_knob->setCornerRadius(9.f);
        m_knob->setBorderWidth(0.f);

        m_track->addChild(m_knob);
        m_right->addChild(m_track);
    }
protected:
    void syncRight() override {
        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;

        float t = std::clamp(m_item.anim01, 0.f, 1.f);
        if (theme) {
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
        float travel = 50.f - 6.f - 18.f;
        m_knob->setMarginLeft(travel * t);
        m_track->layout();
        m_right->setSize(std::max(120.f, rect().width * 0.42f), rect().height);
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
        m_pct->setScale(0.72f);
        m_pct->setHAlign(nxui::Label::HAlign::Right);
        m_pct->setVAlign(nxui::Label::VAlign::Center);
        m_pct->setMarginLeft(10.f);

        m_right->addChild(m_track);
        m_right->addChild(m_pct);
    }

protected:
    void syncRight() override {
        nxui::Font* smallFont = (m_ctx.smallFont && *m_ctx.smallFont) ? *m_ctx.smallFont : nullptr;
        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;

        float t = std::clamp(m_item.anim01, 0.f, 1.f);
        float rightW = std::max(170.f, rect().width * 0.42f);
        float pctW = 44.f;
        float trackW = std::clamp(rightW - pctW - 10.f, 110.f, 260.f);

        float fillW = (trackW - kKnobW) * t;

        m_track->setSize(trackW, 12.f);
        m_fill->setSize(fillW, kFillH);
        m_knob->setSize(kKnobW, kKnobW);

        if (theme) {
            m_track->setBaseColor(nxui::Color(0.3f, 0.3f, 0.35f, 0.5f * opacity()));
            m_fill->setBaseColor(theme->cursorNormal.withAlpha(0.9f * opacity()));
            m_knob->setBaseColor(nxui::Color(1.f, 1.f, 1.f, opacity()));
            m_pct->setTextColor(theme->textPrimary.withAlpha(opacity()));
        }

        int pct = (int)std::round(t * 100.f);
        m_pct->setText(std::to_string(pct) + "%");
        if (smallFont) m_pct->setFont(smallFont);
        m_pct->setSize(pctW, rect().height);

        m_track->layout();
        m_right->setSize(rightW, rect().height);
    }

private:
    static constexpr float kKnobW = 14.f;
    static constexpr float kFillH = 8.f;

    std::shared_ptr<nxui::GlassBox> m_track;
    std::shared_ptr<nxui::GlassBox> m_fill;
    std::shared_ptr<nxui::GlassBox> m_knob;
    std::shared_ptr<nxui::Label> m_pct;
};

class ProgressRowWidget final : public SettingRowBase {
public:
    ProgressRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
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

        m_pct = std::make_shared<nxui::Label>("0%");
        m_pct->setScale(0.72f);
        m_pct->setHAlign(nxui::Label::HAlign::Right);
        m_pct->setVAlign(nxui::Label::VAlign::Center);
        m_pct->setMarginLeft(10.f);

        m_right->addChild(m_track);
        m_right->addChild(m_pct);
    }
protected:
    void syncRight() override {
        nxui::Font* smallFont = (m_ctx.smallFont && *m_ctx.smallFont) ? *m_ctx.smallFont : nullptr;
        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;

        float t = std::clamp(m_item.floatVal, 0.f, 1.f);
        float rightW = std::max(170.f, rect().width * 0.42f);
        float pctW = 44.f;
        float trackW = std::clamp(rightW - pctW - 10.f, 110.f, 260.f);
        float fillW = trackW * t;

        m_track->setSize(trackW, 12.f);
        m_fill->setSize(fillW, 12.f);
        if (theme) {
            m_track->setBaseColor(nxui::Color(0.3f, 0.3f, 0.35f, 0.5f * opacity()));
            m_fill->setBaseColor(theme->cursorNormal.withAlpha(0.9f * opacity()));
            m_pct->setTextColor(theme->textPrimary.withAlpha(opacity()));
        }

        int pct = (int)std::round(t * 100.f);
        m_pct->setText(std::to_string(pct) + "%");
        if (smallFont) m_pct->setFont(smallFont);
        m_pct->setSize(pctW, rect().height);

        m_track->layout();
        m_right->setSize(rightW, rect().height);
    }

private:
    std::shared_ptr<nxui::GlassBox> m_track;
    std::shared_ptr<nxui::GlassBox> m_fill;
    std::shared_ptr<nxui::Label> m_pct;
};

class SelectorRowWidget final : public SettingRowBase {
public:
    SelectorRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_pill = std::make_shared<nxui::GlassBox>(nxui::Axis::ROW);
        m_pill->setPadding(8.f, 10.f, 8.f, 10.f);
        m_pill->setAlignItems(nxui::AlignItems::CENTER);
        m_pill->setJustifyContent(nxui::JustifyContent::SPACE_BETWEEN);
        m_pill->setCornerRadius(9.f);

        m_value = std::make_shared<nxui::Label>("");
        m_value->setScale(0.76f);
        m_value->setGrow(1.f);
        m_value->setHAlign(nxui::Label::HAlign::Left);
        m_value->setVAlign(nxui::Label::VAlign::Center);
        m_chev = std::make_shared<nxui::Label>("v");
        m_chev->setScale(0.76f);
        m_chev->setHAlign(nxui::Label::HAlign::Right);
        m_chev->setVAlign(nxui::Label::VAlign::Center);
        m_chev->setMarginLeft(12.f);

        m_pill->addChild(m_value);
        m_pill->addChild(m_chev);
        m_right->addChild(m_pill);
    }
protected:
    void syncRight() override {
        nxui::Font* smallFont = (m_ctx.smallFont && *m_ctx.smallFont) ? *m_ctx.smallFont : nullptr;
        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;

        int idx = std::clamp(m_item.intVal, 0, std::max(0, (int)m_item.options.size() - 1));
        std::string text = m_item.options.empty() ? std::string() : m_item.options[idx];
        m_value->setText(text);
        if (smallFont) {
            m_value->setFont(smallFont);
            m_chev->setFont(smallFont);
        }
        if (theme) {
            m_pill->setBaseColor(theme->panelBase.withAlpha(0.42f * opacity()));
            m_pill->setBorderColor(theme->panelBorder.withAlpha(0.5f * opacity()));
            m_value->setTextColor(theme->textPrimary.withAlpha(opacity()));
            m_chev->setTextColor(theme->textPrimary.withAlpha(opacity()));
        }
        float w = std::max(190.f, rect().width * 0.45f);
        float h = std::max(30.f, rect().height - 16.f);
        m_pill->setSize(w, h);
        m_value->setSize(std::max(0.f, w - 34.f), h);
        m_chev->setSize(22.f, h);
        m_pill->layout();
        m_right->setSize(std::max(220.f, rect().width * 0.50f), rect().height);
    }
private:
    std::shared_ptr<nxui::GlassBox> m_pill;
    std::shared_ptr<nxui::Label> m_value;
    std::shared_ptr<nxui::Label> m_chev;
};

class ActionRowWidget final : public SettingRowBase {
public:
    ActionRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_btn = std::make_shared<nxui::GlassBox>(nxui::Axis::ROW);
        m_btn->setPadding(8.f, 12.f, 8.f, 12.f);
        m_btn->setAlignItems(nxui::AlignItems::CENTER);
        m_btn->setJustifyContent(nxui::JustifyContent::CENTER);
        m_btn->setCornerRadius(9.f);
        m_btn->setBorderWidth(1.f);

        m_btnLabel = std::make_shared<nxui::Label>(item.label);
        m_btnLabel->setScale(0.76f);
        m_btnLabel->setHAlign(nxui::Label::HAlign::Center);
        m_btnLabel->setVAlign(nxui::Label::VAlign::Center);
        m_btnLabel->setGrow(1.f);
        m_btn->addChild(m_btnLabel);
        m_right->addChild(m_btn);
    }
protected:
    void syncRight() override {
        nxui::Font* smallFont = (m_ctx.smallFont && *m_ctx.smallFont) ? *m_ctx.smallFont : nullptr;
        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;

        if (smallFont) m_btnLabel->setFont(smallFont);
        m_btnLabel->setText(m_item.label);

        float pulse = 0.18f + 0.16f * m_item.anim01;
        if (theme) {
            m_btn->setBaseColor(theme->cursorNormal.withAlpha(pulse * opacity()));
            m_btn->setBorderColor(theme->panelBorder.withAlpha(0.65f * opacity()));
            m_btnLabel->setTextColor(theme->textPrimary.withAlpha(opacity()));
        }
        float btnW = std::max(140.f, rect().width * 0.30f);
        float btnH = std::max(30.f, rect().height - 16.f);
        m_btn->setSize(btnW, btnH);
        m_btnLabel->setSize(std::max(0.f, btnW - 24.f), std::max(0.f, btnH - 8.f));
        m_btn->layout();
        m_right->setSize(std::max(160.f, rect().width * 0.42f), rect().height);
    }
private:
    std::shared_ptr<nxui::GlassBox> m_btn;
    std::shared_ptr<nxui::Label> m_btnLabel;
};

class ColorPickerRowWidget final : public SettingRowBase {
public:
    ColorPickerRowWidget(SettingsScreen::SettingItem& item, const SettingWidgetContext& ctx)
        : SettingRowBase(item, ctx) {
        m_swatch = std::make_shared<nxui::GlassBox>();
        m_swatch->setCornerRadius(8.f);
        m_swatch->setBorderWidth(1.5f);

        m_valueLbl = std::make_shared<nxui::Label>("");
        m_valueLbl->setScale(0.66f);
        m_valueLbl->setHAlign(nxui::Label::HAlign::Right);
        m_valueLbl->setVAlign(nxui::Label::VAlign::Center);
        m_valueLbl->setMarginLeft(8.f);

        m_right->addChild(m_swatch);
        m_right->addChild(m_valueLbl);
    }
protected:
    void syncRight() override {
        nxui::Font* smallFont = (m_ctx.smallFont && *m_ctx.smallFont) ? *m_ctx.smallFont : nullptr;
        const nxui::Theme* theme = (m_ctx.theme && *m_ctx.theme) ? *m_ctx.theme : nullptr;

        nxui::Color col = nxui::Color::fromHSL(m_item.colorH, m_item.colorS, m_item.colorL, 1.f);
        m_swatch->setBaseColor(col.withAlpha(opacity()));
        if (theme) {
            m_swatch->setBorderColor(theme->panelBorder.withAlpha(0.6f * opacity()));
            m_valueLbl->setTextColor(theme->textSecondary.withAlpha(opacity()));
        }
        float swatchW = 36.f;
        float swatchH = std::max(24.f, rect().height - 20.f);
        m_swatch->setSize(swatchW, swatchH);

        if (smallFont) m_valueLbl->setFont(smallFont);
        m_valueLbl->setText("\xE2\x96\xB6");
        m_valueLbl->setSize(20.f, rect().height);

        m_right->setSize(std::max(80.f, rect().width * 0.20f), rect().height);
    }
private:
    std::shared_ptr<nxui::GlassBox> m_swatch;
    std::shared_ptr<nxui::Label> m_valueLbl;
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
        case IT::ColorPicker: return std::make_shared<ColorPickerRowWidget>(item, ctx);
        default:              return std::make_shared<InfoRowWidget>(item, ctx);
    }
}

}
