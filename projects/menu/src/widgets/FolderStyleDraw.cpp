#include "FolderStyleDraw.hpp"
#include "core/FolderStore.hpp"
#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <algorithm>
#include <array>

namespace switchu::folders {
namespace {

struct ThemeShell {
    nxui::Color fill;
    nxui::Color outline;
    nxui::Color title;
    bool light = false;
};

ThemeShell themeShell(nxui::ThemeMode mode, float opacity) {
    ThemeShell out;
    out.light = mode == nxui::ThemeMode::Light;
    out.fill = out.light
        ? nxui::Color(0.94f, 0.96f, 0.97f, 0.96f * opacity)
        : nxui::Color(0.10f, 0.13f, 0.17f, 0.92f * opacity);
    out.outline = out.light
        ? nxui::Color(0.12f, 0.16f, 0.20f, 0.18f * opacity)
        : nxui::Color::white().withAlpha(0.28f * opacity);
    out.title = out.light
        ? nxui::Color(0.08f, 0.12f, 0.16f, 0.96f * opacity)
        : nxui::Color::white().withAlpha(0.98f * opacity);
    return out;
}

void drawHaloTitle(nxui::Renderer& ren, nxui::Font* font, const std::string& title,
                   const nxui::Vec2& pos, float textScale, float scale, float opacity) {
    const float halo = std::max(1.f, 1.5f * scale);
    const nxui::Color shadow(0.05f, 0.16f, 0.26f, 0.34f * opacity);
    const nxui::Vec2 offsets[8] = {
        {-halo, 0.f}, {halo, 0.f}, {0.f, -halo}, {0.f, halo},
        {-halo, -halo}, {halo, -halo}, {-halo, halo}, {halo, halo}};
    for (const nxui::Vec2& off : offsets)
        ren.drawText(title, {pos.x + off.x, pos.y + off.y}, font, shadow, textScale);
    ren.drawText(title, {pos.x, pos.y + halo * 0.7f}, font,
                 nxui::Color(0.04f, 0.14f, 0.24f, 0.30f * opacity), textScale);
    ren.drawText(title, pos, font, nxui::Color::white().withAlpha(0.98f * opacity), textScale);
}

struct FittedTitle {
    float width = 0.f;
    float height = 0.f;
    float scale = 1.f;
};

FittedTitle measureTitle(nxui::Font* font, const std::string& title, float room,
                         float preferredScale, float minScale) {
    FittedTitle out;
    const nxui::Vec2 measured = font->measure(title);
    out.scale = preferredScale;
    if (measured.x > 0.f)
        out.scale = std::min(out.scale, room / measured.x);
    out.scale = std::max(minScale, out.scale);
    out.width = measured.x * out.scale;
    out.height = measured.y * out.scale;
    return out;
}

void drawNineGrid(nxui::Renderer& ren, const nxui::Rect& shell, const nxui::Color& accent,
                  float scale, float opacity) {
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
        ren.drawRoundedRect({cellRect.x, cellRect.y + 1.8f * scale,
                             cellRect.width, cellRect.height},
                            nxui::Color(0.05f, 0.08f, 0.10f, 0.16f * opacity),
                            cell * 0.20f);
        const float variation = 0.92f + 0.035f * static_cast<float>((i + row) % 3);
        nxui::Color cellColor(
            std::min(1.f, accent.r * variation),
            std::min(1.f, accent.g * variation),
            std::min(1.f, accent.b * variation),
            0.94f * opacity);
        ren.drawRoundedRect(cellRect, cellColor, cell * 0.20f);
        ren.drawRoundedRect({cellRect.x + cell * 0.10f,
                             cellRect.y + cell * 0.08f,
                             cellRect.width * 0.80f,
                             std::max(1.f, cellRect.height * 0.13f)},
                            nxui::Color::white().withAlpha(0.18f * opacity),
                            cell * 0.08f);
    }
}

void drawEmptyGlyph(nxui::Renderer& ren, const nxui::Rect& shell, const nxui::Color& accent,
                    float opacity) {
    const float cell = std::min(shell.width, shell.height) * 0.16f;
    const float gap = cell * 0.22f;
    const float gridSize = cell * 2.f + gap;
    const float gridX = shell.x + (shell.width - gridSize) * 0.5f;
    const float gridY = shell.y + (shell.height - gridSize) * 0.52f;
    for (int i = 0; i < 4; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        const nxui::Rect cellRect{gridX + col * (cell + gap),
                                  gridY + row * (cell + gap), cell, cell};
        ren.drawRoundedRect(cellRect, accent.withAlpha(0.55f * opacity), cell * 0.22f);
    }
}

void drawTopCap(nxui::Renderer& ren, const nxui::Rect& inner, float innerRadius,
                const nxui::Color& accent, float sliceH, float opacity) {
    ren.pushClipRect({inner.x, inner.y, inner.width, sliceH});
    ren.drawRoundedRect(inner, accent.withAlpha(0.78f * opacity), innerRadius);
    ren.popClipRect();
    const float highlightH = std::max(2.f, sliceH * 0.45f);
    ren.pushClipRect({inner.x, inner.y, inner.width, highlightH});
    ren.drawRoundedRect(inner, nxui::Color::white().withAlpha(0.22f * opacity), innerRadius);
    ren.popClipRect();
}

void drawBottomBand(nxui::Renderer& ren, const nxui::Rect& inner, float innerRadius,
                    const nxui::Color& accent, float sliceH, float opacity) {
    const float y = inner.bottom() - sliceH;
    ren.pushClipRect({inner.x, y, inner.width, sliceH});
    ren.drawRoundedRect(inner, accent.withAlpha(0.82f * opacity), innerRadius);
    ren.popClipRect();
    const float highlightH = std::max(2.f, sliceH * 0.40f);
    ren.pushClipRect({inner.x, y, inner.width, highlightH});
    ren.drawRoundedRect(inner, nxui::Color::white().withAlpha(0.18f * opacity), innerRadius);
    ren.popClipRect();
}

void drawFocus(nxui::Renderer& ren, const nxui::Rect& shell, float shellRadius,
               const nxui::Color& accent, float scale, float opacity) {
    ren.drawRoundedRectOutline(shell.expanded(2.f * scale),
                               accent.withAlpha(0.50f * opacity),
                               shellRadius + 2.f * scale, 2.f * scale);
}

void drawCoverPlaceholder(nxui::Renderer& ren, const nxui::Rect& photo,
                          const nxui::Color& accent, float radius, float opacity) {
    ren.drawRoundedRect(photo, nxui::Color(0.08f, 0.10f, 0.13f, 0.88f * opacity), radius);
    ren.drawRoundedRect(photo, accent.withAlpha(0.28f * opacity), radius);
    const nxui::Rect screen = photo.shrunk(photo.width * 0.14f);
    ren.drawRoundedRect(screen, nxui::Color(0.18f, 0.22f, 0.28f, 0.90f * opacity),
                        std::max(4.f, radius * 0.45f));
}

void drawCoverPhoto(nxui::Renderer& ren, const nxui::Rect& photo, float radius,
                    const nxui::Texture* cover, const nxui::Color& accent,
                    float opacity, bool posterAspect) {
    if (!cover) {
        drawCoverPlaceholder(ren, photo, accent, radius, opacity);
        return;
    }
    // Square icons in a tall frame: contain. Near-matching aspects: fill.
    const float texW = static_cast<float>(std::max(1, cover->width()));
    const float texH = static_cast<float>(std::max(1, cover->height()));
    const float texAspect = texW / texH;
    const float photoAspect = photo.width / std::max(1.f, photo.height);
    const bool contain = posterAspect && std::abs(texAspect - photoAspect) > 0.18f;
    if (!contain) {
        ren.drawTextureRounded(cover, photo, radius,
                               nxui::Color::white().withAlpha(opacity));
        return;
    }
    ren.drawRoundedRect(photo, nxui::Color(0.06f, 0.08f, 0.11f, 0.90f * opacity), radius);
    float drawW = photo.width;
    float drawH = drawW / texAspect;
    if (drawH > photo.height) {
        drawH = photo.height;
        drawW = drawH * texAspect;
    }
    const nxui::Rect fitted{
        photo.x + (photo.width - drawW) * 0.5f,
        photo.y + (photo.height - drawH) * 0.5f,
        drawW, drawH};
    ren.drawTextureRounded(cover, fitted, std::max(4.f, radius * 0.7f),
                           nxui::Color::white().withAlpha(opacity));
}

nxui::Rect coverPhotoRect(const nxui::Rect& host, float topPad, float bottomPad,
                          bool posterAspect) {
    const float availH = std::max(18.f, host.height - topPad - bottomPad);
    if (posterAspect) {
        // Inset custom folder poster so style chrome stays visible.
        float photoW = host.width * 0.90f;
        float photoH = photoW * 1.5f;
        if (photoH > availH) {
            photoH = availH;
            photoW = std::min(host.width * 0.90f, photoH / 1.5f);
        }
        photoW = std::max(18.f, photoW);
        photoH = std::max(18.f, photoH);
        return {host.x + (host.width - photoW) * 0.5f, host.y + topPad, photoW, photoH};
    }
    float side = std::min(host.width, host.height) * 0.62f;
    side = std::min(side, availH);
    side = std::max(18.f, side);
    return {host.x + (host.width - side) * 0.5f, host.y + topPad, side, side};
}

void drawPeekGrid(nxui::Renderer& ren, const nxui::Rect& host,
                  const std::array<nxui::Texture*, kFolderPeekMax>& peeks,
                  int peekCount, const nxui::Color& accent, float scale,
                  float opacity) {
    if (peekCount <= 0 || host.width < 8.f || host.height < 8.f)
        return;
    constexpr int kCols = 2;
    constexpr int kRows = 3;
    const float s = scale;
    // Equal margin on all sides; size cells to the remaining box.
    const float margin = std::max(6.f, std::min(host.width, host.height) * 0.08f);
    const float gap = std::max(3.f, margin * 0.45f);
    const float innerW = std::max(8.f, host.width - margin * 2.f);
    const float innerH = std::max(8.f, host.height - margin * 2.f);
    const float cell = std::min((innerW - gap * (kCols - 1)) / static_cast<float>(kCols),
                                (innerH - gap * (kRows - 1)) / static_cast<float>(kRows));
    const float gridW = cell * kCols + gap * (kCols - 1);
    const float gridH = cell * kRows + gap * (kRows - 1);
    const float originX = host.x + (host.width - gridW) * 0.5f;
    const float originY = host.y + (host.height - gridH) * 0.5f;
    const float rad = std::max(4.f, cell * 0.16f);
    for (int i = 0; i < kCols * kRows; ++i) {
        const int col = i % kCols;
        const int row = i / kCols;
        const nxui::Rect cellRect{
            originX + col * (cell + gap),
            originY + row * (cell + gap),
            cell, cell};
        ren.drawRoundedRect({cellRect.x + 1.2f * s, cellRect.y + 1.6f * s,
                             cellRect.width, cellRect.height},
                            nxui::Color(0.02f, 0.04f, 0.06f, 0.35f * opacity), rad);
        if (i < peekCount) {
            if (const auto* tex = peeks[static_cast<std::size_t>(i)];
                tex && tex->valid()) {
                ren.drawTextureRounded(tex, cellRect, rad,
                                       nxui::Color::white().withAlpha(opacity));
            } else {
                ren.drawRoundedRect(cellRect, accent.withAlpha(0.50f * opacity), rad);
            }
        } else {
            ren.drawRoundedRect(cellRect, accent.withAlpha(0.18f * opacity), rad);
        }
        ren.drawRoundedRectOutline(cellRect,
                                   nxui::Color::white().withAlpha(0.22f * opacity),
                                   rad, 1.15f * s);
    }
}

} // namespace

void drawFolderStyle(const FolderStyleDrawArgs& args) {
    if (!args.renderer)
        return;

    nxui::Renderer& ren = *args.renderer;
    const nxui::Rect& shell = args.bounds;
    const float s = std::max(0.35f, args.scale);
    const float opacity = args.opacity;
    const nxui::Color& accent = args.accent;
    const float shellRadius = args.radius;
    const nxui::Rect inner = shell.shrunk(1.f * s);
    const float innerRadius = std::max(8.f, shellRadius - 1.f * s);
    const bool poster = args.posterAspect;
    const float sliceH = std::max(4.f, shell.height * 0.09f);
    const ThemeShell theme = themeShell(args.themeMode, opacity);
    const int style = std::clamp(args.styleIndex, 0, kFolderStyleCount - 1);
    const nxui::Texture* cover = (args.cover && args.cover->valid()) ? args.cover : nullptr;
    const bool peekComposite = args.coverIsPeekComposite && cover;
    int peekCount = std::clamp(args.peekCoverCount, 0, kFolderPeekMax);
    for (int i = 0; i < peekCount; ++i) {
        if (!args.peekCovers[static_cast<std::size_t>(i)]
            || !args.peekCovers[static_cast<std::size_t>(i)]->valid()) {
            peekCount = i;
            break;
        }
    }
    const bool hasArt = cover || args.schematicPlaceholder || peekCount > 0 || peekComposite;
    // Caller sets drawName. Poster+art stays title-pill-only; empty poster
    // tiles (e.g. FT lite) still get a centered in-tile name like grid.
    const bool named = args.drawName && args.font && args.title
        && !args.title->empty();
    const bool useCover = hasArt && (
        (style != kFolderStyleClassic && folderShouldShowCover(style, args.showCover))
        || (poster && (cover || peekCount > 0 || peekComposite)));
    // Poster tiles are ~2× taller than grid icons — use a larger preferred
    // scale (still width-clamped) so empty single-row folders read clearly.
    const float namePref = poster ? 1.48f : 0.92f;
    const float nameMin = poster ? 0.55f : 0.40f;
    const float classicPref = poster ? 1.55f : 1.05f;

    auto drawCenteredName = [&](bool halo, float preferred, float minScale, float yBias) {
        if (!named)
            return;
        const FittedTitle fit = measureTitle(args.font, *args.title,
                                             std::max(8.f, shell.width - 18.f * s),
                                             preferred * s, minScale * s);
        const nxui::Vec2 pos{shell.x + (shell.width - fit.width) * 0.5f,
                             shell.y + (shell.height - fit.height) * yBias};
        if (halo)
            drawHaloTitle(ren, args.font, *args.title, pos, fit.scale, s, opacity);
        else
            ren.drawText(*args.title, pos, args.font, theme.title, fit.scale);
    };

    auto drawCoverName = [&](const nxui::Rect& host, const nxui::Rect& photo, bool halo) {
        if (!named)
            return;
        const FittedTitle fit = measureTitle(args.font, *args.title,
                                             std::max(8.f, host.width - 16.f * s),
                                             0.78f * s, 0.38f * s);
        const float nameY = std::min(photo.bottom() + 4.f * s,
                                     host.bottom() - fit.height - 6.f * s);
        const nxui::Vec2 pos{host.x + (host.width - fit.width) * 0.5f, nameY};
        if (halo)
            drawHaloTitle(ren, args.font, *args.title, pos, fit.scale, s, opacity);
        else
            ren.drawText(*args.title, pos, args.font, theme.title, fit.scale);
    };

    // Shared art fill: custom poster, 2x2 peeks, or single cover / placeholder.
    auto drawArtInHost = [&](const nxui::Rect& host, float topPad, float bottomPad) {
        if (args.coverIsFolderPoster && cover) {
            const nxui::Rect photo = coverPhotoRect(host, topPad, bottomPad, true);
            drawCoverPhoto(ren, photo, std::max(6.f, photo.width * 0.12f),
                           cover, accent, opacity, true);
            return;
        }
        if (poster && peekComposite) {
            // Same content band as drawPeekGrid; mosaic margins are baked in.
            const float side = std::max(4.f, 6.f * s);
            const nxui::Rect content{
                host.x + side,
                host.y + topPad,
                std::max(8.f, host.width - side * 2.f),
                std::max(18.f, host.height - topPad - bottomPad)};
            const float rad = std::max(6.f, std::min(content.width, content.height) * 0.06f);
            ren.drawTextureRounded(cover, content, rad,
                                   nxui::Color::white().withAlpha(opacity));
            return;
        }
        if (poster && peekCount > 0) {
            // Stay below/above style chrome; equal margin is applied inside drawPeekGrid.
            const float side = std::max(4.f, 6.f * s);
            const nxui::Rect content{
                host.x + side,
                host.y + topPad,
                std::max(8.f, host.width - side * 2.f),
                std::max(18.f, host.height - topPad - bottomPad)};
            drawPeekGrid(ren, content, args.peekCovers, peekCount, accent, s, opacity);
            return;
        }
        if (cover || args.schematicPlaceholder) {
            const nxui::Rect photo = coverPhotoRect(host, topPad, bottomPad, poster);
            drawCoverPhoto(ren, photo, std::max(6.f, photo.width * 0.16f),
                           cover, accent, opacity, poster);
        }
    };

    if (style == kFolderStyleClassic) {
        const nxui::Color plateWash =
            accent.withAlpha((theme.light ? 0.10f : 0.14f) * opacity);
        ren.drawRoundedRect(shell, theme.fill, shellRadius);
        ren.drawRoundedRect(shell, plateWash, shellRadius);
        ren.drawRoundedRectOutline(shell.shrunk(1.f * s), theme.outline,
                                   std::max(8.f, shellRadius - 1.f * s), 1.5f * s);
        if (poster && (peekCount > 0 || peekComposite
                       || (args.coverIsFolderPoster && cover))) {
            drawArtInHost(inner.shrunk(8.f * s), 0.f, 0.f);
        } else {
            drawNineGrid(ren, shell, accent, s, opacity);
            if (named) {
                const FittedTitle fit = measureTitle(args.font, *args.title,
                                                     std::max(8.f, shell.width - 6.f * s),
                                                     classicPref * s, nameMin * s);
                const nxui::Vec2 pos{shell.x + (shell.width - fit.width) * 0.5f,
                                     shell.y + (shell.height - fit.height) * 0.5f};
                drawHaloTitle(ren, args.font, *args.title, pos, fit.scale, s, opacity);
            }
        }
        if (args.focused)
            drawFocus(ren, shell, shellRadius, accent, s, opacity);
        return;
    }

    if (style == kFolderStyleManila) {
        const float tabW = std::max(18.f, shell.width * 0.28f);
        const nxui::Rect flap{shell.x + 8.f * s, shell.y,
                              shell.width - 16.f * s, shell.height * 0.30f};
        const nxui::Rect tab{shell.right() - 10.f * s - tabW, shell.y,
                             tabW, shell.height * 0.20f};
        const nxui::Rect body{shell.x, shell.y + shell.height * 0.12f,
                              shell.width, shell.height * 0.88f};
        const float bodyRadius = std::max(10.f, shellRadius * 0.92f);
        ren.drawRoundedRect(flap, accent.withAlpha(0.42f * opacity), 8.f * s);
        ren.drawRoundedRect(tab, accent.withAlpha(0.92f * opacity), 6.f * s);
        ren.drawRoundedRect(body, theme.fill, bodyRadius);
        ren.drawRoundedRect(body, accent.withAlpha((theme.light ? 0.08f : 0.12f) * opacity),
                            bodyRadius);
        ren.drawRoundedRectOutline(body.shrunk(1.f * s), theme.outline,
                                   std::max(8.f, bodyRadius - 1.f * s), 1.5f * s);
        if (useCover) {
            const nxui::Rect host = body.shrunk(6.f * s);
            const float bottomPad = named ? 20.f * s : 8.f * s;
            drawArtInHost(host, 4.f * s, bottomPad);
            if (named && !poster && !peekCount) {
                const nxui::Rect photo = coverPhotoRect(host, 4.f * s, bottomPad, false);
                drawCoverName(body, photo, true);
            }
        } else if (named) {
            const FittedTitle fit = measureTitle(args.font, *args.title,
                                                 std::max(8.f, body.width - 18.f * s),
                                                 namePref * s, nameMin * s);
            const nxui::Vec2 pos{body.x + (body.width - fit.width) * 0.5f,
                                 body.y + (body.height - fit.height) * 0.52f};
            ren.drawText(*args.title, pos, args.font, theme.title, fit.scale);
        } else if (!args.schematicPlaceholder) {
            drawEmptyGlyph(ren, body, accent, opacity);
        }
        if (args.focused)
            drawFocus(ren, shell, shellRadius, accent, s, opacity);
        return;
    }

    ren.drawRoundedRect(shell, theme.fill, shellRadius);
    ren.drawRoundedRect(shell, accent.withAlpha((theme.light ? 0.10f : 0.14f) * opacity),
                        shellRadius);

    if (style == kFolderStyleSimple)
        drawTopCap(ren, inner, innerRadius, accent, sliceH, opacity);
    else if (style == kFolderStyleTab) {
        const float tabW = std::max(14.f, inner.width * 0.34f);
        const float tabH = std::max(6.f, inner.height * 0.12f);
        ren.pushClipRect({inner.x, inner.y, tabW, tabH});
        ren.drawRoundedRect(inner, accent.withAlpha(0.82f * opacity), innerRadius);
        ren.popClipRect();
        const float highlightH = std::max(2.f, tabH * 0.40f);
        ren.pushClipRect({inner.x, inner.y, tabW, highlightH});
        ren.drawRoundedRect(inner, nxui::Color::white().withAlpha(0.22f * opacity), innerRadius);
        ren.popClipRect();
    } else if (style == kFolderStyleLabel)
        drawBottomBand(ren, inner, innerRadius, accent, sliceH, opacity);
    else if (style == kFolderStyleRing)
        ren.drawRoundedRectOutline(inner, accent.withAlpha(0.90f * opacity),
                                   innerRadius, std::max(2.5f, 3.6f * s));

    if (style != kFolderStyleRing)
        ren.drawRoundedRectOutline(inner, theme.outline, innerRadius, 1.5f * s);

    if (useCover) {
        const float topPad = (style == kFolderStyleSimple || style == kFolderStyleTab)
            ? sliceH + 6.f * s : 6.f * s;
        const float bottomPad = style == kFolderStyleLabel
            ? sliceH + (named ? 18.f * s : 8.f * s)
            : (named ? 20.f * s : 8.f * s);
        drawArtInHost(inner, topPad, bottomPad);
        if (named && !poster && cover && peekCount == 0) {
            const nxui::Rect photo = coverPhotoRect(inner, topPad, bottomPad, false);
            if (style == kFolderStyleLabel) {
                const FittedTitle fit = measureTitle(args.font, *args.title,
                                                     std::max(8.f, shell.width - 18.f * s),
                                                     0.78f * s, 0.38f * s);
                const float nameY = std::min(photo.bottom() + 3.f * s,
                                             shell.bottom() - sliceH - fit.height - 3.f * s);
                const nxui::Vec2 pos{shell.x + (shell.width - fit.width) * 0.5f, nameY};
                ren.drawText(*args.title, pos, args.font, theme.title, fit.scale);
            } else {
                drawCoverName(shell, photo, true);
            }
        }
        if (args.focused)
            drawFocus(ren, shell, shellRadius, accent, s, opacity);
        return;
    }

    if (style == kFolderStyleLabel && named) {
        const FittedTitle fit = measureTitle(args.font, *args.title,
                                             std::max(8.f, shell.width - 18.f * s),
                                             namePref * s, nameMin * s);
        const float nameY = shell.y + (shell.height - sliceH - fit.height) * 0.50f;
        const nxui::Vec2 pos{shell.x + (shell.width - fit.width) * 0.5f, nameY};
        ren.drawText(*args.title, pos, args.font, theme.title, fit.scale);
    } else if (named) {
        drawCenteredName(false, namePref, nameMin, 0.52f);
    } else if (!args.schematicPlaceholder &&
               (style == kFolderStyleSimple || style == kFolderStyleMinimal ||
                style == kFolderStyleTab || style == kFolderStyleRing)) {
        drawEmptyGlyph(ren, shell, accent, opacity);
    }

    if (args.focused)
        drawFocus(ren, shell, shellRadius, accent, s, opacity);
}

} // namespace switchu::folders
