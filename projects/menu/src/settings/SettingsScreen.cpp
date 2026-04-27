#include "SettingsScreen.hpp"
#include "SettingsGlassTuning.hpp"
#include "SettingItemWidgets.hpp"
#include "tabs/TabBuilders.hpp"
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
static constexpr float kSettingsBlurRadius = 6.0f;
static constexpr int kSettingsBlurIter = 1;

namespace {

static constexpr float kTabRailInset = 14.f;
static constexpr float kTabCardGap = 10.f;
static constexpr float kContentCardInsetX = 18.f;
static constexpr float kContentCardInsetY = 8.f;
static constexpr int kSettingsBackdropCacheTarget = 2;

class SettingsTabWidget final : public nxui::GlassBox {
public:
    explicit SettingsTabWidget(const std::string& text)
        : nxui::GlassBox(nxui::Axis::ROW) {
        setAlignItems(nxui::AlignItems::CENTER);
        setJustifyContent(nxui::JustifyContent::FLEX_START);
        setPadding(0.f, 22.f, 0.f, 22.f);
        setCornerRadius(18.f);
        setBorderWidth(1.f);
        setWireframeEnabled(false);

        m_label = std::make_shared<nxui::Label>(text);
        m_label->setGrow(1.f);
        m_label->setHAlign(nxui::Label::HAlign::Left);
        m_label->setVAlign(nxui::Label::VAlign::Center);
        addChild(m_label);
    }

    void sync(const std::string& text,
              nxui::Font* font,
              const nxui::Theme* theme,
              bool selected,
              bool focused,
              float uiTime,
              float accentWidth) {
        m_selected = selected;
        m_focused = focused;
        m_accentWidth = accentWidth;
        m_accentColor = theme ? theme->cursorNormal : nxui::Color::white();

        if (font) {
            m_label->setFont(font);
        }
        m_label->setText(text);

        float breathe = selected ? (0.5f + 0.5f * std::sin(uiTime * 4.6f)) : 0.f;
        float textScale = selected ? (0.83f + 0.018f * breathe) : 0.79f;
        m_label->setScale(textScale);
        m_label->setSize(std::max(0.f, rect().width - 36.f), rect().height);

        if (theme) {
            nxui::Color textColor = selected ? theme->textPrimary : theme->textSecondary;
            nxui::Color baseColor = theme->panelBase.withAlpha(selected ? 0.12f : 0.025f);
            nxui::Color borderColor = selected
                ? theme->cursorNormal.withAlpha(focused ? 0.54f : 0.34f)
                : theme->panelBorder.withAlpha(focused ? 0.18f : 0.08f);
            nxui::Color hiColor = theme->panelHighlight.withAlpha(selected ? 0.08f : 0.015f);

            setBaseColor(baseColor);
            setBorderColor(borderColor);
            setHighlightColor(hiColor);
            setBorderWidth(selected || focused ? 1.2f : 1.f);
            setScale(selected ? (1.01f + 0.01f * breathe) : 1.f);

            m_label->setTextColor(textColor.withAlpha(opacity()));
        }

        layout();
    }

protected:
    void onRender(nxui::Renderer& ren) override {
        nxui::GlassBox::onRender(ren);

        if (!m_selected || opacity() <= 0.01f)
            return;

        nxui::Rect r = rect();
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
    float m_accentWidth = 3.f;
    nxui::Color m_accentColor = nxui::Color::white();
};

class SettingsItemCard final : public nxui::GlassBox {
public:
    SettingsItemCard(SettingsScreen::SettingItem& item,
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
        const bool isSection = (m_item.type == SettingsScreen::ItemType::Section);
        const bool isActionLike = m_item.type == SettingsScreen::ItemType::Action
            || m_item.type == SettingsScreen::ItemType::Selector
            || m_item.type == SettingsScreen::ItemType::ColorPicker;

        setCornerRadius(isSection ? 14.f : 18.f);
        setBorderWidth(isSection ? 0.f : 1.f);
        setScale(selected ? 1.008f : 1.f);

        if (theme) {
            float baseAlpha = isSection ? 0.0f
                : selected ? 0.14f
                : isActionLike ? 0.045f
                : m_item.focusable() ? 0.025f : 0.012f;
            float borderAlpha = isSection ? 0.0f
                : selected ? 0.44f
                : isActionLike ? 0.16f : 0.09f;
            float hiAlpha = selected ? 0.08f : (isSection ? 0.0f : 0.015f);

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
    SettingsScreen::SettingItem& m_item;
    std::shared_ptr<nxui::Box> m_content;
};

} // namespace


SettingsScreen::SettingsScreen() {
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
    setPanelOpacity(0.82f);

    m_focusCursor.setBorderWidth(2.6f);
    m_focusCursor.setCornerRadius(10.f);
    m_tabGlowY.setImmediate(0.f);
    m_contentGlowY.setImmediate(0.f);
    m_tabReveal.setImmediate(1.f);
    m_dropdownAnim.setImmediate(0.f);
    m_colorPickerAnim.setImmediate(0.f);
    m_trackToastAnim.setImmediate(0.f);
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
SettingsScreen::~SettingsScreen() {
    nxui::I18n::instance().removeLanguageChangedListener(m_i18nListenerId);
}

void SettingsScreen::setTheme(const nxui::Theme* t) {
    m_theme = t;
    if (!m_theme)
        return;

    setBaseColor(m_theme->panelBase.withAlpha(std::clamp(m_theme->panelBase.a * 0.72f, 0.12f, 0.30f)));
    setBorderColor(m_theme->panelBorder.withAlpha(std::clamp(m_theme->panelBorder.a * 0.92f, 0.14f, 0.42f)));
    setHighlightColor(m_theme->panelHighlight.withAlpha(std::clamp(m_theme->panelHighlight.a * 0.92f, 0.05f, 0.18f)));
    setLiquidGlassShade(m_theme->mode == nxui::ThemeMode::Dark ? 0.08f : -0.03f);
    invalidateBackdropCache();
}

void SettingsScreen::show() {
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
    m_tabGlowY.setImmediate(0.f);
    m_contentGlowY.setImmediate(0.f);
    m_tabReveal.setImmediate(0.f);
    m_tabReveal.set(1.f, 0.24f, nxui::Easing::outCubic);
    m_dropdownOpen = false;
    m_dropdownRawIdx = -1;
    m_dropdownHover = 0;
    m_dropdownAnim.setImmediate(0.f);
    m_colorPickerOpen = false;
    m_colorPickerRawIdx = -1;
    m_colorPickerSlider = 0;
    m_colorPickerAnim.setImmediate(0.f);
    m_touchDirectControl = false;
    m_ignoreInitialTouchRelease = true;
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_contentSlideAnim.setImmediate(1.f);
    m_tabAccentW.setImmediate(3.f);
    if (m_tabBar) rebuildTabBar();
    if (m_tabContent) rebuildContentItems();
    invalidateBackdropCache();

    setVisible(true);
    syncPanelState(0.f);
    setFocusable(true);
    setupActions();
}

void SettingsScreen::hide() {
    if (!m_active) return;
    DebugLog::log("[settings] hide()");
    if (m_closeSfxCb) m_closeSfxCb();
    m_dropdownOpen = false;
    m_dropdownRawIdx = -1;
    m_dropdownAnim.setImmediate(0.f);
    m_colorPickerOpen = false;
    m_colorPickerRawIdx = -1;
    m_colorPickerAnim.setImmediate(0.f);
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_animating = true;
    m_showing   = false;
    m_animT     = 0.f;

    setFocusable(false);
    clearActions();
}


void SettingsScreen::rebuildTabBar() {
    m_tabBar->clearChildren();
    nxui::Rect tr = tabsRect();
    m_tabBar->setRect(tr);

    float tabX = tr.x + kTabRailInset;
    float tabY = tr.y + kTabRailInset;
    float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);
    float tabH = kTabRowHeight - kTabCardGap;

    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        auto tabBox = std::make_shared<SettingsTabWidget>(m_tabs[i].name);
        tabBox->setTag(m_tabs[i].name);
        tabBox->setRect({tabX, tabY, tabW, tabH});
        m_tabBar->addChild(tabBox);
        tabY += tabH + kTabCardGap;
    }
}

void SettingsScreen::rebuildContentItems() {
    m_tabContent->clearChildren();
    nxui::Rect cr = contentRect();
    m_tabContent->setRect(cr);

    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    auto& items = m_tabs[m_tabIndex].items;
    auto& cache = m_cachedTabContentWidgets[(size_t)m_tabIndex];
    DebugLog::log("[settings] rebuildContent tab=%d items=%d cache=%s",
                  m_tabIndex, (int)items.size(), cache.empty() ? "miss" : "hit");

    if (cache.empty()) {
        cache.reserve(items.size());

        float y = 0.f;
        for (int i = 0; i < (int)items.size(); ++i) {
            float h = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
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

std::shared_ptr<nxui::Box> SettingsScreen::makeItemWidget(SettingItem& item) {
    settings::widgets::SettingWidgetContext ctx;
    ctx.font = &m_font;
    ctx.smallFont = &m_smallFont;
    ctx.theme = &m_theme;
    auto content = settings::widgets::createSettingItemWidget(item, ctx);
    return std::make_shared<SettingsItemCard>(item, content);
}
void SettingsScreen::onRender(nxui::Renderer& ren) {
    if (!m_active && !m_animating)
        return;

    float opacity = visibilityProgress();
    nxui::Rect p = panelRect(scale());

    if (m_theme)
        m_focusCursor.setColor(m_theme->cursorNormal);
    m_focusCursor.setOpacity(opacity);

    if (m_tabBar) m_tabBar->setRect(tabsRect(p));
    if (m_tabContent) m_tabContent->setRect(contentRect(p));

    const auto& tuning = settings::debug::settingsGlassTuning();
    bool needsBackdropRefresh = !m_backdropCacheValid
        || std::abs(m_cachedPreBlurRadius - tuning.preBlurRadius) > 0.001f
        || m_cachedBlurIterations != tuning.blurIterations;

    if (opacity > 0.01f) {
        if (needsBackdropRefresh) {
            ren.captureToOffscreen(false);
            if (tuning.blurIterations > 0 && tuning.preBlurRadius > 0.001f) {
                ren.applyBlur(tuning.preBlurRadius, tuning.blurIterations);
            }
            ren.copyOffscreen(0, kSettingsBackdropCacheTarget);
            m_backdropCacheValid = true;
            m_cachedPreBlurRadius = tuning.preBlurRadius;
            m_cachedBlurIterations = tuning.blurIterations;
        }
    }

    drawBackground(ren, p, opacity * 0.72f);

    if (opacity > 0.01f) {
        nxui::LiquidGlassSettings savedGlass = ren.liquidGlassSettings();
        auto& glass = ren.liquidGlassSettings();
        glass.refractionIntensity = std::clamp(tuning.refractionIntensity, 0.0f, 1.5f);
        glass.blurIntensity = std::max(0.0f, tuning.shaderBlurIntensity);
        glass.noiseIntensity = 0.0f;
        glass.glowIntensity = std::max(0.0f, tuning.glowIntensity);
        glass.saturation = std::max(0.0f, tuning.saturation);
        glass.opacityMultiplier = 1.0f;
        glass.roughness = std::max(0.0f, tuning.roughness);
        glass.powerFactor = std::max(1.001f, tuning.powerFactor);

        nxui::Color glassTint = m_theme
            ? m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark
                ? std::clamp(tuning.tintAlphaDark, 0.0f, 1.0f)
                : std::clamp(tuning.tintAlphaLight, 0.0f, 1.0f))
            : m_base.withAlpha(0.14f);
        nxui::Rect glassRect = p.shrunk(std::max(0.0f, tuning.inset));
        float glassRadius = std::max(12.0f, kPanelRadius - std::max(0.0f, tuning.inset) * 0.5f);

        ren.drawLiquidGlass(kSettingsBackdropCacheTarget, glassRect, glassRadius, glassTint, opacity,
                            std::clamp(tuning.shade, 0.0f, 1.0f));

        ren.liquidGlassSettings() = savedGlass;
    }

    onContentRender(ren);

    if (ren.boxWireframeEnabled()) {
        syncDebugWireframeRects(p);
        if (m_tabBar) m_tabBar->render(ren);
        if (m_tabContent) m_tabContent->render(ren);
    }

    m_focusCursor.render(ren);
}

void SettingsScreen::onContentRender(nxui::Renderer& ren) {
    float opacity = visibilityProgress();
    nxui::Rect p = panelRect(scale());
    float textOp = m_showing ? opacity : opacity * opacity;

    ren.pushClipRect(p);
    drawTabs(ren, p, textOp);
    drawContent(ren, p, textOp);
    drawDropdown(ren, p, textOp);
    drawColorPicker(ren, p, textOp);
    drawTrackChangedToast(ren, p, textOp);
    ren.popClipRect();
}

void SettingsScreen::syncDebugWireframeRects(const nxui::Rect& panel) {
    if (!m_tabBar || !m_tabContent) return;

    nxui::Rect tr = tabsRect(panel);
    nxui::Rect cr = contentRect(panel);

    m_tabBar->setRect(tr);
    auto& tabChildren = m_tabBar->children();
    float tabY = tr.y + kTabRailInset;
    float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);
    float tabH = kTabRowHeight - kTabCardGap;
    for (int i = 0; i < (int)tabChildren.size(); ++i) {
        tabChildren[i]->setRect({tr.x + kTabRailInset, tabY, tabW, tabH});
        tabY += tabH + kTabCardGap;
    }

    m_tabContent->setRect(cr);
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    auto& items = m_tabs[m_tabIndex].items;
    auto& itemChildren = m_tabContent->children();

    float slideT = std::clamp(m_contentSlideAnim.value(), 0.f, 1.f);
    float slideOffset = (1.f - slideT) * 24.f * (float)m_tabSwitchDir;

    float y = cr.y - m_scrollY + slideOffset;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
        float insetY = (items[i].type == ItemType::Section) ? 1.f : kContentCardInsetY;
        float cardH = std::max(0.f, h - (items[i].type == ItemType::Section ? 2.f : 6.f));
        itemChildren[i]->setRect({cr.x + kContentCardInsetX, y + insetY,
                                  std::max(0.f, cr.width - kContentCardInsetX * 2.f), cardH});
        y += h;
    }
}

void SettingsScreen::drawBackground(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || opacity <= 0.01f)
        return;

    nxui::Rect screen = {0.f, 0.f, (float)ren.width(), (float)ren.height()};
    nxui::Color scrim = nxui::Color::lerp(m_theme->background, nxui::Color::black(),
                                          m_theme->mode == nxui::ThemeMode::Dark ? 0.72f : 0.28f)
        .withAlpha((m_theme->mode == nxui::ThemeMode::Dark ? 0.14f : 0.10f) * opacity);

    ren.drawRect(screen, scrim);
}

void SettingsScreen::drawTabs(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_theme || !m_tabBar) return;
    nxui::Rect tr = tabsRect(panel);
    auto* tabPanel = static_cast<nxui::GlassBox*>(m_tabBar.get());

    tabPanel->setRect(tr);
    tabPanel->setOpacity(opacity);
    tabPanel->setCornerRadius(24.f);
    tabPanel->setBorderWidth(1.f);
    tabPanel->setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.08f : 0.10f));
    tabPanel->setBorderColor(m_theme->panelBorder.withAlpha(0.14f));
    tabPanel->setHighlightColor(m_theme->panelHighlight.withAlpha(0.03f));
    tabPanel->setPanelOpacity(1.f);

    auto& tabChildren = m_tabBar->children();
    float reveal = std::clamp(m_tabReveal.value(), 0.f, 1.f);
    float tabY = tr.y + kTabRailInset;
    float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);
    float tabH = kTabRowHeight - kTabCardGap;

    for (int i = 0; i < (int)tabChildren.size() && i < (int)m_tabs.size(); ++i) {
        float delay = std::min(0.45f, i * 0.04f);
        float local = std::clamp((reveal - delay) / 0.24f, 0.f, 1.f);
        float rowOpacity = opacity * local;
        float rowYOffset = (1.f - local) * 8.f;

        auto* tab = static_cast<SettingsTabWidget*>(tabChildren[i].get());
        tab->setRect({tr.x + kTabRailInset, tabY + rowYOffset, tabW, tabH});
        tab->setOpacity(rowOpacity);
        tab->sync(m_tabs[i].name,
                  m_font,
                  m_theme,
                  i == m_tabIndex,
                  m_focusArea == FocusArea::Tabs && i == m_tabIndex,
                  m_uiTime,
                  m_tabAccentW.value());
        tabY += tabH + kTabCardGap;
    }

    if (m_focusArea == FocusArea::Tabs && m_tabIndex >= 0 && m_tabIndex < (int)tabChildren.size()) {
        m_focusCursor.moveTo(tabChildren[m_tabIndex]->rect().expanded(1.f), 16.f, 0.08f);
    }

    m_tabBar->render(ren);
}

void SettingsScreen::drawContent(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_smallFont || !m_theme) return;
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    if (!m_tabContent) return;

    nxui::Rect cr = contentRect(panel);
    auto* contentPanel = static_cast<nxui::GlassBox*>(m_tabContent.get());
    contentPanel->setRect(cr);
    contentPanel->setOpacity(opacity);
    contentPanel->setCornerRadius(26.f);
    contentPanel->setBorderWidth(1.f);
    contentPanel->setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.06f : 0.08f));
    contentPanel->setBorderColor(m_theme->panelBorder.withAlpha(0.12f));
    contentPanel->setHighlightColor(m_theme->panelHighlight.withAlpha(0.03f));
    contentPanel->setPanelOpacity(1.f);

    auto& items = m_tabs[m_tabIndex].items;
    auto& itemChildren = m_tabContent->children();
    int focusedRawIdx = (m_focusArea == FocusArea::Content && focusableCount() > 0)
        ? rawIndexFromFocusable(m_contentIdx) : -1;

    float slideT = std::clamp(m_contentSlideAnim.value(), 0.f, 1.f);
    float slideOffset = (1.f - slideT) * 24.f * (float)m_tabSwitchDir;
    float slideOpacity = opacity * slideT;
    float reveal = std::clamp(m_tabReveal.value(), 0.f, 1.f);

    float y = cr.y + 14.f - m_scrollY + slideOffset;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
        float delay = std::min(0.60f, i * 0.055f);
        float local = std::clamp((reveal - delay) / 0.28f, 0.f, 1.f);
        float rowOpacity = slideOpacity * local;
        float rowYOffset = (1.f - local) * 10.f;
        float insetY = (items[i].type == ItemType::Section) ? 1.f : kContentCardInsetY;
        float cardH = std::max(0.f, h - (items[i].type == ItemType::Section ? 2.f : 6.f));

        itemChildren[i]->setRect({cr.x + kContentCardInsetX,
                                  y + rowYOffset + insetY,
                                  std::max(0.f, cr.width - kContentCardInsetX * 2.f),
                                  cardH});
        itemChildren[i]->setOpacity(rowOpacity);

        auto* card = static_cast<SettingsItemCard*>(itemChildren[i].get());
        bool selected = (i == focusedRawIdx);
        card->sync(m_theme, selected, rowOpacity);

        if (selected) {
            m_focusCursor.moveTo(itemChildren[i]->rect().expanded(1.f),
                                 items[i].type == ItemType::Section ? 14.f : 18.f,
                                 0.08f);
        }
        y += h;
    }

    ren.pushClipRect(cr);
    m_tabContent->render(ren);
    ren.popClipRect();
}

void SettingsScreen::drawDropdown(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || !m_smallFont || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    float open = m_dropdownAnim.value();
    if (open <= 0.01f) return;

    auto& items = m_tabs[m_tabIndex].items;
    if (m_dropdownRawIdx < 0 || m_dropdownRawIdx >= (int)items.size()) return;
    auto& item = items[m_dropdownRawIdx];
    if (item.type != ItemType::Selector || item.options.empty()) return;

    nxui::Rect cr = contentRect(panel);

    float y = cr.y - m_scrollY;
    for (int i = 0; i < m_dropdownRawIdx; ++i)
        y += (items[i].type == ItemType::Section) ? kSectionHeight : kRowHeight;
    float rowH = (item.type == ItemType::Section) ? kSectionHeight : kRowHeight;

    float ctrlX = cr.x + cr.width * 0.40f;
    float ctrlW = cr.width * 0.60f;

    int total = (int)item.options.size();
    int visible = std::min(total, 6);
    float optH = 42.f;
    float listH = visible * optH + 10.f;

    int start = 0;
    if (total > visible)
        start = std::clamp(m_dropdownHover - visible / 2, 0, total - visible);

    float dy = y + rowH + 6.f;
    if (dy + listH > cr.bottom() - 4.f)
        dy = y - listH - 6.f;

    float scale = 0.94f + 0.06f * open;
    float w = ctrlW * scale;
    float h = listH * scale;
    float dx = ctrlX + (ctrlW - w) * 0.5f;
    float fy = dy + (listH - h) * 0.5f;

    nxui::Rect pop = { dx, fy, w, h };

    nxui::Color bg = (m_theme->mode == nxui::ThemeMode::Dark)
        ? nxui::Color(0.10f, 0.10f, 0.14f, 0.97f * opacity * open)
        : nxui::Color(0.97f, 0.97f, 0.99f, 0.98f * opacity * open);

    ren.drawRoundedRect(pop, bg, 10.f);
    ren.drawRoundedRectOutline(pop,
                               m_theme->panelBorder.withAlpha(0.8f * opacity * open),
                               10.f, 1.2f);

    nxui::Rect listClip = pop.shrunk(4.f);
    ren.pushClipRect(listClip);

    for (int i = 0; i < visible; ++i) {
        int idx = start + i;
        float ry = listClip.y + 2.f + i * optH;
        nxui::Rect rr = { listClip.x + 2.f, ry, listClip.width - 4.f, optH };

        bool hovered = idx == m_dropdownHover;
        if (hovered) {
            float pulse = 0.9f + 0.1f * (std::sin(m_uiTime * 5.2f) * 0.5f + 0.5f);
            nxui::Color hi = m_theme->cursorNormal.withAlpha(0.22f * pulse * opacity * open);
            ren.drawRoundedRect(rr, hi, 7.f);
            m_focusCursor.moveTo(rr.shrunk(0.5f), 7.f, 0.08f);
        }

        nxui::Color tc = (idx == item.intVal) ? m_theme->textPrimary : m_theme->textSecondary;
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
        float tx = rr.x + 10.f;
        float ty = rr.y + (rr.height - tsz.y * 0.7f) * 0.5f;
        ren.drawText(displayText, {tx, ty}, m_smallFont, tc.withAlpha(opacity * open), 0.76f);
    }

    ren.popClipRect();
}

void SettingsScreen::drawTrackChangedToast(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
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

void SettingsScreen::drawColorPicker(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || !m_smallFont || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    float open = m_colorPickerAnim.value();
    if (open <= 0.01f) return;

    auto& items = m_tabs[m_tabIndex].items;
    if (m_colorPickerRawIdx < 0 || m_colorPickerRawIdx >= (int)items.size()) return;
    auto& item = items[m_colorPickerRawIdx];
    if (item.type != ItemType::ColorPicker) return;

    nxui::Rect cr = contentRect(panel);
    float H = item.colorH, S = item.colorS, L = item.colorL;

    constexpr float popW = 320.f;
    constexpr float popH = 230.f;
    constexpr float pad = 16.f;
    constexpr float sliderH = 18.f;
    constexpr float sliderGap = 10.f;
    constexpr float labelW = 80.f;
    constexpr float swatchH = 42.f;
    constexpr float knobR = 9.f;

    float px = cr.x + (cr.width - popW) * 0.5f;
    float py = cr.y + (cr.height - popH) * 0.5f;

    float scale = 0.92f + 0.08f * open;
    float w = popW * scale;
    float h = popH * scale;
    float fx = px + (popW - w) * 0.5f;
    float fy = py + (popH - h) * 0.5f;
    nxui::Rect pop = { fx, fy, w, h };

    nxui::Color bg = (m_theme->mode == nxui::ThemeMode::Dark)
        ? nxui::Color(0.08f, 0.08f, 0.12f, 0.97f * opacity * open)
        : nxui::Color(0.96f, 0.96f, 0.98f, 0.98f * opacity * open);
    ren.drawRoundedRect(pop, bg, 14.f);
    ren.drawRoundedRectOutline(pop,
                               m_theme->panelBorder.withAlpha(0.7f * opacity * open),
                               14.f, 1.2f);

    ren.pushClipRect(pop.shrunk(2.f));

    float contentX = pop.x + pad;
    float contentW = pop.width - pad * 2.f;
    float curY = pop.y + pad;

    nxui::Color previewCol = nxui::Color::fromHSL(H, S, L, 1.f);
    nxui::Rect swatchRect = { contentX, curY, contentW, swatchH };
    ren.drawRoundedRect(swatchRect, previewCol.withAlpha(opacity * open), 8.f);
    ren.drawRoundedRectOutline(swatchRect,
                                m_theme->panelBorder.withAlpha(0.5f * opacity * open),
                                8.f, 1.f);
    curY += swatchH + pad;

    const char* labels[3] = { "H", "S", "L" };
    float vals[3] = { H, S, L };

    float sliderX = contentX + labelW;
    float sliderW = contentW - labelW - 40.f;

    for (int si = 0; si < 3; ++si) {
        float rowY = curY + si * (sliderH + sliderGap);
        float t = vals[si];
        bool active = (m_colorPickerOpen && si == m_colorPickerSlider);

        nxui::Color labelCol = active ? m_theme->textPrimary : m_theme->textSecondary;
        ren.drawText(labels[si], {contentX, rowY + 1.f}, m_smallFont,
                     labelCol.withAlpha(opacity * open), 0.72f);

        nxui::Rect track = { sliderX, rowY + (sliderH - 12.f) * 0.5f, sliderW, 12.f };
        if (si == 0) {
            int segs = 12;
            float segW = track.width / segs;
            for (int j = 0; j < segs; ++j) {
                float h0 = (float)j / segs;
                float h1 = (float)(j + 1) / segs;
                nxui::Color c0 = nxui::Color::fromHSL(h0, 0.9f, 0.5f, opacity * open);
                nxui::Color c1 = nxui::Color::fromHSL(h1, 0.9f, 0.5f, opacity * open);
                nxui::Rect seg = { track.x + j * segW, track.y, segW + 1.f, track.height };
                nxui::Color mixed(
                    (c0.r + c1.r) * 0.5f,
                    (c0.g + c1.g) * 0.5f,
                    (c0.b + c1.b) * 0.5f,
                    opacity * open
                );
                if (j == 0) {
                    ren.drawRoundedRect({seg.x, seg.y, seg.width, seg.height}, c0, 4.f);
                } else if (j == segs - 1) {
                    ren.drawRoundedRect({seg.x, seg.y, seg.width, seg.height}, c1, 4.f);
                } else {
                    ren.drawRect(seg, mixed);
                }
            }
        } else if (si == 1) {
            nxui::Color sLeft = nxui::Color::fromHSL(H, 0.f, L, opacity * open);
            nxui::Color sRight = nxui::Color::fromHSL(H, 1.f, L, opacity * open);
            nxui::Color sMid((sLeft.r + sRight.r) * 0.5f,
                             (sLeft.g + sRight.g) * 0.5f,
                             (sLeft.b + sRight.b) * 0.5f, opacity * open);
            ren.drawRoundedRect(track, sMid, 4.f);
            nxui::Rect lHalf = { track.x, track.y, track.width * 0.5f, track.height };
            nxui::Rect rHalf = { track.x + track.width * 0.5f, track.y, track.width * 0.5f, track.height };
            nxui::Color lMid((sLeft.r * 0.7f + sMid.r * 0.3f),
                             (sLeft.g * 0.7f + sMid.g * 0.3f),
                             (sLeft.b * 0.7f + sMid.b * 0.3f), opacity * open);
            nxui::Color rMid((sRight.r * 0.7f + sMid.r * 0.3f),
                             (sRight.g * 0.7f + sMid.g * 0.3f),
                             (sRight.b * 0.7f + sMid.b * 0.3f), opacity * open);
            ren.drawRect(lHalf, lMid);
            ren.drawRoundedRect({lHalf.x, lHalf.y, lHalf.width + 2.f, lHalf.height}, lMid, 4.f);
            ren.drawRoundedRect({rHalf.x - 2.f, rHalf.y, rHalf.width + 2.f, rHalf.height}, rMid, 4.f);
        } else {
            nxui::Color lLeft(0.f, 0.f, 0.f, opacity * open);
            nxui::Color lMid = nxui::Color::fromHSL(H, S, 0.5f, opacity * open);
            nxui::Color lRight(1.f, 1.f, 1.f, opacity * open);
            nxui::Rect leftH = { track.x, track.y, track.width * 0.5f, track.height };
            nxui::Rect rightH = { track.x + track.width * 0.5f, track.y, track.width * 0.5f, track.height };
            nxui::Color darkMix((lLeft.r + lMid.r) * 0.5f,
                                (lLeft.g + lMid.g) * 0.5f,
                                (lLeft.b + lMid.b) * 0.5f, opacity * open);
            nxui::Color lightMix((lMid.r + lRight.r) * 0.5f,
                                 (lMid.g + lRight.g) * 0.5f,
                                 (lMid.b + lRight.b) * 0.5f, opacity * open);
            ren.drawRoundedRect({leftH.x, leftH.y, leftH.width + 2.f, leftH.height}, darkMix, 4.f);
            ren.drawRoundedRect({rightH.x - 2.f, rightH.y, rightH.width + 2.f, rightH.height}, lightMix, 4.f);
        }

        ren.drawRoundedRectOutline(track,
                                    m_theme->panelBorder.withAlpha(0.4f * opacity * open),
                                    4.f, 0.8f);

        float knobX = sliderX + t * (sliderW - knobR * 2.f) + knobR;
        float knobY = rowY + sliderH * 0.5f;
        nxui::Color knobCol(1.f, 1.f, 1.f, opacity * open);
        ren.drawCircle({knobX, knobY}, knobR, knobCol, 24);
        ren.drawCircle({knobX, knobY}, knobR - 2.f,
                       nxui::Color::fromHSL(
                           (si == 0) ? t : H,
                           (si == 1) ? t : S,
                           (si == 2) ? t : L,
                           opacity * open),
                       24);

        if (active) {
            float pulse = 0.8f + 0.2f * (std::sin(m_uiTime * 5.0f) * 0.5f + 0.5f);
            nxui::Rect focRow = { sliderX - 4.f, rowY - 3.f, sliderW + 48.f, sliderH + 6.f };
            m_focusCursor.moveTo(focRow, 6.f, 0.06f);
        }

        int pct = (int)std::round(t * 100.f);
        std::string valStr = std::to_string(pct) + "%";
        float valX = sliderX + sliderW + 6.f;
        ren.drawText(valStr, {valX, rowY + 1.f}, m_smallFont,
                     m_theme->textSecondary.withAlpha(opacity * open), 0.66f);
    }

    ren.popClipRect();
}
