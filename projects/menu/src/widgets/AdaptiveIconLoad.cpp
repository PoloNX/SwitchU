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

    // Sparse sample — icons are usually 256–512; a full scan is fine but this
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
// icons while mid-tone gradients survive in alpha — not flat stark silhouettes.
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

} // namespace

bool loadMaybeAdaptiveIcon(nxui::Texture& out,
                           nxui::GpuDevice& gpu,
                           nxui::Renderer& ren,
                           const std::string& path,
                           bool* outAdaptive,
                           int maxSide) {
    if (outAdaptive)
        *outAdaptive = false;

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

    const MonoStats mono = sampleMonochrome(owned, w, h);
    if (mono.mono)
        normalizeToTintableMask(owned, w, h, mono.meanLuma);
    if (outAdaptive)
        *outAdaptive = mono.mono;

    const bool ok = out.loadFromPixels(gpu, ren, owned, w, h);
    if (stbiOwned)
        stbi_image_free(owned);
    else
        std::free(owned);
    return ok;
}

} // namespace switchu::icons
