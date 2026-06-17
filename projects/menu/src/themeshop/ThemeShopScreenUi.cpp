#include "ThemeShopScreen.hpp"

#include "widgets/ActionButtonStyle.hpp"
#include "settings/SettingsGlassTuning.hpp"

#include "core/DebugLog.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <utility>

namespace {

constexpr int kGridCols = 2;
constexpr int kVisibleRows = 2;
constexpr float kContentInsetX = 24.f;
constexpr float kContentInsetY = 18.f;
constexpr float kHeaderHeight = 78.f;
constexpr float kGridGapX = 16.f;
constexpr float kGridGapY = 20.f;
constexpr float kFooterHintHeight = 74.f;
constexpr float kPreviewAspect = 16.f / 9.f;
constexpr float kSearchButtonWidth = 208.f;
constexpr float kRefreshButtonWidth = 144.f;
constexpr float kHeaderButtonGap = 12.f;
constexpr int kEntriesPerPage = kGridCols * kVisibleRows;
constexpr float kPageButtonWidth = 124.f;
constexpr float kPageCounterWidth = 96.f;
constexpr float kActionButtonRadius = 16.f;
constexpr int kThemeShopBackdropCacheTarget = 2;
constexpr size_t kTextMeasureCacheLimit = 1024;
constexpr size_t kEllipsizeCacheLimit = 1024;

std::unordered_map<std::string, nxui::Vec2> g_textMeasureCache;
std::unordered_map<std::string, std::string> g_ellipsizeCache;

std::string measureCacheKey(nxui::Font* font, const std::string& text) {
    return std::to_string((std::uintptr_t)font) + "\n"
         + std::to_string(font ? font->revision() : 0) + "\n"
         + text;
}

std::string ellipsizeCacheKey(nxui::Font* font, const std::string& text, float maxWidth, float scale) {
    return std::to_string((std::uintptr_t)font)
         + "\n" + std::to_string(font ? font->revision() : 0)
        + "\n" + std::to_string((int)std::lround(maxWidth * 4.f))
        + "\n" + std::to_string((int)std::lround(scale * 1000.f))
        + "\n" + text;
}

nxui::Vec2 measureTextCached(nxui::Font* font, const std::string& text) {
    if (!font || text.empty())
        return font ? font->measure(text) : nxui::Vec2{};

    std::string key = measureCacheKey(font, text);
    auto it = g_textMeasureCache.find(key);
    if (it != g_textMeasureCache.end())
        return it->second;

    nxui::Vec2 size = font->measure(text);
    if (g_textMeasureCache.size() >= kTextMeasureCacheLimit)
        g_textMeasureCache.clear();
    g_textMeasureCache.emplace(std::move(key), size);
    return size;
}

struct GridLayout {
    nxui::Rect header;
    nxui::Rect searchButton;
    nxui::Rect refreshButton;
    nxui::Rect pagePrevButton;
    nxui::Rect pageCounter;
    nxui::Rect pageNextButton;
    nxui::Rect grid;
    float cardW = 0.f;
    float cardH = 0.f;
};

GridLayout makeGridLayout(const nxui::Rect& content) {
    GridLayout layout;
    layout.header = {
        content.x + kContentInsetX,
        content.y + kContentInsetY,
        content.width - kContentInsetX * 2.f,
        kHeaderHeight
    };
    layout.searchButton = {
        layout.header.right() - kSearchButtonWidth,
        layout.header.y + 10.f,
        kSearchButtonWidth,
        46.f
    };
    layout.refreshButton = {
        layout.searchButton.x - kHeaderButtonGap - kRefreshButtonWidth,
        layout.header.y + 10.f,
        kRefreshButtonWidth,
        46.f
    };
    layout.grid = {
        content.x + kContentInsetX,
        layout.header.bottom() + 12.f,
        content.width - kContentInsetX * 2.f,
        content.height - (layout.header.bottom() - content.y) - 12.f - kFooterHintHeight
    };
    const float pageControlsW = kPageButtonWidth * 2.f + kPageCounterWidth + kHeaderButtonGap * 2.f;
    const float pageControlsX = layout.grid.x + std::max(0.f, (layout.grid.width - pageControlsW) * 0.5f);
    const float pageControlsY = layout.grid.bottom() + 8.f;
    layout.pagePrevButton = {
        pageControlsX,
        pageControlsY,
        kPageButtonWidth,
        46.f
    };
    layout.pageCounter = {
        layout.pagePrevButton.right() + kHeaderButtonGap,
        pageControlsY,
        kPageCounterWidth,
        46.f
    };
    layout.pageNextButton = {
        layout.pageCounter.right() + kHeaderButtonGap,
        pageControlsY,
        kPageButtonWidth,
        46.f
    };
    layout.cardW = (layout.grid.width - kGridGapX * (kGridCols - 1)) / (float)kGridCols;
    layout.cardH = (layout.grid.height - kGridGapY * (kVisibleRows - 1)) / (float)kVisibleRows;
    return layout;
}

nxui::Rect gridCardRect(const GridLayout& layout, int localIndex) {
    int col = localIndex % kGridCols;
    int row = localIndex / kGridCols;
    return {
        layout.grid.x + col * (layout.cardW + kGridGapX),
        layout.grid.y + row * (layout.cardH + kGridGapY),
        layout.cardW,
        layout.cardH
    };
}

nxui::Rect detailDialogRect(const nxui::Rect& content) {
    return {
        content.x + 38.f,
        content.y + 42.f,
        content.width - 76.f,
        content.height - 84.f
    };
}

std::vector<nxui::Rect> detailButtonRects(const nxui::Rect& dialog, int count) {
    std::vector<nxui::Rect> rects;
    if (count <= 0)
        return rects;

    float gap = 14.f;
    float width = count == 1 ? 258.f : 246.f;
    float totalWidth = count * width + (count - 1) * gap;
    float x = dialog.right() - 30.f - totalWidth;
    float y = dialog.bottom() - 68.f;
    rects.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        rects.push_back({x + i * (width + gap), y, width, 46.f});
    }
    return rects;
}

std::vector<nxui::Rect> headerButtonRects(const GridLayout& layout, bool communityTab) {
    std::vector<nxui::Rect> rects;
    if (communityTab)
        rects.push_back(layout.refreshButton);
    rects.push_back(layout.searchButton);
    return rects;
}

std::string ellipsize(nxui::Font* font, const std::string& text, float maxWidth, float scale) {
    if (!font || text.empty() || maxWidth <= 0.f)
        return text;

    std::string cacheKey = ellipsizeCacheKey(font, text, maxWidth, scale);
    auto cached = g_ellipsizeCache.find(cacheKey);
    if (cached != g_ellipsizeCache.end())
        return cached->second;

    if (measureTextCached(font, text).x * scale <= maxWidth)
        return text;

    std::string out = text;
    while (!out.empty() && measureTextCached(font, out + "...").x * scale > maxWidth)
        out.pop_back();

    std::string result = out.empty() ? text : out + "...";
    if (g_ellipsizeCache.size() >= kEllipsizeCacheLimit)
        g_ellipsizeCache.clear();
    g_ellipsizeCache.emplace(std::move(cacheKey), result);
    return result;
}

nxui::Rect fitAspectRect(const nxui::Rect& bounds, float aspect) {
    if (bounds.width <= 0.f || bounds.height <= 0.f || aspect <= 0.f)
        return bounds;

    float width = std::min(bounds.width, bounds.height * aspect);
    float height = width / aspect;
    if (height > bounds.height) {
        height = bounds.height;
        width = height * aspect;
    }

    return {
        bounds.x + (bounds.width - width) * 0.5f,
        bounds.y + (bounds.height - height) * 0.5f,
        width,
        height
    };
}

nxui::Rect detailPreviewRect(const nxui::Rect& dialog) {
    constexpr float kBodyTopInset = 26.f;
    constexpr float kBodyBottomInset = 96.f;
    nxui::Rect bounds = {
        dialog.x + 22.f,
        dialog.y + kBodyTopInset,
        dialog.width * 0.50f,
        dialog.height - kBodyTopInset - kBodyBottomInset
    };
    return fitAspectRect(bounds, kPreviewAspect);
}

struct DetailPreviewControls {
    nxui::Rect prev;
    nxui::Rect counter;
    nxui::Rect next;
};

DetailPreviewControls detailPreviewControls(const nxui::Rect& preview) {
    DetailPreviewControls controls;
    float gap = 10.f;
    float rowY = preview.bottom() + 18.f;
    float rowH = 42.f;
    float prevW = 118.f;
    float counterW = 82.f;
    float nextW = 118.f;
    float totalW = prevW + counterW + nextW + gap * 2.f;
    float startX = preview.x + std::max(0.f, (preview.width - totalW) * 0.5f);

    controls.prev = {startX, rowY, prevW, rowH};
    controls.counter = {controls.prev.right() + gap, rowY, counterW, rowH};
    controls.next = {controls.counter.right() + gap, rowY, nextW, rowH};
    return controls;
}

std::vector<nxui::Rect> detailPreviewControlRects(const DetailPreviewControls& controls,
                                                 const nxui::Rect& preview,
                                                 int screenshotCount) {
    std::vector<nxui::Rect> rects;
    if (screenshotCount <= 0)
        return rects;
    if (screenshotCount > 1)
        rects.push_back(controls.prev);
    rects.push_back(preview);
    if (screenshotCount > 1)
        rects.push_back(controls.next);
    return rects;
}

nxui::Rect lerpRect(const nxui::Rect& from, const nxui::Rect& to, float t) {
    return {
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
        from.width + (to.width - from.width) * t,
        from.height + (to.height - from.height) * t,
    };
}

nxui::Rect scaledRect(const nxui::Rect& rect, float scale) {
    if (std::abs(scale - 1.f) <= 0.001f) {
        return rect;
    }

    float width = rect.width * scale;
    float height = rect.height * scale;
    return {
        rect.x + (rect.width - width) * 0.5f,
        rect.y + (rect.height - height) * 0.5f,
        width,
        height
    };
}

struct FullscreenOverlayLayout {
    nxui::Rect preview;
    nxui::Rect prev;
    nxui::Rect next;
    nxui::Rect close;
    nxui::Rect counter;
};

FullscreenOverlayLayout makeFullscreenOverlayLayout(const nxui::Rect& content) {
    FullscreenOverlayLayout layout;
    nxui::Rect inner = content.shrunk(18.f);
    nxui::Rect previewBounds = {
        inner.x + 44.f,
        inner.y + 44.f,
        inner.width - 88.f,
        inner.height - 88.f
    };
    layout.preview = fitAspectRect(previewBounds, kPreviewAspect);
    layout.prev = {inner.x + 16.f, layout.preview.y + layout.preview.height * 0.5f - 24.f, 92.f, 48.f};
    layout.next = {inner.right() - 108.f, layout.preview.y + layout.preview.height * 0.5f - 24.f, 92.f, 48.f};
    layout.close = {inner.right() - 128.f, inner.y + 18.f, 112.f, 40.f};
    layout.counter = {inner.x + (inner.width - 84.f) * 0.5f, inner.y + 18.f, 84.f, 40.f};
    return layout;
}

nxui::Rect fitTextureRect(const nxui::Rect& bounds, const nxui::Texture* texture) {
    if (!texture || texture->width() <= 0 || texture->height() <= 0)
        return bounds;

    float texW = (float)texture->width();
    float texH = (float)texture->height();
    float scale = std::min(bounds.width / texW, bounds.height / texH);
    float width = texW * scale;
    float height = texH * scale;
    return {
        bounds.x + (bounds.width - width) * 0.5f,
        bounds.y + (bounds.height - height) * 0.5f,
        width,
        height
    };
}

void drawSpinner(nxui::Renderer& ren, const nxui::Vec2& center, float radius,
                 float time, const nxui::Color& color, float opacity) {
    constexpr int kSegments = 12;
    for (int i = 0; i < kSegments; ++i) {
        float angle = ((float)i / (float)kSegments) * 6.2831853f + time * 5.0f;
        float lead = std::fmod((float)i + time * 12.f, (float)kSegments) / (float)kSegments;
        float alpha = std::clamp(0.12f + lead * 0.88f, 0.f, 1.f) * opacity;
        nxui::Vec2 from = {center.x + std::cos(angle) * (radius - 7.f), center.y + std::sin(angle) * (radius - 7.f)};
        nxui::Vec2 to = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
        ren.drawLine(from, to, color.withAlpha(alpha), 3.f);
    }
}

void drawChip(nxui::Renderer& ren, nxui::Font* font, const nxui::Rect& rect,
              const std::string& text, const nxui::Color& fill, const nxui::Color& border,
              const nxui::Color& textColor, float opacity, float scale = 0.72f) {
    float borderWidth = rect.height >= 42.f ? 1.8f : (rect.height >= 34.f ? 1.5f : 1.3f);
    ren.drawRoundedRect(rect, fill.withAlpha(fill.a * opacity), rect.height * 0.5f);
    ren.drawRoundedRectOutline(rect, border.withAlpha(border.a * opacity), rect.height * 0.5f, borderWidth);
    if (font) {
        std::string fitted = ellipsize(font, text, rect.width - 16.f, scale);
        nxui::Vec2 size = measureTextCached(font, fitted);
        ren.drawText(fitted,
                     {rect.x + (rect.width - size.x * scale) * 0.5f,
                      rect.y + (rect.height - size.y * scale) * 0.5f},
                     font,
                     textColor.withAlpha(opacity),
                     scale);
    }
}

void drawActionButtonChip(nxui::Renderer& ren,
                          nxui::Font* font,
                          const nxui::Rect& rect,
                          const std::string& text,
                          const nxui::Theme* theme,
                          const nxui::Color& textColor,
                          float opacity,
                          float emphasis,
                          float accentMix = -1.f,
                          float scale = 0.72f) {
    auto style = switchu::ui::resolveActionButtonStyle(theme, opacity, emphasis, accentMix);
    nxui::Rect buttonRect = switchu::ui::scaledActionButtonRect(rect, style.scale);
    float radius = std::min(kActionButtonRadius, buttonRect.height * 0.5f);

    switchu::ui::drawActionButtonChrome(ren, rect, radius, style);

    if (font) {
        float textScale = scale * style.scale;
        std::string fitted = ellipsize(font, text, buttonRect.width - 24.f, textScale);
        nxui::Vec2 size = measureTextCached(font, fitted);
        ren.drawText(fitted,
                     {buttonRect.x + (buttonRect.width - size.x * textScale) * 0.5f,
                      buttonRect.y + (buttonRect.height - size.y * textScale) * 0.5f},
                     font,
                     textColor.withAlpha(textColor.a * opacity),
                     textScale);
    }
}

void drawLiquidGlassPanel(nxui::Renderer& ren,
                          const nxui::Theme* theme,
                          const nxui::Rect& rect,
                          float radius,
                          float opacity,
                          float borderBoost = 1.f) {
    if (!theme || opacity <= 0.01f)
        return;

    const auto& tuning = settings::debug::settingsGlassTuning();
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

    nxui::Rect glassRect = rect.shrunk(std::max(0.0f, tuning.inset));
    float glassRadius = std::max(12.0f, radius - std::max(0.0f, tuning.inset) * 0.5f);
    nxui::Color glassTint = theme->panelBase.withAlpha(theme->mode == nxui::ThemeMode::Dark
        ? std::clamp(tuning.tintAlphaDark, 0.0f, 1.0f)
        : std::clamp(tuning.tintAlphaLight, 0.0f, 1.0f));

    ren.drawLiquidGlass(kThemeShopBackdropCacheTarget,
                        glassRect,
                        glassRadius,
                        glassTint,
                        opacity,
                        std::clamp(tuning.shade, 0.0f, 1.0f));
    ren.drawRoundedRectOutline(glassRect,
                               theme->panelBorder.withAlpha(0.24f * borderBoost * opacity),
                               glassRadius,
                               1.2f);
    ren.drawRoundedRectOutline(glassRect.shrunk(1.5f),
                               theme->panelHighlight.withAlpha(0.08f * opacity),
                               std::max(0.f, glassRadius - 1.5f),
                               1.0f);
    ren.liquidGlassSettings() = savedGlass;
}

void drawPreviewPlaceholder(nxui::Renderer& ren,
                            nxui::Font* smallFont,
                            const nxui::Theme* theme,
                            const nxui::Rect& rect,
                            const std::string& label,
                            bool loading,
                            float time,
                            float opacity) {
    ren.drawRoundedRect(rect, theme->panelBase.withAlpha(0.16f * opacity), 14.f);
    ren.drawRoundedRectOutline(rect, theme->panelBorder.withAlpha(0.18f * opacity), 14.f, 1.f);

    nxui::Rect inner = {rect.x + 4.f, rect.y + 4.f, rect.width - 8.f, rect.height - 8.f};
    ren.drawRoundedRect(inner, theme->background.withAlpha(0.22f * opacity), 12.f);

    nxui::Vec2 center = {inner.x + inner.width * 0.5f, inner.y + inner.height * 0.5f};
    if (loading) {
        drawSpinner(ren, {center.x, center.y - 12.f}, 14.f, time, theme->cursorNormal, opacity);
    }

    if (smallFont && !label.empty()) {
        std::string fitted = ellipsize(smallFont, label, inner.width - 20.f, 0.64f);
        nxui::Vec2 size = measureTextCached(smallFont, fitted);
        float textY = center.y + (loading ? 12.f : -size.y * 0.32f);
        ren.drawText(fitted,
                     {inner.x + (inner.width - size.x * 0.64f) * 0.5f, textY},
                     smallFont,
                     theme->textSecondary.withAlpha(0.92f * opacity),
                     0.64f);
    }
}

void drawThemePreview(nxui::Renderer& ren,
                      nxui::Font* smallFont,
                      const nxui::Theme* theme,
                      const nxui::Rect& rect,
                      const nxui::Texture* texture,
                      const std::string& placeholderLabel,
                      bool loading,
                      float time,
                      float opacity) {
    if (!theme)
        return;

    if (texture && texture->valid()) {
        ren.drawRoundedRect(rect, theme->panelBase.withAlpha(0.14f * opacity), 14.f);
        ren.drawRoundedRectOutline(rect, theme->panelBorder.withAlpha(0.18f * opacity), 14.f, 1.f);
        nxui::Rect inner = {rect.x + 4.f, rect.y + 4.f, rect.width - 8.f, rect.height - 8.f};
        ren.drawRoundedRect(inner, nxui::Color(0.f, 0.f, 0.f, 0.14f * opacity), 12.f);
        ren.drawTextureRounded(texture,
                               fitTextureRect(inner, texture),
                               12.f,
                               nxui::Color(1.f, 1.f, 1.f, opacity));
        return;
    }

    drawPreviewPlaceholder(ren, smallFont, theme, rect, placeholderLabel, loading, time, opacity);
}

std::string transferLabel(const ThemeTransferState& state) {
    if (state.label().empty())
        return {};
    if (state.progress01() < 0.f || state.progress01() > 1.f)
        return state.label();

    int percent = (int)std::lround(state.progress01() * 100.f);
    return state.label() + " (" + std::to_string(percent) + "%)";
}

} // namespace

bool ThemeShopScreen::isCommunityTab() const {
    return m_tabIndex == 1;
}

int ThemeShopScreen::currentEntryCount() const {
    return isCommunityTab() ? (int)m_communityEntries.size() : (int)m_themeShopEntries.size();
}

int ThemeShopScreen::currentSelectedIndex() const {
    if (isCommunityTab()) {
        for (int i = 0; i < (int)m_communityEntries.size(); ++i) {
            if (m_communityEntries[i].id == m_communitySelectedId)
                return i;
        }
        return m_communityEntries.empty() ? -1 : 0;
    }

    for (int i = 0; i < (int)m_themeShopEntries.size(); ++i) {
        if (m_themeShopEntries[i].id == m_themeShopSelectedId)
            return i;
    }
    return m_themeShopEntries.empty() ? -1 : 0;
}

void ThemeShopScreen::setCurrentSelectedIndex(int idx) {
    int count = currentEntryCount();
    if (count <= 0)
        return;

    idx = std::clamp(idx, 0, count - 1);
    if (isCommunityTab())
        m_communitySelectedId = m_communityEntries[(size_t)idx].id;
    else
        m_themeShopSelectedId = m_themeShopEntries[(size_t)idx].id;
}

int ThemeShopScreen::pageCount() const {
    int count = currentEntryCount();
    return std::max(1, (count + kEntriesPerPage - 1) / kEntriesPerPage);
}

int ThemeShopScreen::currentPage() const {
    int count = currentEntryCount();
    if (count <= 0)
        return 0;
    int scrollRow = isCommunityTab() ? m_communityScrollRow : m_installedScrollRow;
    return std::clamp(scrollRow / kVisibleRows, 0, pageCount() - 1);
}

void ThemeShopScreen::setCurrentPage(int page) {
    int count = currentEntryCount();
    if (count <= 0) {
        currentScrollRowRef() = 0;
        return;
    }

    page = std::clamp(page, 0, pageCount() - 1);
    int target = std::min(count - 1, page * kEntriesPerPage);
    setCurrentSelectedIndex(target);
    currentScrollRowRef() = page * kVisibleRows;
}

void ThemeShopScreen::stepPage(int delta) {
    int before = currentPage();
    setCurrentPage(before + delta);
}

int& ThemeShopScreen::currentScrollRowRef() {
    return isCommunityTab() ? m_communityScrollRow : m_installedScrollRow;
}

void ThemeShopScreen::ensureSelectionVisible() {
    int count = currentEntryCount();
    if (count <= 0) {
        currentScrollRowRef() = 0;
        return;
    }

    int& scrollRow = currentScrollRowRef();
    int maxPageScroll = std::max(0, (pageCount() - 1) * kVisibleRows);
    scrollRow = std::clamp(scrollRow, 0, maxPageScroll);

    int page = std::clamp(scrollRow / kVisibleRows, 0, pageCount() - 1);
    int pageStart = page * kEntriesPerPage;
    int pageEnd = std::min(count, pageStart + kEntriesPerPage);
    int selected = std::max(0, currentSelectedIndex());
    if (selected < pageStart || selected >= pageEnd)
        setCurrentSelectedIndex(std::min(count - 1, pageStart));
}

void ThemeShopScreen::openDetail() {
    if (currentEntryCount() <= 0)
        return;
    m_detailOpen = true;
    m_detailSheetAnim.setImmediate(0.f);
    m_detailSheetAnim.set(1.f, 0.22f, nxui::Easing::outCubic);
    m_detailFullscreen = false;
    m_detailFullscreenAnim.setImmediate(0.f);
    m_contentFocusArea = ContentFocusArea::Grid;
    m_detailButtonIndex = 0;
    m_detailScreenshotIndex = 0;
    m_detailPreviewButtonIndex = 0;
    m_detailFocusArea = (isCommunityTab() && detailScreenshotCount() > 0)
        ? DetailFocusArea::Preview
        : DetailFocusArea::Buttons;
    m_focusArea = FocusArea::Content;

    if (isCommunityTab())
        primeCommunityPreview(currentDetailCommunityPreviewPath());
}

void ThemeShopScreen::closeDetail() {
    m_detailOpen = false;
    m_detailSheetAnim.set(0.f, 0.16f, nxui::Easing::outCubic);
    m_detailFullscreen = false;
    m_detailFullscreenAnim.setImmediate(0.f);
    m_detailFocusArea = DetailFocusArea::Buttons;
    m_detailButtonIndex = 0;
    m_detailPreviewButtonIndex = 0;
    m_detailScreenshotIndex = 0;
    if (!m_packageTransferState.isRunning()) {
        m_packageTransferState.reset();
        m_packageTransferThemeId.clear();
        m_packageTransferInstallMode = false;
    } else if (isCommunityTab()) {
        clearCommunityPreviewCache();
        m_lastPreviewPrimeKey.clear();
    }
}

int ThemeShopScreen::detailButtonCount() const {
    if (isCommunityTab())
        return selectedCommunityThemeEntry() ? 2 : 0;

    const auto* entry = selectedThemeShopEntry();
    if (!entry)
        return 0;
    return entry->removable ? 2 : 1;
}

void ThemeShopScreen::activateDetailButton(int buttonIndex) {
    auto& i18n = nxui::I18n::instance();

    if (isCommunityTab()) {
        const auto* entry = selectedCommunityThemeEntry();
        if (!entry)
            return;

        if (m_packageTransferState.isRunning()) {
            requestToast(i18n.tr("themeshop.community.transfer_busy",
                                 "Another theme transfer is already running."),
                         2.5f);
            return;
        }

        std::string themeId = entry->id;
        clearCommunityPreviewCache();
        m_lastPreviewPrimeKey.clear();
        if (buttonIndex == 0) {
            if (m_themeShopDownloadCb) {
                m_themeShopDownloadCb(themeId);
            } else {
                requestToast(i18n.tr("themeshop.community.install_pending", "Install flow is not wired yet."), 2.5f);
            }
        } else if (buttonIndex == 1) {
            if (m_themeShopDownloadInstallCb) {
                m_themeShopDownloadInstallCb(themeId);
            } else {
                requestToast(i18n.tr("themeshop.community.download_apply_pending", "Download + apply flow is not wired yet."), 2.5f);
            }
        }
        return;
    }

    const auto* entry = selectedThemeShopEntry();
    if (!entry)
        return;

    std::string themeId = entry->id;
    bool removable = entry->removable;
    closeDetail();
    if (buttonIndex == 0) {
        if (m_themeShopApplyCb)
            m_themeShopApplyCb(themeId);
    } else if (buttonIndex == 1 && removable) {
        if (m_themeShopDeleteCb)
            m_themeShopDeleteCb(themeId);
    }
}

void ThemeShopScreen::updateCustomContent(float dt) {
    if (!usesCustomContentLayout())
        return;

    m_detailSheetAnim.update(std::min(dt, 0.03f));
    m_detailFullscreenAnim.update(std::min(dt, 0.03f));

    if (!m_showing) {
        closeDetail();
        clearCommunityPreviewCache();
        m_lastPreviewPrimeKey.clear();
        return;
    }

    if (isCommunityTab())
        syncFinishedCommunityPreviewLoads();
    else {
        clearCommunityPreviewCache();
        m_lastPreviewPrimeKey.clear();
    }

    int headerButtonCount = isCommunityTab() ? 2 : 1;
    m_headerButtonIndex = std::clamp(m_headerButtonIndex, 0, std::max(0, headerButtonCount - 1));

    if (m_lastCustomTabIndex != m_tabIndex) {
        m_lastCustomTabIndex = m_tabIndex;
        m_contentFocusArea = ContentFocusArea::Grid;
        closeDetail();
    }

    if (currentEntryCount() <= 0) {
        if (m_focusArea == FocusArea::Content)
            m_focusArea = FocusArea::Tabs;
        currentScrollRowRef() = 0;
        closeDetail();
        if (isCommunityTab()) {
            clearCommunityPreviewCache();
            m_lastPreviewPrimeKey.clear();
        }
        return;
    }

    ensureSelectionVisible();

    if (isCommunityTab() && !m_packageTransferState.isRunning()) {
        clampDetailScreenshotIndex();
        int previewButtonCount = detailScreenshotCount() > 0 ? 1 : 0;
        if (previewButtonCount <= 0) {
            m_detailPreviewButtonIndex = 0;
            if (m_detailFocusArea == DetailFocusArea::Preview)
                m_detailFocusArea = DetailFocusArea::Buttons;
        } else {
            m_detailPreviewButtonIndex = 0;
        }
        std::string primeKey = std::to_string(m_communityRevision)
            + "|" + m_communitySelectedId
            + "|" + std::to_string(m_communityScrollRow)
            + "|" + std::to_string(m_detailOpen ? 1 : 0)
            + "|" + std::to_string(m_detailScreenshotIndex);
        if (primeKey != m_lastPreviewPrimeKey) {
            m_lastPreviewPrimeKey = std::move(primeKey);
            trimCommunityPreviewCache();
            primeVisibleCommunityPreviews();
        }
    } else if (isCommunityTab()) {
        clearCommunityPreviewCache();
        m_lastPreviewPrimeKey.clear();
    }
}

bool ThemeShopScreen::handleCustomPressA() {
    if (m_focusArea == FocusArea::Tabs) {
        if (currentEntryCount() > 0) {
            m_focusArea = FocusArea::Content;
            m_contentFocusArea = ContentFocusArea::Grid;
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }

        if ((isCommunityTab() ? 2 : 1) > 0) {
            m_focusArea = FocusArea::Content;
            m_contentFocusArea = ContentFocusArea::Header;
            m_headerButtonIndex = isCommunityTab() ? 0 : 0;
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        return false;
    }

    if (!m_detailOpen && m_contentFocusArea == ContentFocusArea::Header) {
        if (isCommunityTab() && m_headerButtonIndex == 0) {
            refreshCommunityCatalog();
        } else {
            promptSearchQuery();
        }
        if (m_activateSfxCb) m_activateSfxCb();
        return true;
    }

    if (!m_detailOpen && m_contentFocusArea == ContentFocusArea::Pager) {
        int before = currentPage();
        if (m_pageButtonIndex == 0)
            stepPage(-1);
        else
            stepPage(1);
        if (before != currentPage() && m_navSfxCb)
            m_navSfxCb();
        return true;
    }

    if (m_detailOpen) {
        if (m_detailFullscreen) {
            m_detailFullscreen = false;
            m_detailFullscreenAnim.set(0.f, 0.16f, nxui::Easing::outQuad);
            if (m_activateSfxCb) m_activateSfxCb();
            return true;
        }

        if (isCommunityTab() && m_detailFocusArea == DetailFocusArea::Preview && detailScreenshotCount() > 0) {
            m_detailFullscreen = true;
            m_detailFullscreenAnim.set(1.f, 0.18f, nxui::Easing::outCubic);
            if (m_activateSfxCb) m_activateSfxCb();
            return true;
        }

        activateDetailButton(m_detailButtonIndex);
        if (m_activateSfxCb) m_activateSfxCb();
        return true;
    }

    if (currentEntryCount() > 0) {
        openDetail();
        if (m_activateSfxCb) m_activateSfxCb();
        return true;
    }

    return false;
}

void ThemeShopScreen::currentAccessibilityParts(std::string& context,
                                                std::string& position,
                                                std::string& summary,
                                                bool& forceRepeat) const {
    if (!usesCustomContentLayout()) {
        TabbedOverlayScreen::currentAccessibilityParts(context, position, summary, forceRepeat);
        return;
    }

    context.clear();
    position.clear();
    summary.clear();
    forceRepeat = false;

    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
        return;

    auto& i18n = nxui::I18n::instance();
    const std::string tabName = m_tabs[(size_t)m_tabIndex].name;
    if (m_focusArea == FocusArea::Tabs) {
        context = i18n.tr("accessibility.context.themes", "Themes");
        if (m_accessibilitySpeakPosition) {
            position = std::to_string(m_tabIndex + 1) + " "
                     + i18n.tr("accessibility.context.of", "of") + " "
                     + std::to_string((int)m_tabs.size());
        }
        summary = i18n.tr("accessibility.settings.tab_prefix", "Tab") + " " + tabName;
        if (m_accessibilitySpeakHints)
            summary += ". " + i18n.tr("accessibility.settings.tab_actions", "A or right to enter. Up and down to change tab. B to close.");
        return;
    }

    context = tabName;

    if (m_detailOpen) {
        std::string themeName;
        std::string author;
        if (isCommunityTab()) {
            if (const auto* entry = selectedCommunityThemeEntry()) {
                themeName = entry->name;
                author = entry->author;
            }
        } else if (const auto* entry = selectedThemeShopEntry()) {
            themeName = entry->name;
            author = entry->author;
        }
        if (themeName.empty())
            themeName = i18n.tr("settings.tabs.theme", "Theme");

        if (m_detailFullscreen) {
            summary = i18n.tr("themeshop.accessibility.fullscreen_preview", "Fullscreen preview")
                    + ". " + themeName;
            if (m_accessibilitySpeakHints)
                summary += ". " + i18n.tr("themeshop.accessibility.fullscreen_actions", "B to close the preview.");
            return;
        }

        std::string area = (m_detailFocusArea == DetailFocusArea::Preview)
            ? i18n.tr("themeshop.accessibility.preview_area", "Preview")
            : i18n.tr("themeshop.accessibility.actions_area", "Actions");
        summary = i18n.tr("themeshop.accessibility.detail", "Detail")
                + ". " + themeName + ". " + area;
        if (!author.empty())
            summary += ". " + i18n.tr("themeshop.accessibility.author", "Author") + ": " + author;
        if (m_accessibilitySpeakHints)
            summary += ". " + i18n.tr("themeshop.accessibility.detail_actions", "Left and right to change control. A to confirm. B to go back.");
        return;
    }

    if (m_contentFocusArea == ContentFocusArea::Header) {
        summary = isCommunityTab()
            ? i18n.tr("themeshop.accessibility.search_refresh", "Search and refresh")
            : i18n.tr("themeshop.accessibility.search_bar", "Search bar");
        if (m_accessibilitySpeakHints) {
            summary += ". " + (isCommunityTab()
                ? i18n.tr("themeshop.accessibility.header_actions", "Left and right to choose. A to confirm. Down to the grid.")
                : i18n.tr("themeshop.accessibility.search_actions", "A to search. Down to the grid."));
        }
        return;
    }

    if (m_contentFocusArea == ContentFocusArea::Pager) {
        summary = i18n.tr("themeshop.accessibility.pagination", "Pagination");
        if (m_accessibilitySpeakHints)
            summary += ". " + i18n.tr("themeshop.accessibility.pagination_actions", "Left and right to change button. A to change page. Up to the grid.");
        return;
    }

    std::string themeName;
    std::string detail;
    if (isCommunityTab()) {
        if (const auto* entry = selectedCommunityThemeEntry()) {
            themeName = entry->name;
            detail = entry->author;
        }
    } else if (const auto* entry = selectedThemeShopEntry()) {
        themeName = entry->name;
        detail = entry->source;
        if (entry->active)
            detail = detail.empty() ? "Actif" : detail + ", actif";
    }

    if (themeName.empty())
        themeName = i18n.tr("themeshop.accessibility.no_theme", "No theme");

    summary = themeName;
    if (!detail.empty())
        summary += ". " + detail;
    if (m_accessibilitySpeakPosition) {
        const int count = currentEntryCount();
        const int selected = currentSelectedIndex();
        if (selected >= 0 && count > 0) {
            position = std::to_string(selected + 1) + " "
                     + i18n.tr("accessibility.context.of", "of") + " "
                     + std::to_string(count);
        }
    }
    if (m_accessibilitySpeakHints)
        summary += ". " + i18n.tr("themeshop.accessibility.grid_actions", "Directional pad to navigate. A to open details. B to return to tabs.");
}

std::string ThemeShopScreen::currentAccessibilitySummary() const {
    std::string context;
    std::string position;
    std::string summary;
    bool forceRepeat = false;
    currentAccessibilityParts(context, position, summary, forceRepeat);
    std::string out = context;
    if (!summary.empty())
        out += (out.empty() ? "" : ". ") + summary;
    if (!position.empty())
        out += (out.empty() ? "" : ". ") + position;
    return out;
}

bool ThemeShopScreen::handleCustomPressB() {
    if (m_detailFullscreen) {
        m_detailFullscreen = false;
        m_detailFullscreenAnim.set(0.f, 0.16f, nxui::Easing::outQuad);
        if (m_closeSfxCb) m_closeSfxCb();
        return true;
    }

    if (m_detailOpen) {
        closeDetail();
        if (m_closeSfxCb) m_closeSfxCb();
        return true;
    }

    if (m_focusArea == FocusArea::Content) {
        m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }

    return false;
}

bool ThemeShopScreen::handleCustomPressX() {
    bool handled = promptSearchQuery();
    if (handled && m_activateSfxCb)
        m_activateSfxCb();
    return handled;
}

bool ThemeShopScreen::handleCustomNavUp() {
    if (m_detailFullscreen)
        return true;
    if (m_detailOpen) {
        if (isCommunityTab() && m_detailFocusArea == DetailFocusArea::Buttons && detailScreenshotCount() > 0) {
            m_detailFocusArea = DetailFocusArea::Preview;
            if (m_navSfxCb) m_navSfxCb();
        }
        return true;
    }
    if (m_focusArea != FocusArea::Content)
        return false;

    if (m_contentFocusArea == ContentFocusArea::Header) {
        m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }

    if (m_contentFocusArea == ContentFocusArea::Pager) {
        if (currentEntryCount() > 0) {
            m_contentFocusArea = ContentFocusArea::Grid;
            int pageStart = currentPage() * kEntriesPerPage;
            int target = std::min(currentEntryCount() - 1,
                                  pageStart + (kVisibleRows - 1) * kGridCols + m_pageButtonIndex);
            setCurrentSelectedIndex(target);
            ensureSelectionVisible();
        }
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }

    int count = currentEntryCount();
    if (count <= 0)
        return true;

    int selected = std::max(0, currentSelectedIndex());
    int pageStart = currentPage() * kEntriesPerPage;
    if (selected - kGridCols >= pageStart) {
        setCurrentSelectedIndex(selected - kGridCols);
        ensureSelectionVisible();
    } else {
        m_contentFocusArea = ContentFocusArea::Header;
        m_headerButtonIndex = isCommunityTab() ? std::clamp(selected % kGridCols, 0, 1) : 0;
    }
    if (m_navSfxCb) m_navSfxCb();
    return true;
}

bool ThemeShopScreen::handleCustomNavDown() {
    if (m_detailFullscreen)
        return true;
    if (m_detailOpen) {
        if (isCommunityTab() && m_detailFocusArea == DetailFocusArea::Preview && detailButtonCount() > 0) {
            m_detailFocusArea = DetailFocusArea::Buttons;
            if (m_navSfxCb) m_navSfxCb();
        }
        return true;
    }
    if (m_focusArea != FocusArea::Content)
        return false;

    if (m_contentFocusArea == ContentFocusArea::Header) {
        if (currentEntryCount() > 0)
            m_contentFocusArea = ContentFocusArea::Grid;
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }

    if (m_contentFocusArea == ContentFocusArea::Pager)
        return true;

    int count = currentEntryCount();
    if (count <= 0)
        return true;

    int selected = std::max(0, currentSelectedIndex());
    int pageEnd = std::min(count, (currentPage() + 1) * kEntriesPerPage);
    if (selected + kGridCols < pageEnd) {
        setCurrentSelectedIndex(selected + kGridCols);
        ensureSelectionVisible();
        if (m_navSfxCb) m_navSfxCb();
    } else {
        m_contentFocusArea = ContentFocusArea::Pager;
        m_pageButtonIndex = std::clamp(selected % kGridCols, 0, 1);
        if (m_navSfxCb) m_navSfxCb();
    }
    return true;
}

bool ThemeShopScreen::handleCustomNavLeft() {
    if (m_detailFullscreen) {
        int before = m_detailScreenshotIndex;
        stepDetailScreenshot(-1);
        if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
        return true;
    }

    if (m_detailOpen) {
        if (isCommunityTab() && m_detailFocusArea == DetailFocusArea::Preview) {
            int before = m_detailScreenshotIndex;
            stepDetailScreenshot(-1);
            if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
            return true;
        }

        if (m_detailButtonIndex > 0) {
            --m_detailButtonIndex;
            if (m_navSfxCb) m_navSfxCb();
        }
        return true;
    }

    if (m_focusArea != FocusArea::Content)
        return false;

    if (m_contentFocusArea == ContentFocusArea::Header) {
        if (m_headerButtonIndex > 0) {
            --m_headerButtonIndex;
        } else {
            m_focusArea = FocusArea::Tabs;
        }
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }

    if (m_contentFocusArea == ContentFocusArea::Pager) {
        if (m_pageButtonIndex > 0)
            --m_pageButtonIndex;
        else
            m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }

    int count = currentEntryCount();
    if (count <= 0)
        return true;

    int selected = std::max(0, currentSelectedIndex());
    if (selected % kGridCols > 0) {
        setCurrentSelectedIndex(selected - 1);
    } else {
        m_focusArea = FocusArea::Tabs;
    }
    if (m_navSfxCb) m_navSfxCb();
    return true;
}

bool ThemeShopScreen::handleCustomNavRight() {
    if (m_detailFullscreen) {
        int before = m_detailScreenshotIndex;
        stepDetailScreenshot(1);
        if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
        return true;
    }

    if (m_detailOpen) {
        if (isCommunityTab() && m_detailFocusArea == DetailFocusArea::Preview) {
            int before = m_detailScreenshotIndex;
            stepDetailScreenshot(1);
            if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
            return true;
        }

        if (m_detailButtonIndex + 1 < detailButtonCount()) {
            ++m_detailButtonIndex;
            if (m_navSfxCb) m_navSfxCb();
        }
        return true;
    }

    if (m_focusArea == FocusArea::Tabs) {
        if (currentEntryCount() > 0) {
            m_focusArea = FocusArea::Content;
            m_contentFocusArea = ContentFocusArea::Grid;
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        if ((isCommunityTab() ? 2 : 1) > 0) {
            m_focusArea = FocusArea::Content;
            m_contentFocusArea = ContentFocusArea::Header;
            m_headerButtonIndex = 0;
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        return false;
    }

    if (m_contentFocusArea == ContentFocusArea::Header) {
        int headerButtonCount = isCommunityTab() ? 2 : 1;
        if (m_headerButtonIndex + 1 < headerButtonCount) {
            ++m_headerButtonIndex;
        } else if (currentEntryCount() > 0) {
            m_contentFocusArea = ContentFocusArea::Grid;
        }
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }

    if (m_contentFocusArea == ContentFocusArea::Pager) {
        if (m_pageButtonIndex < 1)
            ++m_pageButtonIndex;
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }

    int count = currentEntryCount();
    if (count <= 0)
        return true;

    int selected = std::max(0, currentSelectedIndex());
    if (selected + 1 < count && selected % kGridCols < kGridCols - 1) {
        setCurrentSelectedIndex(selected + 1);
        if (m_navSfxCb) m_navSfxCb();
    }
    return true;
}

int ThemeShopScreen::hitTestGridCard(const nxui::Rect& content, float x, float y) const {
    GridLayout layout = makeGridLayout(content);
    int count = currentEntryCount();
    if (count <= 0 || !layout.grid.contains(x, y))
        return -1;

    int scrollRow = isCommunityTab() ? m_communityScrollRow : m_installedScrollRow;
    int start = scrollRow * kGridCols;
    int end = std::min(count, start + kGridCols * kVisibleRows);
    for (int i = start; i < end; ++i) {
        int local = i - start;
        if (gridCardRect(layout, local).contains(x, y))
            return i;
    }

    return -1;
}

bool ThemeShopScreen::handleCustomTouch(nxui::Input& input, const nxui::Rect&, const nxui::Rect& tabs, const nxui::Rect& content) {
    auto searchHit = [&](float x, float y) {
        return makeGridLayout(content).searchButton.contains(x, y);
    };
    auto refreshHit = [&](float x, float y) {
        return isCommunityTab() && makeGridLayout(content).refreshButton.contains(x, y);
    };
    auto pagePrevHit = [&](float x, float y) {
        return pageCount() > 1 && makeGridLayout(content).pagePrevButton.contains(x, y);
    };
    auto pageNextHit = [&](float x, float y) {
        return pageCount() > 1 && makeGridLayout(content).pageNextButton.contains(x, y);
    };
    auto detailPreviewHitIndex = [&](float x, float y) {
        if (!isCommunityTab() || !m_detailOpen)
            return -1;

        DetailPreviewControls controls = detailPreviewControls(detailPreviewRect(detailDialogRect(content)));
        nxui::Rect previewRect = detailPreviewRect(detailDialogRect(content));
        auto rects = detailPreviewControlRects(controls, previewRect, detailScreenshotCount());
        for (int i = 0; i < (int)rects.size(); ++i) {
            if (rects[(size_t)i].contains(x, y))
                return i;
        }
        return -1;
    };
    auto fullscreenHitIndex = [&](float x, float y) {
        FullscreenOverlayLayout overlay = makeFullscreenOverlayLayout(content);
        if (detailScreenshotCount() > 1 && overlay.prev.contains(x, y))
            return 0;
        if (detailScreenshotCount() > 1 && overlay.next.contains(x, y))
            return 1;
        if (overlay.close.contains(x, y) || overlay.preview.contains(x, y) || content.contains(x, y))
            return 2;
        return -1;
    };

    if (input.touchDown()) {
        float x = input.touchX();
        float y = input.touchY();
        m_themeTouchTarget = ThemeTouchTarget::None;
        m_themeTouchIndex = -1;
        m_themeTouchStartX = x;
        m_themeTouchStartY = y;

        if (tabs.contains(x, y))
            return false;
        if (!content.contains(x, y) && !m_detailOpen)
            return false;

        if (m_detailOpen) {
            if (m_detailFullscreen) {
                m_themeTouchTarget = ThemeTouchTarget::FullscreenPreview;
                m_themeTouchIndex = fullscreenHitIndex(x, y);
                return true;
            }

            nxui::Rect dialog = detailDialogRect(content);
            if (!dialog.contains(x, y)) {
                m_themeTouchTarget = ThemeTouchTarget::DetailBackdrop;
                return true;
            }

            int previewHit = detailPreviewHitIndex(x, y);
            if (previewHit >= 0) {
                m_themeTouchTarget = ThemeTouchTarget::DetailPreviewControl;
                m_themeTouchIndex = previewHit;
                return true;
            }

            auto buttons = detailButtonRects(dialog, detailButtonCount());
            for (int i = 0; i < (int)buttons.size(); ++i) {
                if (buttons[(size_t)i].contains(x, y)) {
                    m_themeTouchTarget = ThemeTouchTarget::DetailButton;
                    m_themeTouchIndex = i;
                    return true;
                }
            }

            return true;
        }

        if (searchHit(x, y)) {
            m_themeTouchTarget = ThemeTouchTarget::Search;
            return true;
        }

        if (refreshHit(x, y)) {
            m_themeTouchTarget = ThemeTouchTarget::Refresh;
            return true;
        }

        if (pagePrevHit(x, y)) {
            m_themeTouchTarget = ThemeTouchTarget::PagePrev;
            return true;
        }

        if (pageNextHit(x, y)) {
            m_themeTouchTarget = ThemeTouchTarget::PageNext;
            return true;
        }

        int hit = hitTestGridCard(content, x, y);
        if (hit >= 0) {
            m_themeTouchTarget = ThemeTouchTarget::GridCard;
            m_themeTouchIndex = hit;
            return true;
        }

        return content.contains(x, y);
    }

    if (input.isTouching() && m_themeTouchTarget != ThemeTouchTarget::None) {
        float dx = std::abs(input.touchX() - m_themeTouchStartX);
        float dy = std::abs(input.touchY() - m_themeTouchStartY);
        if (dx > 18.f || dy > 18.f) {
            m_themeTouchTarget = ThemeTouchTarget::None;
            m_themeTouchIndex = -1;
        }
        return true;
    }

    if (input.touchUp()) {
        float x = input.touchX();
        float y = input.touchY();
        ThemeTouchTarget target = m_themeTouchTarget;
        int hitIndex = m_themeTouchIndex;
        m_themeTouchTarget = ThemeTouchTarget::None;
        m_themeTouchIndex = -1;

        if (target == ThemeTouchTarget::None)
            return content.contains(x, y);

        switch (target) {
            case ThemeTouchTarget::Search:
                if (searchHit(x, y)) {
                    promptSearchQuery();
                    if (m_activateSfxCb) m_activateSfxCb();
                }
                return true;
            case ThemeTouchTarget::Refresh:
                if (refreshHit(x, y)) {
                    refreshCommunityCatalog();
                    if (m_activateSfxCb) m_activateSfxCb();
                }
                return true;
            case ThemeTouchTarget::PagePrev:
                if (pagePrevHit(x, y)) {
                    m_contentFocusArea = ContentFocusArea::Pager;
                    m_pageButtonIndex = 0;
                    int before = currentPage();
                    stepPage(-1);
                    if (before != currentPage() && m_navSfxCb) m_navSfxCb();
                }
                return true;
            case ThemeTouchTarget::PageNext:
                if (pageNextHit(x, y)) {
                    m_contentFocusArea = ContentFocusArea::Pager;
                    m_pageButtonIndex = 1;
                    int before = currentPage();
                    stepPage(1);
                    if (before != currentPage() && m_navSfxCb) m_navSfxCb();
                }
                return true;
            case ThemeTouchTarget::GridCard:
                if (hitTestGridCard(content, x, y) == hitIndex) {
                    bool alreadyFocused = (m_focusArea == FocusArea::Content
                        && m_contentFocusArea == ContentFocusArea::Grid
                        && currentSelectedIndex() == hitIndex);
                    setCurrentSelectedIndex(hitIndex);
                    m_focusArea = FocusArea::Content;
                    m_contentFocusArea = ContentFocusArea::Grid;
                    ensureSelectionVisible();
                    if (alreadyFocused) {
                        openDetail();
                        if (m_activateSfxCb) m_activateSfxCb();
                    } else if (m_navSfxCb) {
                        m_navSfxCb();
                    }
                }
                return true;
            case ThemeTouchTarget::DetailPreviewControl:
                if (hitIndex == detailPreviewHitIndex(x, y)) {
                    if (hitIndex == 0) {
                        if (detailScreenshotCount() > 1) {
                            int before = m_detailScreenshotIndex;
                            stepDetailScreenshot(-1);
                            if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
                        }
                    } else if ((hitIndex == 1 && detailScreenshotCount() > 1)
                            || (hitIndex == 0 && detailScreenshotCount() == 1)) {
                        m_detailFocusArea = DetailFocusArea::Preview;
                        m_detailFullscreen = true;
                        m_detailFullscreenAnim.set(1.f, 0.18f, nxui::Easing::outCubic);
                        if (m_activateSfxCb) m_activateSfxCb();
                    } else if (hitIndex == 2 && detailScreenshotCount() > 1) {
                        int before = m_detailScreenshotIndex;
                        stepDetailScreenshot(1);
                        if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
                    }
                }
                return true;
            case ThemeTouchTarget::DetailButton: {
                nxui::Rect dialog = detailDialogRect(content);
                auto buttons = detailButtonRects(dialog, detailButtonCount());
                if (hitIndex >= 0 && hitIndex < (int)buttons.size() && buttons[(size_t)hitIndex].contains(x, y)) {
                    m_detailButtonIndex = hitIndex;
                    activateDetailButton(hitIndex);
                    if (m_activateSfxCb) m_activateSfxCb();
                }
                return true;
            }
            case ThemeTouchTarget::FullscreenPreview:
                if (hitIndex == fullscreenHitIndex(x, y)) {
                    if (hitIndex == 0) {
                        int before = m_detailScreenshotIndex;
                        stepDetailScreenshot(-1);
                        if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
                    } else if (hitIndex == 1) {
                        int before = m_detailScreenshotIndex;
                        stepDetailScreenshot(1);
                        if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
                    } else if (hitIndex == 2) {
                        m_detailFullscreen = false;
                        m_detailFullscreenAnim.set(0.f, 0.16f, nxui::Easing::outQuad);
                        if (m_closeSfxCb) m_closeSfxCb();
                    }
                }
                return true;
            case ThemeTouchTarget::DetailBackdrop:
                if (!detailDialogRect(content).contains(x, y)) {
                    closeDetail();
                    if (m_closeSfxCb) m_closeSfxCb();
                }
                return true;
            case ThemeTouchTarget::None:
                return false;
        }
    }

    return false;
}

void ThemeShopScreen::drawCustomContent(nxui::Renderer& ren, const nxui::Rect&, const nxui::Rect& content, float opacity) {
    auto& i18n = nxui::I18n::instance();
    GridLayout layout = makeGridLayout(content);
    float slideT = std::clamp(m_contentSlideAnim.value(), 0.f, 1.f);
    float slideOffset = (1.f - slideT) * 24.f * (float)m_tabSwitchDir;
    float contentOpacity = opacity * slideT;
    int count = currentEntryCount();

    if (m_renderDebugFrames > 0) {
        const ThemeShopEntry* installed = selectedThemeShopEntry();
        const ThemeCatalogClient::Entry* community = selectedCommunityThemeEntry();
        DebugLog::log("[themeshop-render] tab=%d count=%d installedCount=%zu communityCount=%zu selectedInstalled=%s selectedCommunity=%s detail=%d fullscreen=%d focusArea=%d contentFocus=%d active=%d visible=%d opacity=%.2f contentOpacity=%.2f",
                      m_tabIndex,
                      count,
                      m_themeShopEntries.size(),
                      m_communityEntries.size(),
                      installed ? installed->id.c_str() : "<none>",
                      community ? community->id.c_str() : "<none>",
                      m_detailOpen ? 1 : 0,
                      m_detailFullscreen ? 1 : 0,
                      (int)m_focusArea,
                      (int)m_contentFocusArea,
                      isActive() ? 1 : 0,
                      isVisible() ? 1 : 0,
                      opacity,
                      contentOpacity);
        --m_renderDebugFrames;
    }

    layout.header.y += slideOffset;
    layout.refreshButton.y += slideOffset;
    layout.searchButton.y += slideOffset;
    layout.pagePrevButton.y += slideOffset;
    layout.pageCounter.y += slideOffset;
    layout.pageNextButton.y += slideOffset;
    layout.grid.y += slideOffset;

    std::string title = isCommunityTab()
        ? i18n.tr("themeshop.community.title", "Browse Community Themes")
        : i18n.tr("themeshop.installed.title", "Installed Theme Library");
    std::string subtitle = isCommunityTab()
        ? i18n.tr("themeshop.community.subtitle", "A visual catalog, not a settings selector.")
        : i18n.tr("themeshop.installed.subtitle", "Your local themes in a gallery layout.");

    ren.drawText(title, {layout.header.x, layout.header.y + 2.f}, m_font,
                 m_theme->textPrimary.withAlpha(contentOpacity), 1.00f);
    ren.drawText(subtitle, {layout.header.x, layout.header.y + 38.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(0.92f * contentOpacity), 0.78f);

    std::string searchLabel = m_searchQuery.empty()
        ? i18n.tr("themeshop.search.button", "Search")
        : m_searchQuery;
    bool searchSelected = !m_detailOpen && m_focusArea == FocusArea::Content && m_contentFocusArea == ContentFocusArea::Header
        && (!isCommunityTab() || m_headerButtonIndex == 1);
    drawActionButtonChip(ren,
                         m_smallFont,
                         layout.searchButton,
                         searchLabel,
                         m_theme,
                         m_theme->textPrimary,
                         contentOpacity,
                         searchSelected ? 1.f : 0.f,
                         searchSelected ? 1.f : (m_searchQuery.empty() ? 0.f : 0.72f),
                         0.78f);

    std::string counterText;
    if (count > 0) {
        int selected = std::max(0, currentSelectedIndex()) + 1;
        counterText = std::to_string(selected) + " / " + std::to_string(count);
    } else if (isCommunityTab() && m_communityTransferState.isRunning()) {
        counterText = i18n.tr("themeshop.community.counter_loading", "Loading");
    } else {
        counterText = "0 / 0";
    }
    nxui::Vec2 countSize = measureTextCached(m_font, counterText);
    float countRight = (isCommunityTab() ? layout.refreshButton.x : layout.searchButton.x) - 18.f;
    ren.drawText(counterText,
                 {countRight - countSize.x * 0.90f, layout.header.y + 4.f},
                 m_font,
                 m_theme->textPrimary.withAlpha(contentOpacity),
                 0.90f);

    if (!m_packageTransferState.label().empty()) {
        nxui::Color statusFill = m_theme->panelBase.withAlpha(0.18f);
        nxui::Color statusBorder = m_theme->panelBorder.withAlpha(0.28f);
        if (m_packageTransferState.isRunning()) {
            statusFill = m_theme->cursorNormal.withAlpha(0.16f);
            statusBorder = m_theme->cursorNormal.withAlpha(0.42f);
        } else if (m_packageTransferState.isReady()) {
            statusFill = nxui::Color(0.18f, 0.50f, 0.28f, 0.18f);
            statusBorder = nxui::Color(0.34f, 0.92f, 0.52f, 0.42f);
        } else if (m_packageTransferState.hasFailed()) {
            statusFill = nxui::Color(0.52f, 0.18f, 0.16f, 0.20f);
            statusBorder = nxui::Color(0.98f, 0.34f, 0.30f, 0.42f);
        }

        nxui::Rect statusChip = {
            layout.header.x,
            layout.header.bottom() - 6.f,
            std::min(layout.grid.width * 0.64f, 430.f),
            28.f
        };
        drawChip(ren,
                 m_smallFont,
                 statusChip,
                 transferLabel(m_packageTransferState),
                 statusFill,
                 statusBorder,
                 m_theme->textPrimary,
                 contentOpacity,
                 0.70f);
        if (m_packageTransferState.isRunning()) {
            drawSpinner(ren,
                        {statusChip.x + 16.f, statusChip.y + statusChip.height * 0.5f},
                        7.f,
                        m_uiTime,
                        m_theme->cursorNormal,
                        contentOpacity);
        }
    }

    if (isCommunityTab()) {
        std::string refreshLabel = m_communityTransferState.isRunning()
            ? i18n.tr("themeshop.community.refreshing", "Refreshing")
            : i18n.tr("themeshop.community.refresh", "Refresh");
        bool refreshSelected = !m_detailOpen && m_focusArea == FocusArea::Content && m_contentFocusArea == ContentFocusArea::Header
            && m_headerButtonIndex == 0;
        drawActionButtonChip(ren,
                             m_smallFont,
                             layout.refreshButton,
                             refreshLabel,
                             m_theme,
                             m_theme->textPrimary,
                             contentOpacity,
                             refreshSelected ? 1.f : 0.f,
                             refreshSelected ? 1.f : 0.68f,
                             0.78f);
        if (m_communityTransferState.isRunning()) {
            drawSpinner(ren,
                        {layout.refreshButton.x + 18.f, layout.refreshButton.y + layout.refreshButton.height * 0.5f},
                        8.f,
                        m_uiTime,
                        m_theme->cursorNormal,
                        contentOpacity);
        }
    }

    const int pages = pageCount();
    const int page = currentPage();
    const bool canPagePrev = page > 0;
    const bool canPageNext = page + 1 < pages;
    const bool pagerFocused = !m_detailOpen
        && m_focusArea == FocusArea::Content
        && m_contentFocusArea == ContentFocusArea::Pager;
    drawActionButtonChip(ren,
                         m_smallFont,
                         layout.pagePrevButton,
                         i18n.tr("themeshop.page.prev", "Previous"),
                         m_theme,
                         canPagePrev ? m_theme->textPrimary : m_theme->textSecondary,
                         contentOpacity,
                         pagerFocused && m_pageButtonIndex == 0 ? 1.f : 0.f,
                         canPagePrev ? (pagerFocused && m_pageButtonIndex == 0 ? 1.f : 0.18f) : 0.f,
                         0.74f);
    drawChip(ren,
             m_smallFont,
             layout.pageCounter,
             std::to_string(page + 1) + " / " + std::to_string(pages),
             m_theme->panelBase.withAlpha(0.16f),
             m_theme->panelBorder.withAlpha(0.24f),
             m_theme->textPrimary,
             contentOpacity,
             0.74f);
    drawActionButtonChip(ren,
                         m_smallFont,
                         layout.pageNextButton,
                         i18n.tr("themeshop.page.next", "Next"),
                         m_theme,
                         canPageNext ? m_theme->textPrimary : m_theme->textSecondary,
                         contentOpacity,
                         pagerFocused && m_pageButtonIndex == 1 ? 1.f : 0.f,
                         canPageNext ? (pagerFocused && m_pageButtonIndex == 1 ? 1.f : 0.18f) : 0.f,
                         0.74f);

    if (pagerFocused) {
        nxui::Rect target = m_pageButtonIndex == 0 ? layout.pagePrevButton : layout.pageNextButton;
        m_focusCursor.moveTo(target.expanded(2.f), 20.f, 0.08f);
    }

    if (isCommunityTab() && !m_communityTransferState.label().empty() && m_packageTransferState.label().empty()) {
        ren.drawText(m_communityTransferState.label(),
                     {layout.header.x, layout.header.bottom() - 6.f},
                     m_smallFont,
                     m_theme->textSecondary.withAlpha(0.78f * contentOpacity),
                     0.72f);
    }

    if (isCommunityTab() && m_communityTransferState.isRunning() && count == 0) {
        nxui::Vec2 center = {layout.grid.x + layout.grid.width * 0.5f, layout.grid.y + layout.grid.height * 0.42f};
        drawSpinner(ren, center, 22.f, m_uiTime, m_theme->cursorNormal, contentOpacity);
        std::string loadingLabel = i18n.tr("themeshop.community.loading", "Loading theme catalog...");
        nxui::Vec2 size = measureTextCached(m_font, loadingLabel);
        ren.drawText(loadingLabel,
                     {center.x - size.x * 0.40f, center.y + 28.f},
                     m_font,
                     m_theme->textPrimary.withAlpha(contentOpacity),
                     0.88f);
        return;
    }

    if (count == 0) {
        nxui::Rect emptyBox = {
            layout.grid.x + layout.grid.width * 0.14f,
            layout.grid.y + layout.grid.height * 0.22f,
            layout.grid.width * 0.72f,
            170.f
        };
        ren.drawRoundedRect(emptyBox, m_theme->panelBase.withAlpha(0.16f * contentOpacity), 20.f);
        ren.drawRoundedRectOutline(emptyBox, m_theme->panelBorder.withAlpha(0.24f * contentOpacity), 20.f, 1.2f);
        bool searchActive = !m_searchQuery.empty();
        std::string emptyTitle = searchActive
            ? i18n.tr("themeshop.search.no_results", "No themes match this search.")
            : (isCommunityTab()
                ? i18n.tr("themeshop.community.catalog_empty", "No published themes are listed in the catalog.")
                : i18n.tr("themeshop.installed.empty", "No installed themes found."));
        std::string emptySubtitle = searchActive
            ? i18n.tr("themeshop.search.no_results_hint", "Press X or use Search to change or clear the filter.")
            : (isCommunityTab() && m_communityTransferState.hasFailed()
                ? i18n.tr("themeshop.community.catalog_failed", "The catalog could not be fetched. Use Refresh to try again.")
                : i18n.tr("themeshop.community.empty_hint", "Add themes to the repository and they will appear here."));

        nxui::Vec2 titleSize = measureTextCached(m_font, emptyTitle);
        ren.drawText(emptyTitle,
                     {emptyBox.x + (emptyBox.width - titleSize.x * 0.90f) * 0.5f, emptyBox.y + 40.f},
                     m_font,
                     m_theme->textPrimary.withAlpha(contentOpacity),
                     0.90f);
        nxui::Vec2 subSize = measureTextCached(m_smallFont, emptySubtitle);
        ren.drawText(emptySubtitle,
                     {emptyBox.x + (emptyBox.width - subSize.x * 0.76f) * 0.5f, emptyBox.y + 94.f},
                     m_smallFont,
                     m_theme->textSecondary.withAlpha(0.90f * contentOpacity),
                     0.76f);
        if (!m_detailOpen && m_focusArea == FocusArea::Content && m_contentFocusArea == ContentFocusArea::Header) {
            auto headerButtons = headerButtonRects(layout, isCommunityTab());
            if (!headerButtons.empty()) {
                int headerIndex = std::clamp(m_headerButtonIndex, 0, (int)headerButtons.size() - 1);
                m_focusCursor.moveTo(headerButtons[(size_t)headerIndex].expanded(2.f), 20.f, 0.08f);
            }
        }
        return;
    }

    int scrollRow = currentScrollRowRef();
    int start = scrollRow * kGridCols;
    int end = std::min(count, start + kGridCols * kVisibleRows);
    int selected = std::max(0, currentSelectedIndex());
    float reveal = std::clamp(m_tabReveal.value(), 0.f, 1.f);

    for (int globalIndex = start; globalIndex < end; ++globalIndex) {
        int localIndex = globalIndex - start;
        nxui::Rect card = gridCardRect(layout, localIndex);
        float delay = std::min(0.42f, localIndex * 0.03f);
        float localReveal = std::clamp((reveal - delay) / 0.30f, 0.f, 1.f);
        float rowOpacity = contentOpacity * localReveal;
        float rise = (1.f - localReveal) * 14.f;
        card.y += rise;

        bool cardSelected = (globalIndex == selected);
        std::string titleText;
        std::string subtitleText;
        std::string versionText;
        const nxui::Texture* previewTexture = nullptr;
        PreviewPhase previewPhase = PreviewPhase::Failed;
        bool previewRequested = false;
        bool activeTheme = false;

        if (isCommunityTab()) {
            const auto& entry = m_communityEntries[(size_t)globalIndex];
            titleText = entry.name;
            subtitleText = entry.author.empty() ? i18n.tr("themeshop.community.author_unknown", "Unknown") : entry.author;
            versionText = entry.version;
            previewTexture = communityPreviewTexture(entry);
            previewPhase = communityPreviewPhase(entry);
            previewRequested = !entry.cover.empty();
        } else {
            const auto& entry = m_themeShopEntries[(size_t)globalIndex];
            titleText = entry.name;
            std::string author = entry.author.empty()
                ? i18n.tr("themeshop.community.author_unknown", "Unknown")
                : entry.author;
            subtitleText = entry.source.empty() ? author : (author + " - " + entry.source);
            versionText = entry.version;
            activeTheme = entry.active;
            previewRequested = !entry.coverPath.empty();
            if (previewRequested) {
                primeInstalledPreview(entry.coverPath);
                previewTexture = installedPreviewTexture(entry.coverPath);
                previewPhase = installedPreviewPhase(entry.coverPath);
            }
        }

        nxui::Color cardFill = m_theme->panelBase.withAlpha((cardSelected ? 0.18f : 0.14f) * rowOpacity);
        nxui::Color cardBorder = m_theme->panelBorder.withAlpha((cardSelected ? 0.34f : 0.24f) * rowOpacity);
        ren.drawRoundedRect(card, cardFill, 22.f);
        ren.drawRoundedRectOutline(card, cardBorder, 22.f, 1.0f);

        nxui::Rect previewBounds = {card.x + 10.f, card.y + 10.f, card.width - 20.f, card.height - 76.f};
        nxui::Rect preview = fitAspectRect(previewBounds, kPreviewAspect);
        bool previewLoading = previewPhase == PreviewPhase::Loading || previewPhase == PreviewPhase::Downloaded;
        std::string previewLabel = previewRequested
            ? (previewLoading
                ? i18n.tr("themeshop.preview.loading", "Loading screenshot...")
                : i18n.tr("themeshop.preview.unavailable", "Screenshot unavailable"))
            : i18n.tr("themeshop.preview.missing", "No screenshot");
        drawThemePreview(ren,
                         m_smallFont,
                         m_theme,
                         preview,
                         previewTexture,
                         previewLabel,
                         previewLoading,
                         m_uiTime,
                         rowOpacity);

        if (!versionText.empty()) {
            float chipWidth = std::max(74.f, std::min(108.f, 32.f + measureTextCached(m_smallFont, versionText).x * 0.64f));
            nxui::Rect versionChip = {preview.right() - chipWidth - 10.f, preview.y + 10.f, chipWidth, 26.f};
            drawChip(ren,
                     m_smallFont,
                     versionChip,
                     versionText,
                     nxui::Color(0.f, 0.f, 0.f, 0.26f),
                     nxui::Color(1.f, 1.f, 1.f, 0.16f),
                     m_theme->textPrimary,
                     rowOpacity,
                     0.64f);
        }

        if (activeTheme) {
            const std::string activeLabel = i18n.tr("themeshop.installed.status_active", "Active");
            float chipWidth = std::max(82.f, std::min(114.f, 32.f + measureTextCached(m_smallFont, activeLabel).x * 0.66f));
            nxui::Rect activeChip = {preview.x + 10.f, preview.y + 10.f, chipWidth, 26.f};
            drawChip(ren,
                     m_smallFont,
                     activeChip,
                     activeLabel,
                     nxui::Color(0.10f, 0.34f, 0.14f, 0.34f),
                     nxui::Color(0.34f, 0.92f, 0.52f, 0.50f),
                     m_theme->textPrimary,
                     rowOpacity,
                     0.66f);
        }

        std::string titleFitted = ellipsize(m_font, titleText, card.width - 18.f, 0.86f);
        ren.drawText(titleFitted,
                     {card.x + 10.f, preview.bottom() + 10.f},
                     m_font,
                     m_theme->textPrimary.withAlpha(rowOpacity),
                     0.86f);
        std::string subtitleFitted = ellipsize(m_smallFont, subtitleText, card.width - 18.f, 0.76f);
        ren.drawText(subtitleFitted,
                     {card.x + 10.f, card.bottom() - 30.f},
                     m_smallFont,
                     m_theme->textSecondary.withAlpha(0.92f * rowOpacity),
                     0.76f);

        if (!m_detailOpen && m_focusArea == FocusArea::Content && m_contentFocusArea == ContentFocusArea::Grid && cardSelected) {
            m_focusCursor.moveTo(card.expanded(2.f), 22.f, 0.08f);
        }
    }

    if (!m_detailOpen && m_focusArea == FocusArea::Content && m_contentFocusArea == ContentFocusArea::Header) {
        auto headerButtons = headerButtonRects(layout, isCommunityTab());
        if (!headerButtons.empty()) {
            int headerIndex = std::clamp(m_headerButtonIndex, 0, (int)headerButtons.size() - 1);
            m_focusCursor.moveTo(headerButtons[(size_t)headerIndex].expanded(2.f), 20.f, 0.08f);
        }
    }

    int firstVisible = start + 1;
    int lastVisible = end;
    std::string footer = std::to_string(firstVisible) + "-" + std::to_string(lastVisible) + " / " + std::to_string(count);
    ren.drawText(footer,
                 {layout.grid.x, layout.grid.bottom() + 15.f},
                 m_smallFont,
                 m_theme->textSecondary.withAlpha(0.84f * contentOpacity),
                 0.76f);

    float detailT = std::clamp(m_detailSheetAnim.value(), 0.f, 1.f);
    if (!m_detailOpen && detailT <= 0.01f)
        return;

    float detailOpacity = contentOpacity * detailT;
    ren.drawRoundedRect(content, nxui::Color(0.f, 0.f, 0.f, 0.24f * detailOpacity), 26.f);
    nxui::Rect dialog = detailDialogRect(content);
    dialog = scaledRect(dialog, 0.96f + 0.04f * detailT);
    drawLiquidGlassPanel(ren, m_theme, dialog, 24.f, detailOpacity, 1.4f);

    nxui::Rect preview = detailPreviewRect(dialog);
    std::string detailTitle;
    std::string detailSubtitle;
    std::string detailInfoA;
    std::string detailInfoB;
    std::string detailInfoC;
    const nxui::Texture* detailPreviewTexture = nullptr;
    PreviewPhase detailPreviewPhase = PreviewPhase::Failed;
    bool detailPreviewRequested = false;
    int detailScreenshotTotal = 0;
    DetailPreviewControls previewControls = detailPreviewControls(preview);

    if (isCommunityTab()) {
        const auto* entry = selectedCommunityThemeEntry();
        if (!entry)
            return;
        detailTitle = entry->name;
        detailSubtitle = entry->author.empty() ? i18n.tr("themeshop.community.author_unknown", "Unknown") : entry->author;
        detailInfoA = i18n.tr("themeshop.community.version", "Version") + std::string(": ")
            + (entry->version.empty() ? i18n.tr("themeshop.community.version_unknown", "Unknown") : entry->version);
        detailInfoB = i18n.tr("themeshop.community.manifest", "Manifest") + std::string(": ")
            + (entry->manifest.empty() ? i18n.tr("themeshop.community.manifest_missing", "Not provided") : entry->manifest);
        detailScreenshotTotal = std::max(0, (int)entry->screenshots.size());
        std::string detailPreviewPath = currentDetailCommunityPreviewPath();
        detailPreviewTexture = communityPreviewTexture(detailPreviewPath);
        detailPreviewPhase = communityPreviewPhase(detailPreviewPath);
        detailPreviewRequested = !detailPreviewPath.empty();
        if (!detailPreviewRequested) {
            detailInfoC = i18n.tr("themeshop.community.cover_missing", "No screenshot has been declared for this theme yet.");
        } else if (detailPreviewTexture) {
            detailInfoC = detailScreenshotTotal > 1
                ? i18n.tr("themeshop.community.cover_present", "Use Left/Right to browse screenshots and A for fullscreen.")
                : i18n.tr("themeshop.community.cover_single", "Press A to open this screenshot in fullscreen.");
        } else if (detailPreviewPhase == PreviewPhase::Loading || detailPreviewPhase == PreviewPhase::Downloaded) {
            detailInfoC = i18n.tr("themeshop.community.cover_loading", "Loading theme screenshot...");
        } else {
            detailInfoC = i18n.tr("themeshop.community.cover_unavailable", "The screenshot could not be loaded. Placeholder shown instead.");
        }
    } else {
        const auto* entry = selectedThemeShopEntry();
        if (!entry)
            return;
        detailTitle = entry->name;
        std::string author = entry->author.empty()
            ? i18n.tr("themeshop.community.author_unknown", "Unknown")
            : entry->author;
        detailSubtitle = entry->source.empty() ? author : (author + " - " + entry->source);
        detailInfoA = i18n.tr("themeshop.installed.status", "Status") + std::string(": ")
            + (entry->active ? i18n.tr("themeshop.installed.status_active", "Active") : i18n.tr("themeshop.installed.status_available", "Available"));
        detailInfoB = i18n.tr("themeshop.installed.sound", "Bundled Sound Pack") + std::string(": ")
            + (entry->soundPreset.empty() ? i18n.tr("themeshop.installed.sound_none", "None") : entry->soundPreset);
        detailInfoC = entry->removable
            ? i18n.tr("themeshop.installed.remove_hint", "This theme can be removed from the console.")
            : i18n.tr("themeshop.installed.builtin_hint", "This is part of the built-in theme set.");
        detailPreviewRequested = !entry->coverPath.empty();
        if (detailPreviewRequested) {
            primeInstalledPreview(entry->coverPath);
            detailPreviewTexture = installedPreviewTexture(entry->coverPath);
            detailPreviewPhase = installedPreviewPhase(entry->coverPath);
        }
    }

    bool detailPreviewLoading = detailPreviewPhase == PreviewPhase::Loading || detailPreviewPhase == PreviewPhase::Downloaded;
    std::string detailPreviewLabel = detailPreviewRequested
        ? (detailPreviewLoading
            ? i18n.tr("themeshop.preview.loading", "Loading screenshot...")
            : i18n.tr("themeshop.preview.unavailable", "Screenshot unavailable"))
        : i18n.tr("themeshop.preview.missing", "No screenshot");
    drawThemePreview(ren,
                     m_smallFont,
                     m_theme,
                     preview,
                     detailPreviewTexture,
                     detailPreviewLabel,
                     detailPreviewLoading,
                     m_uiTime,
                     detailOpacity);

    if (isCommunityTab() && detailPreviewRequested) {
        auto previewButtons = detailPreviewControlRects(previewControls, preview, detailScreenshotTotal);
        if (detailScreenshotTotal > 1) {
            bool canGoPrev = m_detailScreenshotIndex > 0;
            bool canGoNext = m_detailScreenshotIndex + 1 < detailScreenshotTotal;
            drawActionButtonChip(ren,
                                 m_smallFont,
                                 previewControls.prev,
                                 i18n.tr("themeshop.preview.prev", "Prev"),
                                 m_theme,
                                 canGoPrev ? m_theme->textPrimary : m_theme->textSecondary,
                                 detailOpacity,
                                 0.f,
                                 canGoPrev ? 0.24f : 0.f,
                                 0.78f);
            drawActionButtonChip(ren,
                                 m_smallFont,
                                 previewControls.next,
                                 i18n.tr("themeshop.preview.next", "Next"),
                                 m_theme,
                                 canGoNext ? m_theme->textPrimary : m_theme->textSecondary,
                                 detailOpacity,
                                 0.f,
                                 canGoNext ? 0.24f : 0.f,
                                 0.78f);
            drawChip(ren,
                     m_smallFont,
                     previewControls.counter,
                     std::to_string(m_detailScreenshotIndex + 1) + " / " + std::to_string(detailScreenshotTotal),
                     nxui::Color(0.f, 0.f, 0.f, 0.22f),
                     m_theme->panelBorder.withAlpha(0.20f),
                     m_theme->textPrimary,
                     detailOpacity,
                     0.68f);
        }

        if (m_detailFocusArea == DetailFocusArea::Preview && !previewButtons.empty()) {
            m_focusCursor.moveTo(preview.expanded(4.f), 20.f, 0.08f);
        }
    }

    float infoBlockHeight = (isCommunityTab() && !m_packageTransferState.label().empty()) ? 272.f : 224.f;
    nxui::Rect infoBounds = {
        preview.right() + 24.f,
        dialog.y + 26.f,
        dialog.right() - preview.right() - 46.f,
        dialog.height - 122.f
    };
    nxui::Rect info = {
        infoBounds.x,
        infoBounds.y + std::max(0.f, (infoBounds.height - infoBlockHeight) * 0.5f),
        infoBounds.width,
        infoBlockHeight
    };
    ren.drawText(ellipsize(m_font, detailTitle, info.width, 1.10f),
                 {info.x, info.y + 2.f},
                 m_font,
                 m_theme->textPrimary.withAlpha(detailOpacity),
                 1.10f);
    ren.drawText(ellipsize(m_smallFont, detailSubtitle, info.width, 0.84f),
                 {info.x, info.y + 44.f},
                 m_smallFont,
                 m_theme->textSecondary.withAlpha(0.90f * detailOpacity),
                 0.84f);

    ren.drawText(detailInfoA,
                 {info.x, info.y + 108.f},
                 m_smallFont,
                 m_theme->textPrimary.withAlpha(detailOpacity),
                 0.80f);
    ren.drawText(ellipsize(m_smallFont, detailInfoB, info.width, 0.76f),
                 {info.x, info.y + 150.f},
                 m_smallFont,
                 m_theme->textSecondary.withAlpha(0.92f * detailOpacity),
                 0.76f);
    ren.drawText(ellipsize(m_smallFont, detailInfoC, info.width, 0.76f),
                 {info.x, info.y + 190.f},
                 m_smallFont,
                 m_theme->textSecondary.withAlpha(0.92f * detailOpacity),
                 0.76f);

    if (isCommunityTab() && !m_packageTransferState.label().empty()) {
        nxui::Color statusColor = m_theme->textSecondary;
        if (m_packageTransferState.isRunning())
            statusColor = m_theme->textPrimary;
        else if (m_packageTransferState.isReady())
            statusColor = nxui::Color(0.62f, 0.96f, 0.72f, 1.f);
        else if (m_packageTransferState.hasFailed())
            statusColor = nxui::Color(1.f, 0.56f, 0.52f, 1.f);

        ren.drawText(ellipsize(m_smallFont, transferLabel(m_packageTransferState), info.width - 24.f, 0.76f),
                     {info.x + 24.f, info.y + 236.f},
                     m_smallFont,
                     statusColor.withAlpha(detailOpacity),
                     0.76f);
        if (m_packageTransferState.isRunning()) {
            drawSpinner(ren,
                        {info.x + 10.f, info.y + 246.f},
                        7.f,
                        m_uiTime,
                        m_theme->cursorNormal,
                        detailOpacity);
        }
    }

    float fullscreenT = std::clamp(m_detailFullscreenAnim.value(), 0.f, 1.f);
    bool fullscreenVisible = m_detailFullscreen || fullscreenT > 0.01f;
    if (fullscreenVisible) {
        FullscreenOverlayLayout fullscreen = makeFullscreenOverlayLayout(content);
        nxui::Rect animatedPreview = lerpRect(preview, fullscreen.preview, fullscreenT);
        ren.drawRoundedRect(content, nxui::Color(0.f, 0.f, 0.f, 0.58f * detailOpacity * fullscreenT), 26.f);
        drawThemePreview(ren,
                         m_smallFont,
                         m_theme,
                         animatedPreview,
                         detailPreviewTexture,
                         detailPreviewLabel,
                         detailPreviewLoading,
                         m_uiTime,
                         detailOpacity);

        drawActionButtonChip(ren,
                             m_smallFont,
                             fullscreen.close,
                             i18n.tr("button.close", "Close"),
                             m_theme,
                             m_theme->textPrimary,
                             detailOpacity * fullscreenT,
                             0.f,
                             0.38f,
                             0.84f);

        if (detailScreenshotTotal > 1) {
            drawActionButtonChip(ren,
                                 m_smallFont,
                                 fullscreen.prev,
                                 i18n.tr("themeshop.preview.prev", "Prev"),
                                 m_theme,
                                 m_theme->textPrimary,
                                 detailOpacity * fullscreenT,
                                 0.f,
                                 0.32f,
                                 0.84f);
            drawActionButtonChip(ren,
                                 m_smallFont,
                                 fullscreen.next,
                                 i18n.tr("themeshop.preview.next", "Next"),
                                 m_theme,
                                 m_theme->textPrimary,
                                 detailOpacity * fullscreenT,
                                 0.f,
                                 0.32f,
                                 0.84f);
            drawChip(ren,
                     m_smallFont,
                     fullscreen.counter,
                     std::to_string(m_detailScreenshotIndex + 1) + " / " + std::to_string(detailScreenshotTotal),
                     nxui::Color(0.f, 0.f, 0.f, 0.50f),
                     nxui::Color(1.f, 1.f, 1.f, 0.24f),
                     m_theme->textPrimary,
                     detailOpacity * fullscreenT,
                     0.70f);
        }

        m_focusCursor.moveTo(fullscreen.close.expanded(2.f), 20.f, 0.08f);
        return;
    }

    std::vector<std::string> buttonLabels;
    if (isCommunityTab()) {
        buttonLabels.push_back(i18n.tr("themeshop.community.install", "Install"));
        buttonLabels.push_back(i18n.tr("themeshop.community.download_apply", "Download + Apply"));
    } else {
        buttonLabels.push_back(i18n.tr("themeshop.installed.apply", "Apply Theme"));
        const auto* entry = selectedThemeShopEntry();
        if (entry && entry->removable)
            buttonLabels.push_back(i18n.tr("themeshop.installed.remove", "Remove Theme"));
    }

    auto buttons = detailButtonRects(dialog, (int)buttonLabels.size());
    bool disableCommunityButtons = isCommunityTab() && m_packageTransferState.isRunning();
    for (int i = 0; i < (int)buttons.size(); ++i) {
        bool selectedButton = !disableCommunityButtons && (i == m_detailButtonIndex);
        nxui::Color textColor = disableCommunityButtons ? m_theme->textSecondary : m_theme->textPrimary;
        drawActionButtonChip(ren,
                             m_smallFont,
                             buttons[(size_t)i],
                             buttonLabels[(size_t)i],
                             m_theme,
                             textColor,
                             contentOpacity,
                             selectedButton ? 1.f : 0.f,
                             disableCommunityButtons ? 0.f : (selectedButton ? 1.f : 0.18f),
                             0.84f);
    }

    if (!(isCommunityTab() && m_detailFocusArea == DetailFocusArea::Preview && detailPreviewRequested)
        && !buttons.empty() && !disableCommunityButtons) {
        m_focusCursor.moveTo(buttons[(size_t)std::clamp(m_detailButtonIndex, 0, (int)buttons.size() - 1)].expanded(2.f),
                             20.f,
                             0.08f);
    }
}
