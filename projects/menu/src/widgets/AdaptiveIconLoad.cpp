#include "AdaptiveIconLoad.hpp"

#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace switchu::icons {
namespace {

struct MonoStats {
    bool mono = false;
    float meanLuma = 0.5f;
};

MonoStats sampleMonochrome(const std::uint8_t* rgba, int w, int h) {
    MonoStats stats;
    if (!rgba || w <= 0 || h <= 0)
        return stats;

    // Sparse sample - icons are usually 256-512; a full scan is fine but this
    // keeps custom 1024s cheap.
    const int stepX = std::max(1, w / 64);
    const int stepY = std::max(1, h / 64);
    int opaque = 0;
    int colorful = 0;
    double lumaSum = 0.0;
    for (int y = 0; y < h; y += stepY) {
        for (int x = 0; x < w; x += stepX) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w)
                                   + static_cast<std::size_t>(x)) * 4u;
            const int a = rgba[i + 3];
            if (a < 24)
                continue;
            ++opaque;
            const int r = rgba[i];
            const int g = rgba[i + 1];
            const int b = rgba[i + 2];
            const int chroma = std::max({r, g, b}) - std::min({r, g, b});
            if (chroma > 28)
                ++colorful;
            lumaSum += (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
        }
    }
    if (opaque < 8)
        return stats;
    // A few anti-aliased edge samples can pick up theme-panel bleed; require a
    // clear colourful majority to treat as a colour icon.
    stats.mono = (static_cast<float>(colorful) / static_cast<float>(opaque)) < 0.03f;
    stats.meanLuma = static_cast<float>(lumaSum / opaque);
    return stats;
}

// Encode grayscale (and solid black/white glyphs) as a white RGB × coverage
// alpha mask so multiply tinting by chrome textPrimary yields light or dark
// icons while mid-tone gradients survive in alpha - not flat stark silhouettes.
void normalizeToTintableMask(std::uint8_t* rgba, int w, int h, float meanLuma) {
    // Dark ink (near-black glyphs): coverage rises as luma falls.
    // Light / gray-gradient ink: coverage follows luma so soft fills keep shape.
    const bool darkInk = meanLuma < 0.45f;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t p = i * 4u;
        const float r = rgba[p] / 255.f;
        const float g = rgba[p + 1] / 255.f;
        const float b = rgba[p + 2] / 255.f;
        const float a = rgba[p + 3] / 255.f;
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        const float ink = darkInk ? (1.f - luma) : luma;
        const float coverage = std::clamp(ink * a, 0.f, 1.f);
        // Premultiplied-safe: empty texels stay RGB 0 so tint never flashes a
        // solid white/black plate behind transparent mosaic cells.
        if (coverage <= 0.001f) {
            rgba[p] = 0;
            rgba[p + 1] = 0;
            rgba[p + 2] = 0;
            rgba[p + 3] = 0;
        } else {
            rgba[p] = 255;
            rgba[p + 1] = 255;
            rgba[p + 2] = 255;
            rgba[p + 3] = static_cast<std::uint8_t>(coverage * 255.f + 0.5f);
        }
    }
}

// Colour artwork → white×alpha *luminance* mask so multiply tint / B/W force
// keeps internal shading and gradients instead of a flat silhouette.
void buildColourCoverageMask(std::uint8_t* rgba, int w, int h) {
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t p = i * 4u;
        const float r = rgba[p] / 255.f;
        const float g = rgba[p + 1] / 255.f;
        const float b = rgba[p + 2] / 255.f;
        const float a = rgba[p + 3] / 255.f;
        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        // Mild lift so near-black ink still reads when tinted light, without
        // flattening mid-tones into a solid plate.
        const float ink = std::clamp(luma * 0.88f + 0.12f, 0.f, 1.f);
        const float coverage = std::clamp(ink * a, 0.f, 1.f);
        if (coverage <= 0.001f) {
            rgba[p] = 0;
            rgba[p + 1] = 0;
            rgba[p + 2] = 0;
            rgba[p + 3] = 0;
        } else {
            rgba[p] = 255;
            rgba[p + 1] = 255;
            rgba[p + 2] = 255;
            rgba[p + 3] = static_cast<std::uint8_t>(coverage * 255.f + 0.5f);
        }
    }
}

void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx <= mn + 1e-6f) {
        h = 0.f;
        s = 0.f;
        return;
    }
    const float d = mx - mn;
    s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r)
        h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g)
        h = (b - r) / d + 2.f;
    else
        h = (r - g) / d + 4.f;
    h /= 6.f;
}

float hue2rgb(float p, float q, float t) {
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s <= 1e-6f) {
        r = g = b = l;
        return;
    }
    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    r = hue2rgb(p, q, h + 1.f / 3.f);
    g = hue2rgb(p, q, h);
    b = hue2rgb(p, q, h - 1.f / 3.f);
}

float adjustChannelPivot(float value, float pivot) {
    // 0.5 = identity. Below desaturates/darkens toward 0; above pushes to 1.
    pivot = std::clamp(pivot, 0.f, 1.f);
    if (pivot <= 0.5f)
        return value * (pivot * 2.f);
    return value + (1.f - value) * ((pivot - 0.5f) * 2.f);
}

// Rotate every pixel's hue and pivot sat/light - keeps multi-colour artwork
// multi-colour (Photoshop Hue/Saturation style), unlike a flat colorize.
void applyHslAdjust(std::uint8_t* rgba, int w, int h, const HslAdjust& adj) {
    if (!rgba || w <= 0 || h <= 0 || !adj.enabled)
        return;
    const float hueOff = std::clamp(adj.hue, 0.f, 1.f) - 0.5f;
    const bool hueId = std::fabs(hueOff) < 0.0005f;
    const bool satId = std::fabs(adj.sat - 0.5f) < 0.0005f;
    const bool lightId = std::fabs(adj.light - 0.5f) < 0.0005f;
    if (hueId && satId && lightId)
        return;

    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t p = i * 4u;
        if (rgba[p + 3] < 8)
            continue;
        float r = rgba[p] / 255.f;
        float g = rgba[p + 1] / 255.f;
        float b = rgba[p + 2] / 255.f;
        float h = 0.f, s = 0.f, l = 0.f;
        rgbToHsl(r, g, b, h, s, l);
        if (!hueId) {
            h += hueOff;
            h -= std::floor(h);
        }
        if (!satId)
            s = std::clamp(adjustChannelPivot(s, adj.sat), 0.f, 1.f);
        if (!lightId)
            l = std::clamp(adjustChannelPivot(l, adj.light), 0.f, 1.f);
        hslToRgb(h, s, l, r, g, b);
        rgba[p] = static_cast<std::uint8_t>(std::clamp(r, 0.f, 1.f) * 255.f + 0.5f);
        rgba[p + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.f, 1.f) * 255.f + 0.5f);
        rgba[p + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.f, 1.f) * 255.f + 0.5f);
    }
}

} // namespace

ColourIconForce resolveColourIconContrast(float iconMeanLuma,
                                          float backdropLuma,
                                          ColourIconForce previous) {
    // Adaptive contrast exists to rescue artwork that would disappear against
    // the plate behind it, not to flatten every colour icon into a silhouette.
    // So: keep the authored colours as soon as the glyph is clearly lighter or
    // darker than the plate, and only force black/white when the two are too
    // close to read.
    if (!(backdropLuma >= 0.f) || !(iconMeanLuma >= 0.f))
        return ColourIconForce::None;

    const float delta = std::fabs(iconMeanLuma - backdropLuma);
    // Hysteresis: once forced, require a slightly larger gap to go back to
    // colour so focus skims do not flicker.
    const float keepColourDelta = (previous == ColourIconForce::None) ? 0.22f : 0.26f;
    if (delta >= keepColourDelta)
        return ColourIconForce::None;

    return backdropLuma < 0.5f ? ColourIconForce::White : ColourIconForce::Black;
}

bool decodeMaybeAdaptiveIcon(const std::string& path,
                             DecodedAdaptiveIcon& out,
                             int maxSide,
                             const HslAdjust* hslAdjust,
                             bool wantContrastMask) {
    out = DecodedAdaptiveIcon{};
    if (path.empty())
        return false;

    int w = 0, h = 0, ch = 0;
    std::uint8_t* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data)
            stbi_image_free(data);
        return false;
    }

    std::uint8_t* owned = data;
    bool stbiOwned = true;
    if (maxSide > 0 && (w > maxSide || h > maxSide)) {
        const float scale = std::min(static_cast<float>(maxSide) / w,
                                     static_cast<float>(maxSide) / h);
        const int dw = std::max(1, static_cast<int>(w * scale));
        const int dh = std::max(1, static_cast<int>(h * scale));
        auto* scaled = static_cast<std::uint8_t*>(
            std::malloc(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4u));
        if (scaled) {
            for (int y = 0; y < dh; ++y) {
                const int sy = y * h / dh;
                for (int x = 0; x < dw; ++x) {
                    const int sx = x * w / dw;
                    std::memcpy(scaled + (static_cast<std::size_t>(y) * dw + x) * 4u,
                                data + (static_cast<std::size_t>(sy) * w + sx) * 4u,
                                4u);
                }
            }
            stbi_image_free(data);
            owned = scaled;
            stbiOwned = false;
            w = dw;
            h = dh;
        }
    }

    out.w = w;
    out.h = h;

    // Plain screenshots/covers (theme previews): never run icon mono heuristics.
    // Flat Default Light/Dark cards were classified as mono masks, which produced
    // broken GPU uploads and soft-locks when opening detail.
    if (!wantContrastMask && !(hslAdjust && hslAdjust->enabled)) {
        out.adaptive = false;
        out.meanLuma = 0.f;
        out.rgba.assign(owned, owned + static_cast<std::size_t>(w) * h * 4u);
        if (stbiOwned)
            stbi_image_free(owned);
        else
            std::free(owned);
        return true;
    }

    const MonoStats mono = sampleMonochrome(owned, w, h);
    out.meanLuma = mono.meanLuma;

    if (mono.mono) {
        normalizeToTintableMask(owned, w, h, mono.meanLuma);
        out.adaptive = true;
        out.rgba.assign(owned, owned + static_cast<std::size_t>(w) * h * 4u);
        if (stbiOwned)
            stbi_image_free(owned);
        else
            std::free(owned);
        return true;
    }

    if (wantContrastMask) {
        out.contrastMask.assign(owned, owned + static_cast<std::size_t>(w) * h * 4u);
        buildColourCoverageMask(out.contrastMask.data(), w, h);
    }
    if (hslAdjust && hslAdjust->enabled)
        applyHslAdjust(owned, w, h, *hslAdjust);

    out.adaptive = false;
    out.rgba.assign(owned, owned + static_cast<std::size_t>(w) * h * 4u);
    if (stbiOwned)
        stbi_image_free(owned);
    else
        std::free(owned);
    return !out.rgba.empty();
}

bool loadMaybeAdaptiveIcon(nxui::Texture& out,
                           nxui::GpuDevice& gpu,
                           nxui::Renderer& ren,
                           const std::string& path,
                           bool* outAdaptive,
                           int maxSide,
                           float* outMeanLuma,
                           nxui::Texture* outContrastMask,
                           const HslAdjust* hslAdjust) {
    if (outAdaptive)
        *outAdaptive = false;
    if (outMeanLuma)
        *outMeanLuma = 0.5f;

    DecodedAdaptiveIcon decoded;
    if (!decodeMaybeAdaptiveIcon(path, decoded, maxSide, hslAdjust, outContrastMask != nullptr))
        return false;

    if (outMeanLuma)
        *outMeanLuma = decoded.meanLuma;
    if (outAdaptive)
        *outAdaptive = decoded.adaptive;

    const bool ok = out.loadFromPixels(gpu, ren, decoded.rgba.data(), decoded.w, decoded.h);
    if (ok && outContrastMask && !decoded.contrastMask.empty())
        outContrastMask->loadFromPixels(gpu, ren, decoded.contrastMask.data(), decoded.w, decoded.h);
    return ok;
}

bool probeIconLooksColour(const std::string& path) {
    if (path.empty())
        return false;
    int w = 0, h = 0, ch = 0;
    std::uint8_t* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data)
            stbi_image_free(data);
        return false;
    }
    const MonoStats stats = sampleMonochrome(data, w, h);
    stbi_image_free(data);
    return !stats.mono;
}

} // namespace switchu::icons
