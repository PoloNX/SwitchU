#include "ThemeShopScreen.hpp"

#include "widgets/ActionButtonStyle.hpp"
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

// eShop-inspired shelf: roomy tiles, cover-first (art owns the card).
constexpr int kGridCols = 2;
constexpr int kVisibleRows = 2;
constexpr float kContentInsetX = 28.f;
constexpr float kContentInsetY = 16.f;
constexpr float kHeaderHeight = 64.f;
constexpr float kGridGapX = 22.f;
constexpr float kGridGapY = 22.f;
constexpr float kFooterHintHeight = 68.f;
constexpr float kCardCaptionH = 54.f;
constexpr float kPreviewAspect = 16.f / 9.f;
constexpr float kSearchButtonWidth = 208.f;
constexpr float kRefreshButtonWidth = 144.f;
constexpr float kHeaderButtonGap = 12.f;
constexpr int kEntriesPerPage = kGridCols * kVisibleRows;
constexpr float kPageButtonWidth = 124.f;
constexpr float kPageCounterWidth = 96.f;
constexpr float kActionButtonRadius = 16.f;
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
    // Product sheet: closer to full content — eShop product page, not a modal card.
    return {
        content.x + 22.f,
        content.y + 24.f,
        content.width - 44.f,
        content.height - 52.f
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
    constexpr float kBodyTopInset = 28.f;
    constexpr float kBodyBottomInset = 92.f;
    nxui::Rect bounds = {
        dialog.x + 28.f,
        dialog.y + kBodyTopInset,
        dialog.width * 0.58f,
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

void drawFrostedInsetPanel(nxui::Renderer& ren,
                          const nxui::Theme* theme,
                          const nxui::Rect& rect,
                          float radius,
                          float opacity,
                          float borderBoost = 1.f) {
    if (!theme || opacity <= 0.01f)
        return;

    ren.drawRoundedRect(rect,
                        theme->panelBase.withAlpha(0.96f * opacity), radius);
    ren.drawRoundedRectOutline(
        rect.shrunk(1.f),
        theme->panelHighlight.withAlpha(0.20f * opacity),
        std::max(0.f, radius - 1.f), 1.f);
    ren.drawRoundedRectOutline(
        rect,
        theme->panelBorder.withAlpha(0.56f * borderBoost * opacity),
        radius, 1.4f);
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

void drawSpectrumBadge(nxui::Renderer& ren, const nxui::Rect& rect, float opacity) {
    if (rect.width <= 1.f || rect.height <= 1.f)
        return;
    const float radius = std::min(4.f, rect.height * 0.35f);
    ren.drawRoundedRect(rect, nxui::Color(0.f, 0.f, 0.f, 0.28f * opacity), radius);
    // Four solid swatches - clearer than a fine rainbow gradient.
    static const nxui::Color kSwatches[4] = {
        nxui::Color(0.92f, 0.28f, 0.32f, 1.f),
        nxui::Color(0.98f, 0.78f, 0.18f, 1.f),
        nxui::Color(0.22f, 0.78f, 0.48f, 1.f),
        nxui::Color(0.28f, 0.52f, 0.96f, 1.f),
    };
    constexpr int kBlocks = 4;
    const float inset = 2.5f;
    const float gap = 1.5f;
    const float innerW = rect.width - inset * 2.f - gap * static_cast<float>(kBlocks - 1);
    const float blockW = innerW / static_cast<float>(kBlocks);
    const float blockH = rect.height - inset * 2.f;
    const float blockR = std::min(2.5f, blockH * 0.35f);
    for (int i = 0; i < kBlocks; ++i) {
        nxui::Rect block = {
            rect.x + inset + (blockW + gap) * static_cast<float>(i),
            rect.y + inset,
            std::max(1.f, blockW),
            blockH
        };
        ren.drawRoundedRect(block, kSwatches[i].withAlpha(opacity), blockR);
    }
    ren.drawRoundedRectOutline(rect, nxui::Color(1.f, 1.f, 1.f, 0.18f * opacity), radius, 1.f);
}

void drawHueSpectrumTrack(nxui::Renderer& ren, const nxui::Rect& track, float hue01, float opacity,
                          bool focused) {
    constexpr int kBands = 24;
    const float bandW = track.width / static_cast<float>(kBands);
    for (int i = 0; i < kBands; ++i) {
        const float hue = static_cast<float>(i) / static_cast<float>(kBands);
        ren.drawRect({track.x + bandW * static_cast<float>(i), track.y, bandW + 0.6f, track.height},
                     nxui::Color::fromHSL(hue, 0.80f, 0.52f, opacity));
    }
    ren.drawRoundedRectOutline(track,
                               focused ? nxui::Color(1.f, 1.f, 1.f, 0.55f * opacity)
                                       : nxui::Color(0.f, 0.f, 0.f, 0.28f * opacity),
                               track.height * 0.5f,
                               focused ? 2.f : 1.f);
    const float knobX = track.x + std::clamp(hue01, 0.f, 1.f) * track.width;
    const float knobR = track.height * 0.72f;
    const nxui::Vec2 knobC = {knobX, track.y + track.height * 0.5f};
    ren.drawCircle(knobC, knobR + 2.6f, nxui::Color(0.f, 0.f, 0.f, 0.35f * opacity));
    ren.drawCircle(knobC, knobR + 1.4f, nxui::Color(1.f, 1.f, 1.f, 0.90f * opacity));
    ren.drawCircle(knobC, knobR,
                   nxui::Color::fromHSL(std::clamp(hue01, 0.f, 1.f), 0.78f, 0.55f, opacity));
}

void drawTintTogglePill(nxui::Renderer& ren,
                        nxui::Font* font,
                        const nxui::Rect& pill,
                        bool enabled,
                        float opacity,
                        bool focused) {
    const nxui::Color pillBg = enabled
        ? nxui::Color(0.20f, 0.82f, 0.45f, 0.90f * opacity)
        : nxui::Color(0.35f, 0.38f, 0.45f, 0.75f * opacity);
    ren.drawRoundedRect(pill, pillBg, pill.height * 0.5f);
    if (focused)
        ren.drawRoundedRectOutline(pill, nxui::Color(1.f, 1.f, 1.f, 0.55f * opacity),
                                   pill.height * 0.5f, 2.f);
    const float knobX = enabled ? (pill.x + pill.width - pill.height * 0.5f)
                                : (pill.x + pill.height * 0.5f);
    ren.drawCircle({knobX, pill.y + pill.height * 0.5f}, pill.height * 0.40f,
                   nxui::Color(1.f, 1.f, 1.f, opacity));
    if (font) {
        // Label drawn by caller beside the pill.
        (void)font;
    }
}

struct IconTintLayout {
    nxui::Rect tintLabelRow;
    nxui::Rect tintPill;
    nxui::Rect flattenLabelRow;
    nxui::Rect flattenPill;
    nxui::Rect grayLabelRow;
    nxui::Rect grayPill;
    nxui::Rect hueLabel;
    nxui::Rect hueTrack;
    nxui::Rect satLabel;
    nxui::Rect satTrack;
    nxui::Rect lightLabel;
    nxui::Rect lightTrack;
};

// Toggle stack + HSL under the pack preview (side column).
IconTintLayout makeIconTintLayout(const nxui::Rect& band) {
    IconTintLayout layout;
    constexpr float kRowH = 34.f;
    constexpr float kPillW = 56.f;
    constexpr float kPillH = 28.f;
    constexpr float kToggleGap = 6.f;
    auto placeToggle = [&](nxui::Rect& labelRow, nxui::Rect& pill, float y) {
        labelRow = {band.x, y, band.width, kRowH};
        pill = {band.x + band.width - kPillW - 2.f, y + 3.f, kPillW, kPillH};
    };
    float y = band.y;
    placeToggle(layout.tintLabelRow, layout.tintPill, y);
    y = layout.tintLabelRow.bottom() + kToggleGap;
    placeToggle(layout.flattenLabelRow, layout.flattenPill, y);
    y = layout.flattenLabelRow.bottom() + kToggleGap;
    placeToggle(layout.grayLabelRow, layout.grayPill, y);

    constexpr float kLabelH = 20.f;
    constexpr float kTrackH = 16.f;
    constexpr float kLabelGap = 5.f;
    constexpr float kSectionGap = 18.f;
    y = layout.grayLabelRow.bottom() + 12.f;
    layout.hueLabel = {band.x, y, band.width, kLabelH};
    y = layout.hueLabel.bottom() + kLabelGap;
    layout.hueTrack = {band.x, y, band.width, kTrackH};
    y = layout.hueTrack.bottom() + kSectionGap;
    layout.satLabel = {band.x, y, band.width, kLabelH};
    y = layout.satLabel.bottom() + kLabelGap;
    layout.satTrack = {band.x, y, band.width, kTrackH};
    y = layout.satTrack.bottom() + kSectionGap;
    layout.lightLabel = {band.x, y, band.width, kLabelH};
    y = layout.lightLabel.bottom() + kLabelGap;
    layout.lightTrack = {band.x, y, band.width, kTrackH};
    return layout;
}

// Large left preview (same footprint as theme detail) + roomy tint column on the right.
nxui::Rect colourIconPreviewBounds(const nxui::Rect& dialog) {
    constexpr float kBodyTopInset = 78.f; // room for title above the studio preview
    constexpr float kBodyBottomInset = 92.f;
    nxui::Rect bounds = {
        dialog.x + 28.f,
        dialog.y + kBodyTopInset,
        dialog.width * 0.48f,
        dialog.height - kBodyTopInset - kBodyBottomInset
    };
    return fitAspectRect(bounds, kPreviewAspect);
}

nxui::Rect colourIconTintBand(const nxui::Rect& dialog, const nxui::Rect& preview, bool tintExpanded) {
    (void)tintExpanded;
    return {
        preview.right() + 24.f,
        dialog.y + 118.f,
        std::max(160.f, dialog.right() - preview.right() - 46.f),
        std::max(80.f, dialog.height - 118.f - 96.f)
    };
}

void drawGradientValueTrack(nxui::Renderer& ren,
                            const nxui::Rect& track,
                            float value01,
                            float opacity,
                            bool focused,
                            const std::function<nxui::Color(float)>& colorAt) {
    constexpr int kBands = 24;
    const float bandW = track.width / static_cast<float>(kBands);
    for (int i = 0; i < kBands; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(kBands);
        ren.drawRect({track.x + bandW * static_cast<float>(i), track.y, bandW + 0.6f, track.height},
                     colorAt(t).withAlpha(opacity));
    }
    ren.drawRoundedRectOutline(track,
                               focused ? nxui::Color(1.f, 1.f, 1.f, 0.55f * opacity)
                                       : nxui::Color(0.f, 0.f, 0.f, 0.28f * opacity),
                               track.height * 0.5f,
                               focused ? 2.f : 1.f);
    const float knobX = track.x + std::clamp(value01, 0.f, 1.f) * track.width;
    const float knobR = track.height * 0.72f;
    const nxui::Vec2 knobC = {knobX, track.y + track.height * 0.5f};
    ren.drawCircle(knobC, knobR + 2.6f, nxui::Color(0.f, 0.f, 0.f, 0.35f * opacity));
    ren.drawCircle(knobC, knobR + 1.4f, nxui::Color(1.f, 1.f, 1.f, 0.90f * opacity));
    ren.drawCircle(knobC, knobR, colorAt(std::clamp(value01, 0.f, 1.f)).withAlpha(opacity));
}

void drawThemePreview(nxui::Renderer& ren,
                      nxui::Font* smallFont,
                      const nxui::Theme* theme,
                      const nxui::Rect& rect,
                      const nxui::Texture* texture,
                      const std::string& placeholderLabel,
                      bool loading,
                      float time,
                      float opacity,
                      bool adaptiveTint = false,
                      bool iconPackGlass = false,
                      const nxui::Texture* contrastMask = nullptr,
                      const nxui::Color* customTint = nullptr,
                      bool shelfCover = false) {
    if (!theme)
        return;

    if (texture && texture->valid()) {
        nxui::Rect drawRect = rect;
        float corner = 14.f;
        if (shelfCover) {
            // eShop shelf / product hero: cover is the plane — no inset frame.
            ren.drawRoundedRect(rect, nxui::Color(0.f, 0.f, 0.f, 0.22f * opacity), 16.f);
            drawRect = rect;
            corner = 16.f;
        } else if (iconPackGlass) {
            // Little glass well inside the pack card - theme-coloured (no hard
            // #1c1e25 plate). Glyphs keep alpha and sit on this glass.
            const bool dark = theme->mode == nxui::ThemeMode::Dark;
            const nxui::Color glassBase = theme->panelBase.withAlpha((dark ? 0.58f : 0.78f) * opacity);
            const nxui::Color glassBorder = theme->panelBorder.withAlpha(0.36f * opacity);
            const nxui::Color glassHighlight = theme->panelHighlight.withAlpha(0.16f * opacity);
            ren.drawFrostedInset(rect, glassBase, glassBorder, glassHighlight, 14.f, opacity);
            drawRect = {rect.x + 10.f, rect.y + 10.f, rect.width - 20.f, rect.height - 20.f};
            corner = 10.f;
        } else {
            // Theme screenshots keep a framed inset well.
            ren.drawRoundedRect(rect, theme->panelBase.withAlpha(0.14f * opacity), 14.f);
            ren.drawRoundedRectOutline(rect, theme->panelBorder.withAlpha(0.18f * opacity), 14.f, 1.f);
            drawRect = {rect.x + 4.f, rect.y + 4.f, rect.width - 8.f, rect.height - 8.f};
            ren.drawRoundedRect(drawRect, nxui::Color(0.f, 0.f, 0.f, 0.14f * opacity), 12.f);
            corner = 12.f;
        }
        const nxui::Texture* drawTex = texture;
        nxui::Color tint = adaptiveTint
            ? theme->textPrimary.withAlpha(theme->textPrimary.a * opacity)
            : nxui::Color(1.f, 1.f, 1.f, opacity);
        if (!adaptiveTint && customTint && contrastMask && contrastMask->valid()) {
            drawTex = contrastMask;
            tint = customTint->withAlpha(customTint->a * opacity);
        }
        ren.drawTextureRounded(drawTex,
                               fitTextureRect(drawRect, drawTex),
                               corner,
                               tint);
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

bool ThemeShopScreen::isIconsTab() const {
    return m_tabIndex == 2;
}

bool ThemeShopScreen::isInstalledTab() const {
    return m_tabIndex == 0;
}

int ThemeShopScreen::currentEntryCount() const {
    if (isCommunityTab())
        return (int)m_communityEntries.size();
    if (isIconsTab())
        return (int)m_iconPackEntries.size();
    return (int)m_themeShopEntries.size();
}

int ThemeShopScreen::currentSelectedIndex() const {
    if (isCommunityTab()) {
        for (int i = 0; i < (int)m_communityEntries.size(); ++i) {
            if (m_communityEntries[i].id == m_communitySelectedId)
                return i;
        }
        return m_communityEntries.empty() ? -1 : 0;
    }
    if (isIconsTab()) {
        for (int i = 0; i < (int)m_iconPackEntries.size(); ++i) {
            if (m_iconPackEntries[i].id == m_iconPackSelectedId)
                return i;
        }
        return m_iconPackEntries.empty() ? -1 : 0;
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
    else if (isIconsTab())
        m_iconPackSelectedId = m_iconPackEntries[(size_t)idx].id;
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
    int scrollRow = isCommunityTab() ? m_communityScrollRow
                  : (isIconsTab() ? m_iconPackScrollRow : m_installedScrollRow);
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
    if (isCommunityTab())
        return m_communityScrollRow;
    if (isIconsTab())
        return m_iconPackScrollRow;
    return m_installedScrollRow;
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
    // Always land on Apply. Icons: Up → tint, Left → preview. Installed /
    // Community: Up → preview. Down from preview/tint returns here.
    m_detailFocusArea = DetailFocusArea::Buttons;
    m_focusArea = FocusArea::Content;

    beginIconPackTintDraft();

    if (isCommunityTab())
        primeCommunityPreview(currentDetailCommunityPreviewPath());
}

void ThemeShopScreen::closeDetail() {
    m_detailOpen = false;
    m_detailSheetAnim.set(0.f, 0.16f, nxui::Easing::outCubic);
    m_detailFullscreen = false;
    m_detailFullscreenAnim.setImmediate(0.f);
    // Avoid lerping the blue ring from the detail Apply button through the
    // bottom-left card before settling on the real selection.
    m_snapGridFocus = true;
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
    discardIconPackTintDraft();
}

int ThemeShopScreen::detailButtonCount() const {
    if (isCommunityTab())
        return selectedCommunityThemeEntry() ? 2 : 0;

    const auto* entry = selectedThemeShopEntry();
    if (!entry)
        return 0;
    if (isIconsTab()) {
        if (!entry->installed)
            return 2; // Install / Download + Apply
        return entry->removable ? 2 : 1; // Apply [/ Remove]
    }
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

    if (isIconsTab()) {
        if (!entry->installed) {
            // Catalog pack: install it, optionally applying right away.
            closeDetail();
            StringCb& cb = (buttonIndex == 1) ? m_iconPackDownloadApplyCb : m_iconPackDownloadCb;
            if (cb)
                cb(themeId);
            return;
        }
        // Commit draft tint before closing so Apply publishes HSL + HOME chrome.
        if (buttonIndex == 0)
            commitDraftIconPackTint();
        closeDetail();
        if (buttonIndex == 0) {
            if (m_iconPackApplyCb)
                m_iconPackApplyCb(themeId);
        } else if (buttonIndex == 1 && removable && m_iconPackDeleteCb) {
            m_iconPackDeleteCb(themeId);
        }
        return;
    }

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

    // ZL/ZR (and L/R) flip pages like HOME - Installed / Community / Icons.
    if (m_input && m_showing && !m_detailOpen && pageCount() > 1) {
        if (m_input->isDown(nxui::Button::ZL) || m_input->isDown(nxui::Button::L))
            stepPage(-1);
        else if (m_input->isDown(nxui::Button::ZR) || m_input->isDown(nxui::Button::R))
            stepPage(1);
    }

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
    if (isInstalledTab() || isIconsTab())
        syncFinishedInstalledPreviewLoads();

    int headerButtonCount = isCommunityTab() ? 2 : 1;
    m_headerButtonIndex = std::clamp(m_headerButtonIndex, 0, std::max(0, headerButtonCount - 1));

    if (m_lastCustomTabIndex != m_tabIndex) {
        m_lastCustomTabIndex = m_tabIndex;
        m_contentFocusArea = ContentFocusArea::Grid;
        closeDetail();
        // Don't clearCommunityPreviewCache() here - nuking every texture waitIdle's
        // and is the major hitch when cycling past Community. Soft-trim drips below.
    }

    if (currentEntryCount() <= 0) {
        if (m_focusArea == FocusArea::Content)
            m_focusArea = FocusArea::Tabs;
        currentScrollRowRef() = 0;
        closeDetail();
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
            // Trim only when scroll changes - not every detail open/close
            // (open/close was soft-trim thrashing covers <-> screenshots).
            const bool pageChanged = m_lastPreviewPrimeKey.empty()
                || primeKey.find('|') == std::string::npos
                || ([&]() {
                       auto field = [](const std::string& s, int idx) -> std::string {
                           std::size_t start = 0;
                           for (int i = 0; i < idx; ++i) {
                               std::size_t p = s.find('|', start);
                               if (p == std::string::npos) return {};
                               start = p + 1;
                           }
                           std::size_t p = s.find('|', start);
                           return s.substr(start, p == std::string::npos ? std::string::npos : p - start);
                       };
                       return field(primeKey, 2) != field(m_lastPreviewPrimeKey, 2);
                   })();
            m_lastPreviewPrimeKey = std::move(primeKey);
            if (pageChanged)
                trimCommunityPreviewCache();
        }
        // Always re-attempt primes so Idle slots can start when a download slot frees.
        primeVisibleCommunityPreviews();
    } else {
        // Away from Community: drip-release GPU textures so Installed/Icons can upload
        // without a one-frame waitIdle storm.
        if (!m_communityPreviewCache.empty())
            trimCommunityPreviewCache();
        if (isInstalledTab() || isIconsTab())
            trimInstalledPreviewCache();
    }

    updateTintSliderAnalog(dt);
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
            refreshCommunityCatalog(true);
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

        if (m_detailFocusArea == DetailFocusArea::Preview
            && (isCommunityTab() ? detailScreenshotCount() > 0
                                 : (selectedThemeShopEntry() && !selectedThemeShopEntry()->coverPath.empty()))) {
            m_detailFullscreen = true;
            m_detailFullscreenAnim.set(1.f, 0.18f, nxui::Easing::outCubic);
            if (m_activateSfxCb) m_activateSfxCb();
            return true;
        }

        if (m_detailFocusArea == DetailFocusArea::TintToggle) {
            setIconCustomTintEnabled(!selectedIconCustomTint());
            if (selectedIconCustomTint()) {
                m_detailFocusArea = selectedIconShowsHueSat()
                    ? DetailFocusArea::TintHue : DetailFocusArea::TintLight;
            }
            if (m_activateSfxCb) m_activateSfxCb();
            return true;
        }
        if (m_detailFocusArea == DetailFocusArea::FlattenToggle) {
            setIconFlattenEnabled(!selectedIconPackTint().flatten);
            if (m_activateSfxCb) m_activateSfxCb();
            return true;
        }
        if (m_detailFocusArea == DetailFocusArea::GrayscaleToggle) {
            setIconGrayscaleEnabled(!selectedIconPackTint().grayscale);
            if (m_activateSfxCb) m_activateSfxCb();
            return true;
        }

        if (m_detailFocusArea == DetailFocusArea::TintHue
            || m_detailFocusArea == DetailFocusArea::TintSat
            || m_detailFocusArea == DetailFocusArea::TintLight) {
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
        const bool canFocusPreview = isCommunityTab()
            ? detailScreenshotCount() > 0
            : (selectedThemeShopEntry() && !selectedThemeShopEntry()->coverPath.empty());
        if (showSelectedIconTintControls()) {
            // Apply Up → Light → Sat → Hue → Grayscale → Flatten → Custom tint → Preview.
            // (HS skipped when Grayscale hides them; Light only when Custom tint is on.)
            if (m_detailFocusArea == DetailFocusArea::Buttons) {
                if (selectedIconCustomTint())
                    m_detailFocusArea = DetailFocusArea::TintLight;
                else
                    m_detailFocusArea = DetailFocusArea::GrayscaleToggle;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::TintLight) {
                m_detailFocusArea = selectedIconShowsHueSat()
                    ? DetailFocusArea::TintSat : DetailFocusArea::GrayscaleToggle;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::TintSat) {
                m_detailFocusArea = DetailFocusArea::TintHue;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::TintHue) {
                m_detailFocusArea = DetailFocusArea::GrayscaleToggle;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::GrayscaleToggle) {
                m_detailFocusArea = DetailFocusArea::FlattenToggle;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::FlattenToggle) {
                m_detailFocusArea = DetailFocusArea::TintToggle;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::TintToggle && canFocusPreview) {
                m_detailFocusArea = DetailFocusArea::Preview;
                if (m_navSfxCb) m_navSfxCb();
            }
        } else if (canFocusPreview && m_detailFocusArea == DetailFocusArea::Buttons) {
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
        // Preview Down → Apply. Toggle stack: tint → flatten → grayscale → Apply.
        if (m_detailFocusArea == DetailFocusArea::Preview) {
            if (detailButtonCount() > 0) {
                m_detailFocusArea = DetailFocusArea::Buttons;
                if (m_navSfxCb) m_navSfxCb();
                return true;
            }
        }
        if (showSelectedIconTintControls()) {
            if (m_detailFocusArea == DetailFocusArea::TintToggle) {
                m_detailFocusArea = DetailFocusArea::FlattenToggle;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::FlattenToggle) {
                m_detailFocusArea = DetailFocusArea::GrayscaleToggle;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::GrayscaleToggle) {
                // Toggles → sliders (when Custom tint is on) → Apply.
                if (selectedIconCustomTint()) {
                    m_detailFocusArea = selectedIconShowsHueSat()
                        ? DetailFocusArea::TintHue : DetailFocusArea::TintLight;
                    if (m_navSfxCb) m_navSfxCb();
                } else if (detailButtonCount() > 0) {
                    m_detailFocusArea = DetailFocusArea::Buttons;
                    if (m_navSfxCb) m_navSfxCb();
                }
            } else if (m_detailFocusArea == DetailFocusArea::TintHue) {
                m_detailFocusArea = selectedIconShowsHueSat()
                    ? DetailFocusArea::TintSat : DetailFocusArea::TintLight;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::TintSat) {
                m_detailFocusArea = DetailFocusArea::TintLight;
                if (m_navSfxCb) m_navSfxCb();
            } else if (m_detailFocusArea == DetailFocusArea::TintLight) {
                m_detailFocusArea = DetailFocusArea::Buttons;
                if (m_navSfxCb) m_navSfxCb();
            }
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

        if (m_detailFocusArea == DetailFocusArea::TintToggle
            || m_detailFocusArea == DetailFocusArea::FlattenToggle
            || m_detailFocusArea == DetailFocusArea::GrayscaleToggle) {
            // Prefer returning to the preview; A toggles on/off.
            const bool canPreview = selectedThemeShopEntry()
                && !selectedThemeShopEntry()->coverPath.empty();
            if (canPreview)
                m_detailFocusArea = DetailFocusArea::Preview;
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        if (m_detailFocusArea == DetailFocusArea::TintHue) {
            nudgeIconTintHue(-1.f / 36.f);
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        if (m_detailFocusArea == DetailFocusArea::TintSat) {
            nudgeIconTintSat(-1.f / 20.f);
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        if (m_detailFocusArea == DetailFocusArea::TintLight) {
            nudgeIconTintLight(-1.f / 20.f);
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }

        // Icons: Left from Apply → pack preview.
        if (m_detailFocusArea == DetailFocusArea::Buttons
            && m_detailButtonIndex == 0
            && isIconsTab()
            && selectedThemeShopEntry()
            && !selectedThemeShopEntry()->coverPath.empty()) {
            m_detailFocusArea = DetailFocusArea::Preview;
            if (m_navSfxCb) m_navSfxCb();
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
        // Icons: Preview Right -> Custom tint (when the pack supports it).
        if (isIconsTab() && m_detailFocusArea == DetailFocusArea::Preview
            && showSelectedIconTintControls()) {
            m_detailFocusArea = DetailFocusArea::TintToggle;
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        // Icons: Apply Right stays on buttons; colour packs also allow
        // Apply → Tint when only one primary action is focused.
        if (isIconsTab() && m_detailFocusArea == DetailFocusArea::Buttons
            && m_detailButtonIndex + 1 >= detailButtonCount()
            && showSelectedIconTintControls()) {
            m_detailFocusArea = DetailFocusArea::TintToggle;
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }

        if (m_detailFocusArea == DetailFocusArea::TintToggle) {
            setIconCustomTintEnabled(true);
            m_detailFocusArea = selectedIconShowsHueSat()
                ? DetailFocusArea::TintHue : DetailFocusArea::TintLight;
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        if (m_detailFocusArea == DetailFocusArea::TintHue) {
            nudgeIconTintHue(1.f / 36.f);
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        if (m_detailFocusArea == DetailFocusArea::TintSat) {
            nudgeIconTintSat(1.f / 20.f);
            if (m_navSfxCb) m_navSfxCb();
            return true;
        }
        if (m_detailFocusArea == DetailFocusArea::TintLight) {
            nudgeIconTintLight(1.f / 20.f);
            if (m_navSfxCb) m_navSfxCb();
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

    int scrollRow = isCommunityTab() ? m_communityScrollRow
                  : (isIconsTab() ? m_iconPackScrollRow : m_installedScrollRow);
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
    auto installedHasCover = [&]() {
        const auto* entry = selectedThemeShopEntry();
        return entry && !entry->coverPath.empty();
    };
    auto detailPreviewInteractCount = [&]() {
        if (isCommunityTab())
            return detailScreenshotCount();
        return installedHasCover() ? 1 : 0;
    };
    auto detailPreviewHitIndex = [&](float x, float y) {
        if (!m_detailOpen)
            return -1;
        const int count = detailPreviewInteractCount();
        if (count <= 0)
            return -1;

        nxui::Rect dialog = detailDialogRect(content);
        nxui::Rect previewRect = isIconsTab() && showSelectedIconTintControls()
            ? colourIconPreviewBounds(dialog)
            : detailPreviewRect(dialog);
        DetailPreviewControls controls = detailPreviewControls(previewRect);
        auto rects = detailPreviewControlRects(controls, previewRect, count);
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

            if (showSelectedIconTintControls()) {
                nxui::Rect previewRect = colourIconPreviewBounds(dialog);
                IconTintLayout tintLayout = makeIconTintLayout(
                    colourIconTintBand(dialog, previewRect, selectedIconCustomTint()));
                if (tintLayout.tintPill.expanded(8.f).contains(x, y)) {
                    m_themeTouchTarget = ThemeTouchTarget::DetailTintToggle;
                    return true;
                }
                if (tintLayout.flattenPill.expanded(8.f).contains(x, y)) {
                    m_themeTouchTarget = ThemeTouchTarget::DetailFlattenToggle;
                    return true;
                }
                if (tintLayout.grayPill.expanded(8.f).contains(x, y)) {
                    m_themeTouchTarget = ThemeTouchTarget::DetailGrayscaleToggle;
                    return true;
                }
                if (selectedIconCustomTint()) {
                    auto hitTrack = [&](const nxui::Rect& track, ThemeTouchTarget target,
                                       auto setter) {
                        if (!track.expanded(8.f).contains(x, y))
                            return false;
                        m_themeTouchTarget = target;
                        const float t = std::clamp((x - track.x) / std::max(1.f, track.width), 0.f, 1.f);
                        setter(t);
                        return true;
                    };
                    if (selectedIconShowsHueSat()) {
                        if (hitTrack(tintLayout.hueTrack, ThemeTouchTarget::DetailTintHue,
                                     [this](float t) { setIconTintHue(t); }))
                            return true;
                        if (hitTrack(tintLayout.satTrack, ThemeTouchTarget::DetailTintSat,
                                     [this](float t) { setIconTintSat(t); }))
                            return true;
                        if (hitTrack(tintLayout.lightTrack, ThemeTouchTarget::DetailTintLight,
                                     [this](float t) { setIconTintLight(t); }))
                            return true;
                    } else if (hitTrack(tintLayout.hueTrack, ThemeTouchTarget::DetailTintLight,
                                        [this](float t) { setIconTintLight(t); })) {
                        return true;
                    }
                }
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
        if ((m_themeTouchTarget == ThemeTouchTarget::DetailTintHue
             || m_themeTouchTarget == ThemeTouchTarget::DetailTintSat
             || m_themeTouchTarget == ThemeTouchTarget::DetailTintLight)
            && showSelectedIconTintControls()) {
            nxui::Rect dialog = detailDialogRect(content);
            nxui::Rect previewRect = colourIconPreviewBounds(dialog);
            IconTintLayout tintLayout = makeIconTintLayout(
                colourIconTintBand(dialog, previewRect, selectedIconCustomTint()));
            const nxui::Rect* track = &tintLayout.hueTrack;
            if (m_themeTouchTarget == ThemeTouchTarget::DetailTintSat)
                track = &tintLayout.satTrack;
            else if (m_themeTouchTarget == ThemeTouchTarget::DetailTintLight)
                track = &tintLayout.lightTrack;
            const float t = std::clamp((input.touchX() - track->x)
                                           / std::max(1.f, track->width),
                                       0.f, 1.f);
            if (m_themeTouchTarget == ThemeTouchTarget::DetailTintHue)
                setIconTintHue(t);
            else if (m_themeTouchTarget == ThemeTouchTarget::DetailTintSat)
                setIconTintSat(t);
            else
                setIconTintLight(t);
            return true;
        }
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
                    refreshCommunityCatalog(true);
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
                    const int shots = detailPreviewInteractCount();
                    if (hitIndex == 0) {
                        if (shots > 1) {
                            int before = m_detailScreenshotIndex;
                            stepDetailScreenshot(-1);
                            if (before != m_detailScreenshotIndex && m_navSfxCb) m_navSfxCb();
                        } else if (shots == 1) {
                            m_detailFocusArea = DetailFocusArea::Preview;
                            m_detailFullscreen = true;
                            m_detailFullscreenAnim.set(1.f, 0.18f, nxui::Easing::outCubic);
                            if (m_activateSfxCb) m_activateSfxCb();
                        }
                    } else if ((hitIndex == 1 && shots > 1)
                            || (hitIndex == 0 && shots == 1)) {
                        m_detailFocusArea = DetailFocusArea::Preview;
                        m_detailFullscreen = true;
                        m_detailFullscreenAnim.set(1.f, 0.18f, nxui::Easing::outCubic);
                        if (m_activateSfxCb) m_activateSfxCb();
                    } else if (hitIndex == 2 && shots > 1) {
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
            case ThemeTouchTarget::DetailTintToggle: {
                if (showSelectedIconTintControls()) {
                    setIconCustomTintEnabled(!selectedIconCustomTint());
                    m_detailFocusArea = selectedIconCustomTint()
                        ? (selectedIconShowsHueSat() ? DetailFocusArea::TintHue
                                                     : DetailFocusArea::TintLight)
                        : DetailFocusArea::TintToggle;
                    if (m_activateSfxCb) m_activateSfxCb();
                }
                return true;
            }
            case ThemeTouchTarget::DetailFlattenToggle: {
                if (showSelectedIconTintControls()) {
                    setIconFlattenEnabled(!selectedIconPackTint().flatten);
                    m_detailFocusArea = DetailFocusArea::FlattenToggle;
                    if (m_activateSfxCb) m_activateSfxCb();
                }
                return true;
            }
            case ThemeTouchTarget::DetailGrayscaleToggle: {
                if (showSelectedIconTintControls()) {
                    setIconGrayscaleEnabled(!selectedIconPackTint().grayscale);
                    m_detailFocusArea = DetailFocusArea::GrayscaleToggle;
                    if (m_activateSfxCb) m_activateSfxCb();
                }
                return true;
            }
            case ThemeTouchTarget::DetailTintHue:
                m_detailFocusArea = DetailFocusArea::TintHue;
                return true;
            case ThemeTouchTarget::DetailTintSat:
                m_detailFocusArea = DetailFocusArea::TintSat;
                return true;
            case ThemeTouchTarget::DetailTintLight:
                m_detailFocusArea = DetailFocusArea::TintLight;
                return true;
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

    // eShop shelf headers: short store names, one quiet line of context.
    std::string title;
    std::string subtitle;
    if (isCommunityTab()) {
        title = i18n.tr("themeshop.community.title", "Community");
        subtitle = i18n.tr("themeshop.community.subtitle", "Themes from the catalog.");
    } else if (isIconsTab()) {
        title = i18n.tr("themeshop.icons.title", "Icons");
        subtitle = i18n.tr("themeshop.icons.subtitle", "Sidebar icon packs.");
    } else {
        title = i18n.tr("themeshop.installed.title", "Themes");
        subtitle = i18n.tr("themeshop.installed.subtitle", "Installed on this console.");
    }

    ren.drawText(title, {layout.header.x, layout.header.y + 0.f}, m_font,
                 m_theme->textPrimary.withAlpha(contentOpacity), 1.08f);
    ren.drawText(subtitle, {layout.header.x, layout.header.y + 36.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(0.82f * contentOpacity), 0.74f);

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
                : (isIconsTab()
                    ? i18n.tr("themeshop.icons.empty", "No icon packs found.")
                    : i18n.tr("themeshop.installed.empty", "No installed themes found.")));
        std::string emptySubtitle = searchActive
            ? i18n.tr("themeshop.search.no_results_hint", "Press X or use Search to change or clear the filter.")
            : (isCommunityTab() && m_communityTransferState.hasFailed()
                ? i18n.tr("themeshop.community.catalog_failed", "The catalog could not be fetched. Use Refresh to try again.")
                : (isIconsTab()
                    ? i18n.tr("themeshop.icons.empty_hint", "Add folders under icons/ to create packs.")
                    : i18n.tr("themeshop.community.empty_hint", "Add themes to the repository and they will appear here.")));

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
    const float gridDetailT = std::clamp(m_detailSheetAnim.value(), 0.f, 1.f);
    const bool gridFocusLive = !m_detailOpen && gridDetailT <= 0.01f;

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
        bool previewAdaptive = false;
        bool activeTheme = false;

        bool showIconsBadge = false;
        if (isCommunityTab()) {
            const auto& entry = m_communityEntries[(size_t)globalIndex];
            titleText = entry.name;
            subtitleText = entry.author.empty() ? i18n.tr("themeshop.community.author_unknown", "Unknown") : entry.author;
            versionText = entry.version;
            previewTexture = communityPreviewTexture(entry);
            previewPhase = communityPreviewPhase(entry);
            previewRequested = !entry.cover.empty();
            showIconsBadge = entry.hasIcons;
        } else {
            const auto& entry = isIconsTab()
                ? m_iconPackEntries[(size_t)globalIndex]
                : m_themeShopEntries[(size_t)globalIndex];
            titleText = entry.name;
            // Shelf caption: author only (source/version live as chips or detail).
            subtitleText = entry.author.empty()
                ? i18n.tr("themeshop.community.author_unknown", "Unknown")
                : entry.author;
            versionText = entry.version;
            activeTheme = entry.active;
            previewRequested = !entry.coverPath.empty() || !entry.coverUrl.empty();
            showIconsBadge = !isIconsTab() && entry.hasIcons;
            if (previewRequested && !entry.coverUrl.empty()) {
                // Catalog pack that is not on the console yet: fetch the cover.
                primeCommunityPreview(entry.coverUrl);
                previewTexture = communityPreviewTexture(entry.coverUrl);
                previewPhase = communityPreviewPhase(entry.coverUrl);
            } else if (previewRequested) {
                const std::string packId = isIconsTab() ? entry.id : std::string{};
                primeInstalledPreview(entry.coverPath, isIconsTab(), packId, nullptr, 384); // grid
                previewTexture = installedPreviewTexture(entry.coverPath, isIconsTab(), packId);
                previewPhase = installedPreviewPhase(entry.coverPath, isIconsTab(), packId);
                previewAdaptive = isIconsTab()
                    && installedPreviewAdaptive(entry.coverPath, true, packId);
            }
        }

        // Shelf tile: soft plate + cover-forward art (caption lives under the cover).
        nxui::Color cardFill = m_theme->panelBase.withAlpha((cardSelected ? 0.16f : 0.10f) * rowOpacity);
        nxui::Color cardBorder = m_theme->panelBorder.withAlpha((cardSelected ? 0.28f : 0.14f) * rowOpacity);
        ren.drawRoundedRect(card, cardFill, 20.f);
        ren.drawRoundedRectOutline(card, cardBorder, 20.f, 1.0f);

        nxui::Rect previewBounds = {
            card.x + 8.f,
            card.y + 8.f,
            card.width - 16.f,
            card.height - kCardCaptionH - 10.f
        };
        nxui::Rect preview = isIconsTab()
            ? previewBounds
            : fitAspectRect(previewBounds, kPreviewAspect);
        // Idle means "queued but not started yet" (download cap) - still loading, not unavailable.
        bool previewLoading = previewPhase == PreviewPhase::Idle
            || previewPhase == PreviewPhase::Loading
            || previewPhase == PreviewPhase::Downloaded;
        std::string previewLabel = previewRequested
            ? (previewLoading
                ? (isIconsTab()
                    ? i18n.tr("themeshop.icons.preview_loading", "Loading preview...")
                    : i18n.tr("themeshop.preview.loading", "Loading screenshot..."))
                : (isIconsTab()
                    ? i18n.tr("themeshop.icons.preview_unavailable", "Preview unavailable")
                    : i18n.tr("themeshop.preview.unavailable", "Screenshot unavailable")))
            : (isIconsTab()
                ? i18n.tr("themeshop.icons.preview_missing", "No preview")
                : i18n.tr("themeshop.preview.missing", "No screenshot"));
        drawThemePreview(ren,
                         m_smallFont,
                         m_theme,
                         preview,
                         previewTexture,
                         previewLabel,
                         previewLoading,
                         m_uiTime,
                         rowOpacity,
                         previewAdaptive,
                         isIconsTab(),
                         nullptr,
                         nullptr,
                         !isIconsTab());

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

                const bool showSpectrumBadge = isIconsTab() && !isCommunityTab()
            && iconPackTintFor(m_iconPackEntries[(size_t)globalIndex].id).enabled;
        constexpr float kSpectrumBadgeW = 40.f;
        constexpr float kSpectrumBadgeH = 14.f;
        constexpr float kSpectrumBadgeGap = 8.f;
        const std::string iconsBadgeLabel = i18n.tr("themeshop.badge.icons", "Icons");
        const float iconsBadgeW = showIconsBadge
            ? std::max(58.f, std::min(86.f, 28.f + measureTextCached(m_smallFont, iconsBadgeLabel).x * 0.62f))
            : 0.f;
        constexpr float kIconsBadgeH = 22.f;
        constexpr float kIconsBadgeGap = 8.f;
        float titleReserve = 0.f;
        if (showSpectrumBadge)
            titleReserve += kSpectrumBadgeW + kSpectrumBadgeGap;
        if (showIconsBadge)
            titleReserve += iconsBadgeW + kIconsBadgeGap;
        const float titleMaxW = titleReserve > 0.f
            ? std::max(40.f, card.width - 18.f - titleReserve)
            : (card.width - 18.f);
        std::string titleFitted = ellipsize(m_font, titleText, titleMaxW, 0.82f);
        const float titleY = card.bottom() - kCardCaptionH + 6.f;
        ren.drawText(titleFitted,
                     {card.x + 12.f, titleY},
                     m_font,
                     m_theme->textPrimary.withAlpha(rowOpacity),
                     0.82f);
        float badgeX = card.x + 12.f + measureTextCached(m_font, titleFitted).x * 0.82f + kSpectrumBadgeGap;
        const float titleH = measureTextCached(m_font, titleFitted).y * 0.82f;
        if (showSpectrumBadge) {
            nxui::Rect spectrum = {
                badgeX,
                titleY + std::max(0.f, (titleH - kSpectrumBadgeH) * 0.5f),
                kSpectrumBadgeW,
                kSpectrumBadgeH
            };
            drawSpectrumBadge(ren, spectrum, rowOpacity);
            badgeX = spectrum.right() + kIconsBadgeGap;
        }
        if (showIconsBadge) {
            nxui::Rect iconsChip = {
                showSpectrumBadge ? badgeX
                                  : (card.x + 12.f
                                     + measureTextCached(m_font, titleFitted).x * 0.82f
                                     + kIconsBadgeGap),
                titleY + std::max(0.f, (titleH - kIconsBadgeH) * 0.5f),
                iconsBadgeW,
                kIconsBadgeH
            };
            drawChip(ren,
                     m_smallFont,
                     iconsChip,
                     iconsBadgeLabel,
                     nxui::Color(0.12f, 0.28f, 0.48f, 0.38f),
                     nxui::Color(0.45f, 0.72f, 1.f, 0.48f),
                     m_theme->textPrimary,
                     rowOpacity,
                     0.62f);
        }
        // Author only — eShop shelves don't dump source/version in the caption.
        std::string subtitleFitted = ellipsize(m_smallFont, subtitleText, card.width - 24.f, 0.70f);
        ren.drawText(subtitleFitted,
                     {card.x + 12.f, card.bottom() - 22.f},
                     m_smallFont,
                     m_theme->textSecondary.withAlpha(0.78f * rowOpacity),
                     0.70f);

        // Hold grid focus until the detail close anim finishes, then snap once.
        // Mid-close updates let the fading sheet steal the ring back to Apply.
        if (gridFocusLive
            && m_focusArea == FocusArea::Content && m_contentFocusArea == ContentFocusArea::Grid
            && cardSelected) {
            const float dur = m_snapGridFocus ? 0.f : 0.08f;
            m_focusCursor.moveTo(card.expanded(2.f), 22.f, dur);
            if (m_snapGridFocus)
                m_snapGridFocus = false;
        }
    }

    if (gridFocusLive
        && m_focusArea == FocusArea::Content && m_contentFocusArea == ContentFocusArea::Header) {
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
    // Soft store scrim — dim the shelf, lift the product sheet.
    ren.drawRoundedRect(content, nxui::Color(0.f, 0.f, 0.f, 0.52f * detailOpacity), 26.f);
    nxui::Rect dialog = detailDialogRect(content);
    dialog = scaledRect(dialog, 0.97f + 0.03f * detailT);
    drawFrostedInsetPanel(ren, m_theme, dialog, 26.f, detailOpacity, 1.2f);

    // Tint only applies to packs that live on the console.
    const auto* tintEntry = isIconsTab() ? selectedThemeShopEntry() : nullptr;
    const bool showIconTintControls = isIconsTab() && showSelectedIconTintControls()
        && tintEntry && tintEntry->installed;
    nxui::Rect preview = showIconTintControls
        ? colourIconPreviewBounds(dialog)
        : detailPreviewRect(dialog);
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
        const std::string statusText = entry->active
            ? i18n.tr("themeshop.installed.status_active", "Active")
            : (entry->installed
                ? i18n.tr("themeshop.installed.status_available", "Available")
                : i18n.tr("themeshop.icons.status_online", "Available online"));
        detailInfoA = i18n.tr("themeshop.installed.status", "Status") + std::string(": ")
            + statusText;
        if (isIconsTab()) {
            detailInfoB = entry->description.empty()
                ? i18n.tr("themeshop.icons.fallback", "Missing icons fall back to Default.")
                : entry->description;
            if (!entry->installed) {
                detailInfoC = i18n.tr("themeshop.icons.download_hint", "Install to download this pack to the console.");
            } else if (entry->removable) {
                detailInfoC = i18n.tr("themeshop.icons.remove_hint", "Downloaded pack: it can be removed from the console.");
            } else if (!entry->coverPath.empty()) {
                detailInfoC = i18n.tr("themeshop.icons.cover_single", "Press A for a closer look, then Apply to switch icons.");
            } else {
                detailInfoC = i18n.tr("themeshop.icons.apply_hint", "Apply to switch sidebar icons immediately.");
            }
        } else {
            detailInfoB = i18n.tr("themeshop.installed.sound", "Bundled Sound Pack") + std::string(": ")
                + (entry->soundPreset.empty() ? i18n.tr("themeshop.installed.sound_none", "None") : entry->soundPreset);
            detailInfoC = entry->removable
                ? i18n.tr("themeshop.installed.remove_hint", "This theme can be removed from the console.")
                : i18n.tr("themeshop.installed.builtin_hint", "This is part of the built-in theme set.");
            if (!entry->coverPath.empty())
                detailInfoC = i18n.tr("themeshop.installed.cover_single", "Press A to open this screenshot in fullscreen.");
        }
        detailPreviewRequested = !entry->coverPath.empty() || !entry->coverUrl.empty();
        if (detailPreviewRequested && !entry->coverUrl.empty()) {
            primeCommunityPreview(entry->coverUrl);
            detailPreviewTexture = communityPreviewTexture(entry->coverUrl);
            detailPreviewPhase = communityPreviewPhase(entry->coverUrl);
        } else if (detailPreviewRequested) {
            const std::string packId = isIconsTab() ? entry->id : std::string{};
            // Detail always uses the draft HSL slot while tint controls are up —
            // including when Custom/Flatten/Gray are all off, so the preview can
            // return to the pack's original look instead of the last Apply.
            // Icons grid keeps the committed preview (hslOverride=null).
            switchu::icons::HslAdjust draftHsl;
            const switchu::icons::HslAdjust* hslOverride = nullptr;
            if (isIconsTab() && showSelectedIconTintControls()) {
                draftHsl = hslAdjustFromTint(selectedIconPackTint());
                hslOverride = &draftHsl;
            }
            primeInstalledPreview(entry->coverPath, isIconsTab(), packId, hslOverride, 384); // match grid - avoid waitIdle reupload
            detailPreviewTexture =
                installedPreviewTexture(entry->coverPath, isIconsTab(), packId, hslOverride);
            detailPreviewPhase =
                installedPreviewPhase(entry->coverPath, isIconsTab(), packId, hslOverride);
        }
    }

    const bool detailPreviewAdaptive = [&]() {
        if (!isIconsTab() || !detailPreviewRequested || !selectedThemeShopEntry())
            return false;
        const std::string packId = selectedThemeShopEntry()->id;
        switchu::icons::HslAdjust draftHsl;
        const switchu::icons::HslAdjust* hslOverride = nullptr;
        if (showSelectedIconTintControls()) {
            draftHsl = hslAdjustFromTint(selectedIconPackTint());
            hslOverride = &draftHsl;
        }
        return installedPreviewAdaptive(selectedThemeShopEntry()->coverPath, true, packId,
                                        hslOverride);
    }();

    // Never flash the spinner over a still-valid draft frame (Idle / Loading /
    // Downloaded all keep the previous texture during HSL scrub).
    bool detailPreviewLoading = !detailPreviewTexture
        && (detailPreviewPhase == PreviewPhase::Idle
            || detailPreviewPhase == PreviewPhase::Loading
            || detailPreviewPhase == PreviewPhase::Downloaded);
    std::string detailPreviewLabel = detailPreviewRequested
        ? (detailPreviewLoading
            ? i18n.tr("themeshop.preview.loading", "Loading screenshot...")
            : i18n.tr("themeshop.preview.unavailable", "Screenshot unavailable"))
        : i18n.tr("themeshop.preview.missing", "No screenshot");
    // Custom tint is baked into the preview texture via HSL adjust on load.
    drawThemePreview(ren,
                     m_smallFont,
                     m_theme,
                     preview,
                     detailPreviewTexture,
                     detailPreviewLabel,
                     detailPreviewLoading,
                     m_uiTime,
                     detailOpacity,
                     detailPreviewAdaptive,
                     isIconsTab(),
                     nullptr,
                     nullptr,
                     !isIconsTab());

    if (detailPreviewRequested) {
        auto previewButtons = detailPreviewControlRects(previewControls, preview, detailScreenshotTotal);
        if (isCommunityTab() && detailScreenshotTotal > 1) {
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

        if (m_detailOpen && m_detailFocusArea == DetailFocusArea::Preview) {
            m_focusCursor.moveTo(preview.expanded(4.f), 20.f, 0.08f);
        }
    }

    if (showIconTintControls) {
        // Header above the preview; tint controls stacked underneath.
        const float headerX = dialog.x + 36.f;
        const float headerW = dialog.width - 72.f;
        ren.drawText(ellipsize(m_font, detailTitle, headerW * 0.62f, 1.10f),
                     {headerX, dialog.y + 22.f},
                     m_font,
                     m_theme->textPrimary.withAlpha(detailOpacity),
                     1.10f);
        ren.drawText(ellipsize(m_smallFont, detailSubtitle, headerW * 0.55f, 0.84f),
                     {headerX, dialog.y + 56.f},
                     m_smallFont,
                     m_theme->textSecondary.withAlpha(0.90f * detailOpacity),
                     0.84f);
        ren.drawText(ellipsize(m_smallFont, detailInfoA, headerW * 0.40f, 0.78f),
                     {headerX + headerW * 0.58f, dialog.y + 28.f},
                     m_smallFont,
                     m_theme->textPrimary.withAlpha(detailOpacity),
                     0.78f);

        IconTintLayout tintLayout = makeIconTintLayout(
            colourIconTintBand(dialog, preview, selectedIconCustomTint()));
        auto drawToggleRow = [&](const nxui::Rect& labelRow, const nxui::Rect& pill,
                                 const std::string& label, bool enabled, bool focused) {
            ren.drawText(ellipsize(m_smallFont, label, pill.x - labelRow.x - 12.f, 0.86f),
                         {labelRow.x, labelRow.y + 6.f},
                         m_smallFont,
                         m_theme->textPrimary.withAlpha(detailOpacity),
                         0.86f);
            drawTintTogglePill(ren, m_smallFont, pill, enabled, detailOpacity, focused);
        };
        drawToggleRow(tintLayout.tintLabelRow, tintLayout.tintPill,
                      i18n.tr("themeshop.icons.custom_tint", "Custom tint"),
                      selectedIconCustomTint(),
                      m_detailFocusArea == DetailFocusArea::TintToggle);
        drawToggleRow(tintLayout.flattenLabelRow, tintLayout.flattenPill,
                      i18n.tr("themeshop.icons.flatten", "Flatten"),
                      selectedIconPackTint().flatten,
                      m_detailFocusArea == DetailFocusArea::FlattenToggle);
        drawToggleRow(tintLayout.grayLabelRow, tintLayout.grayPill,
                      i18n.tr("themeshop.icons.grayscale", "Grayscale"),
                      selectedIconPackTint().grayscale,
                      m_detailFocusArea == DetailFocusArea::GrayscaleToggle);

        if (selectedIconCustomTint()) {
            constexpr float kLabelScale = 0.80f;
            nxui::Rect lightLabel = tintLayout.lightLabel;
            nxui::Rect lightTrack = tintLayout.lightTrack;
            if (selectedIconShowsHueSat()) {
                ren.drawText(i18n.tr("themeshop.icons.tint_hue", "Hue"),
                             {tintLayout.hueLabel.x, tintLayout.hueLabel.y},
                             m_smallFont,
                             m_theme->textPrimary.withAlpha(0.92f * detailOpacity),
                             kLabelScale);
                drawHueSpectrumTrack(ren,
                                     tintLayout.hueTrack,
                                     selectedIconPackTint().hue,
                                     detailOpacity,
                                     m_detailFocusArea == DetailFocusArea::TintHue);
                ren.drawText(i18n.tr("themeshop.icons.tint_saturation", "Saturation"),
                             {tintLayout.satLabel.x, tintLayout.satLabel.y},
                             m_smallFont,
                             m_theme->textPrimary.withAlpha(0.92f * detailOpacity),
                             kLabelScale);
                drawGradientValueTrack(ren, tintLayout.satTrack, selectedIconPackTint().sat, detailOpacity,
                                       m_detailFocusArea == DetailFocusArea::TintSat,
                                       [](float t) {
                                           return nxui::Color::fromHSL(0.58f, t, 0.55f, 1.f);
                                       });
            } else {
                // Pack Lightness under the toggles when H/S are hidden.
                lightLabel = tintLayout.hueLabel;
                lightTrack = tintLayout.hueTrack;
            }
            ren.drawText(i18n.tr("themeshop.icons.tint_lightness", "Lightness"),
                         {lightLabel.x, lightLabel.y},
                         m_smallFont,
                         m_theme->textPrimary.withAlpha(0.92f * detailOpacity),
                         kLabelScale);
            drawGradientValueTrack(ren, lightTrack, selectedIconPackTint().light, detailOpacity,
                                   m_detailFocusArea == DetailFocusArea::TintLight,
                                   [](float t) {
                                       return nxui::Color::fromHSL(0.58f, 0.35f, t, 1.f);
                                   });
            if (m_detailOpen && m_detailFocusArea == DetailFocusArea::TintLight)
                m_focusCursor.moveTo(lightTrack.expanded(8.f), 18.f, 0.08f);
        }

        if (m_detailOpen && m_detailFocusArea == DetailFocusArea::TintToggle) {
            m_focusCursor.moveTo(tintLayout.tintPill.expanded(6.f), 18.f, 0.08f);
        } else if (m_detailOpen && m_detailFocusArea == DetailFocusArea::FlattenToggle) {
            m_focusCursor.moveTo(tintLayout.flattenPill.expanded(6.f), 18.f, 0.08f);
        } else if (m_detailOpen && m_detailFocusArea == DetailFocusArea::GrayscaleToggle) {
            m_focusCursor.moveTo(tintLayout.grayPill.expanded(6.f), 18.f, 0.08f);
        } else if (m_detailOpen && m_detailFocusArea == DetailFocusArea::TintHue
                   && selectedIconCustomTint() && selectedIconShowsHueSat()) {
            m_focusCursor.moveTo(tintLayout.hueTrack.expanded(8.f), 18.f, 0.08f);
        } else if (m_detailOpen && m_detailFocusArea == DetailFocusArea::TintSat
                   && selectedIconCustomTint() && selectedIconShowsHueSat()) {
            m_focusCursor.moveTo(tintLayout.satTrack.expanded(8.f), 18.f, 0.08f);
        } else if ((m_detailFocusArea == DetailFocusArea::TintHue
                    || m_detailFocusArea == DetailFocusArea::TintSat)
                   && !(selectedIconCustomTint() && selectedIconShowsHueSat())) {
            m_detailFocusArea = selectedIconCustomTint()
                ? DetailFocusArea::TintLight : DetailFocusArea::TintToggle;
        } else if (m_detailFocusArea == DetailFocusArea::TintLight
                   && !selectedIconCustomTint()) {
            m_detailFocusArea = DetailFocusArea::TintToggle;
        }
    } else {
        // Product copy: title, author, one soft line — no settings-style dump.
        nxui::Rect infoBounds = {
            preview.right() + 28.f,
            dialog.y + 36.f,
            dialog.right() - preview.right() - 52.f,
            dialog.height - 130.f
        };
        ren.drawText(ellipsize(m_font, detailTitle, infoBounds.width, 1.22f),
                     {infoBounds.x, infoBounds.y + 4.f},
                     m_font,
                     m_theme->textPrimary.withAlpha(detailOpacity),
                     1.22f);
        ren.drawText(ellipsize(m_smallFont, detailSubtitle, infoBounds.width, 0.86f),
                     {infoBounds.x, infoBounds.y + 52.f},
                     m_smallFont,
                     m_theme->textSecondary.withAlpha(0.88f * detailOpacity),
                     0.86f);

        // Soft status chip (Active / Available) instead of "Status: …" prose.
        if (!detailInfoA.empty()) {
            const std::string statusChipLabel = [&]() -> std::string {
                if (detailInfoA.find(':') != std::string::npos)
                    return detailInfoA.substr(detailInfoA.find(':') + 1);
                return detailInfoA;
            }();
            std::string trimmed = statusChipLabel;
            while (!trimmed.empty() && trimmed.front() == ' ')
                trimmed.erase(trimmed.begin());
            const float chipW = std::max(96.f,
                std::min(infoBounds.width * 0.72f,
                         36.f + measureTextCached(m_smallFont, trimmed).x * 0.72f));
            nxui::Rect statusChip = {infoBounds.x, infoBounds.y + 96.f, chipW, 30.f};
            drawChip(ren,
                     m_smallFont,
                     statusChip,
                     trimmed,
                     m_theme->panelBase.withAlpha(0.22f),
                     m_theme->panelBorder.withAlpha(0.28f),
                     m_theme->textPrimary,
                     detailOpacity,
                     0.72f);
        }

        ren.drawText(ellipsize(m_smallFont, detailInfoC, infoBounds.width, 0.76f),
                     {infoBounds.x, infoBounds.y + 146.f},
                     m_smallFont,
                     m_theme->textSecondary.withAlpha(0.80f * detailOpacity),
                     0.76f);
    }

    if (isCommunityTab() && !m_packageTransferState.label().empty()) {
        nxui::Rect infoBounds = {
            preview.right() + 28.f,
            dialog.y + 36.f,
            dialog.right() - preview.right() - 52.f,
            dialog.height - 130.f
        };
        nxui::Color statusColor = m_theme->textSecondary;
        if (m_packageTransferState.isRunning())
            statusColor = m_theme->textPrimary;
        else if (m_packageTransferState.isReady())
            statusColor = nxui::Color(0.62f, 0.96f, 0.72f, 1.f);
        else if (m_packageTransferState.hasFailed())
            statusColor = nxui::Color(1.f, 0.56f, 0.52f, 1.f);

        ren.drawText(ellipsize(m_smallFont, transferLabel(m_packageTransferState), infoBounds.width, 0.76f),
                     {infoBounds.x, infoBounds.bottom() - 36.f},
                     m_smallFont,
                     statusColor.withAlpha(detailOpacity),
                     0.76f);
        if (m_packageTransferState.isRunning()) {
            drawSpinner(ren,
                        {infoBounds.x + infoBounds.width - 18.f, infoBounds.bottom() - 24.f},
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
                         detailOpacity,
                         detailPreviewAdaptive,
                         isIconsTab(),
                         nullptr,
                         nullptr,
                         !isIconsTab());

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

        if (m_detailOpen)
            m_focusCursor.moveTo(fullscreen.close.expanded(2.f), 20.f, 0.08f);
        return;
    }

    std::vector<std::string> buttonLabels;
    if (isCommunityTab()) {
        buttonLabels.push_back(i18n.tr("themeshop.community.install", "Install"));
        buttonLabels.push_back(i18n.tr("themeshop.community.download_apply", "Download + Apply"));
    } else if (isIconsTab()) {
        const auto* entry = selectedThemeShopEntry();
        if (entry && !entry->installed) {
            buttonLabels.push_back(i18n.tr("themeshop.community.install", "Install"));
            buttonLabels.push_back(i18n.tr("themeshop.community.download_apply", "Download + Apply"));
        } else {
            buttonLabels.push_back(i18n.tr("themeshop.icons.apply", "Apply Icon Pack"));
            if (entry && entry->removable)
                buttonLabels.push_back(i18n.tr("themeshop.icons.remove", "Remove Icon Pack"));
        }
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

    const bool tintFocus = m_detailFocusArea == DetailFocusArea::TintToggle
        || m_detailFocusArea == DetailFocusArea::FlattenToggle
        || m_detailFocusArea == DetailFocusArea::GrayscaleToggle
        || m_detailFocusArea == DetailFocusArea::TintHue
        || m_detailFocusArea == DetailFocusArea::TintSat
        || m_detailFocusArea == DetailFocusArea::TintLight;
    // Only drive the ring while detail is actually open - during the close
    // anim the grid snap (above) owns focus so we don't jump Apply -> card.
    if (m_detailOpen
        && !(m_detailFocusArea == DetailFocusArea::Preview && detailPreviewRequested)
        && !tintFocus
        && !buttons.empty() && !disableCommunityButtons) {
        m_focusCursor.moveTo(buttons[(size_t)std::clamp(m_detailButtonIndex, 0, (int)buttons.size() - 1)].expanded(2.f),
                             20.f,
                             0.08f);
    }
}
