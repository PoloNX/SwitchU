#include "TabbedOverlayScreen.hpp"
#include "SettingsGlassTuning.hpp"
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
static constexpr float kSettingsBlurRadius = 6.0f;
static constexpr int kSettingsBlurIter = 1;

namespace {

static constexpr float kTabRailInset = 14.f;
static constexpr float kTabCardGap = 10.f;
static constexpr float kContentCardInsetX = 18.f;
static constexpr float kContentCardInsetY = 8.f;

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

        float textScale = selected ? 0.91f : 0.87f;
        if (font && rect().width > 44.f) {
            const float measured = font->measure(text).x;
            if (measured > 0.f)
                textScale = std::min(textScale, (rect().width - 44.f) / measured);
            textScale = std::max(0.55f, textScale);
        }
        if (std::abs(m_cachedTextScale - textScale) > 0.001f) {
            m_cachedTextScale = textScale;
            m_label->setScale(textScale);
        }
        m_label->setOpacity(opacity());
        m_label->setRect({rect().x + 22.f, rect().y,
                          std::max(0.f, rect().width - 44.f), rect().height});

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
            setScale(selected ? 1.01f : 1.f);

            m_label->setTextColor(textColor);
        }
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
    if (item.type == ItemType::Section) return kSectionHeight;
    if (!item.wrapLabel) return kRowHeight;

    nxui::Label probe(item.label);
    if (m_font) probe.setFont(m_font);
    probe.setScale(0.94f);
    probe.setMultiline(true);
    // Content card inset (36), card content inset (28), row inset (40).
    const float labelWidth = std::max(1.f, contentWidth - 104.f);
    return std::max(kRowHeight, probe.measureWrappedText(labelWidth).y + 34.f);
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

    if (!usesCustomContentLayout()) {
        m_cachedTabContentWidgets.clear();
        m_cachedTabContentWidgets.resize(m_tabs.size());

        if (isActive())
            rebuildContentItems();
    }
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
    int visible = std::min((int)item.options.size(), 6);
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


void TabbedOverlayScreen::rebuildTabBar() {
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

void TabbedOverlayScreen::rebuildContentItems() {
    m_tabContent->clearChildren();
    nxui::Rect cr = contentRect();
    m_tabContent->setRect(cr);

    if (usesCustomContentLayout())
        return;

    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    ensureTabLoaded(m_tabIndex);
    auto& items = m_tabs[m_tabIndex].items;
    auto& cache = m_cachedTabContentWidgets[(size_t)m_tabIndex];
    DebugLog::log("[settings] rebuildContent tab=%d items=%d cache=%s",
                  m_tabIndex, (int)items.size(), cache.empty() ? "miss" : "hit");

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

    const auto& tuning = settings::debug::settingsGlassTuning();
    drawBackground(ren, p, opacity * 0.72f);

    if (opacity > 0.01f) {
        nxui::Color glassTint = m_theme
            ? m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.94f : 0.96f)
            : m_base.withAlpha(0.94f);
        nxui::Rect glassRect = p.shrunk(std::max(0.0f, tuning.inset));
        float glassRadius = std::max(12.0f, kPanelRadius - std::max(0.0f, tuning.inset) * 0.5f);

        const nxui::Color border = m_theme
            ? m_theme->panelBorder.withAlpha(0.32f)
            : nxui::Color::white().withAlpha(0.18f);
        const nxui::Color highlight = m_theme
            ? m_theme->panelHighlight.withAlpha(0.11f)
            : nxui::Color::white().withAlpha(0.08f);
        ren.drawFrostedInset(glassRect, glassTint, border, highlight,
                             glassRadius, opacity);
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

    ren.pushClipRect(p);
    drawOverlayHeader(ren, p, textOp);
    drawTabs(ren, p, textOp);
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

    tabPanel->setRect(tr);
    tabPanel->setOpacity(opacity);
    tabPanel->setCornerRadius(24.f);
    tabPanel->setBorderWidth(1.f);
    tabPanel->setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.94f : 0.96f));
    tabPanel->setBorderColor(m_theme->panelBorder.withAlpha(0.30f));
    tabPanel->setHighlightColor(m_theme->panelHighlight.withAlpha(0.08f));
    tabPanel->setPanelOpacity(1.f);

    auto& tabChildren = m_tabBar->children();
    float reveal = std::clamp(m_tabReveal.value(), 0.f, 1.f);
    float tabY = tr.y + kTabRailInset;
    float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);
    float tabH = kTabRowHeight - kTabCardGap;
    float rowOpacity = opacity * reveal;
    float rowYOffset = (1.f - reveal) * 6.f;

    for (int i = 0; i < (int)tabChildren.size() && i < (int)m_tabs.size(); ++i) {
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

void TabbedOverlayScreen::drawContent(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_smallFont || !m_theme) return;
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    if (!m_tabContent) return;

    nxui::Rect cr = contentRect(panel);
    auto* contentPanel = static_cast<nxui::GlassBox*>(m_tabContent.get());
    contentPanel->setRect(cr);
    contentPanel->setOpacity(opacity);
    contentPanel->setCornerRadius(26.f);
    contentPanel->setBorderWidth(1.f);
    contentPanel->setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.94f : 0.96f));
    contentPanel->setBorderColor(m_theme->panelBorder.withAlpha(0.30f));
    contentPanel->setHighlightColor(m_theme->panelHighlight.withAlpha(0.08f));
    contentPanel->setPanelOpacity(1.f);

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

    float slideT = std::clamp(m_contentSlideAnim.value(), 0.f, 1.f);
    float slideOffset = (1.f - slideT) * 18.f * (float)m_tabSwitchDir;
    float slideOpacity = opacity * slideT * std::clamp(m_tabReveal.value(), 0.f, 1.f);

    float y = cr.y + 16.f - m_scrollY + slideOffset;
    float x = cr.x;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = itemHeight(items[i], cr.width);
        float insetY = (items[i].type == ItemType::Section) ? 1.f : kContentCardInsetY;
        float cardH = std::max(0.f, h - (items[i].type == ItemType::Section ? 2.f : 6.f));
        nxui::Rect itemRect = {
            x + kContentCardInsetX,
            y + insetY,
            std::max(0.f, cr.width - kContentCardInsetX * 2.f),
            cardH
        };
        bool visible = itemRect.bottom() >= cr.y - 8.f && itemRect.y <= cr.bottom() + 8.f;

        itemChildren[i]->setVisible(visible);
        if (!visible) {
            y += h;
            continue;
        }

        itemChildren[i]->setRect(itemRect);
        itemChildren[i]->setOpacity(slideOpacity);

        auto* card = static_cast<SettingsItemCard*>(itemChildren[i].get());
        bool selected = (i == focusedRawIdx);
        card->sync(m_theme, selected, slideOpacity);

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
        ren.drawRoundedRect(rail, m_theme->panelBorder.withAlpha(0.16f * opacity), 1.5f);
        ren.drawRoundedRect(thumb, m_theme->cursorNormal.withAlpha(0.46f * opacity), 2.f);
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

    float y = cr.y + 16.f - m_scrollY;
    for (int i = 0; i < m_dropdownRawIdx; ++i)
        y += itemHeight(items[i], cr.width);
    float rowH = itemHeight(item, cr.width);

    float ctrlX = cr.x + cr.width * 0.40f;
    float ctrlW = cr.width * 0.60f;

    int total = (int)item.options.size();
    int visible = std::min(total, 6);
    float optH = 46.f;
    float listH = visible * optH + 16.f;

    float visualStart = 0.f;
    if (total > visible)
        visualStart = std::clamp(m_dropdownVisualStart, 0.f, (float)(total - visible));
    int start = std::clamp((int)std::floor(visualStart), 0, std::max(0, total - visible));
    float rowOffset = (float)start - visualStart;

    float dy = y + rowH + 6.f;
    if (dy + listH > cr.bottom() - 4.f)
        dy = y - listH - 6.f;

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
