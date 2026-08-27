#include "SteamGridDbBackdrop.hpp"

#include "steamgriddb/SteamGridDbManager.hpp"
#include "core/DebugLog.hpp"

#include <nxui/core/Renderer.hpp>
#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

SteamGridDbBackdrop::SteamGridDbBackdrop(nxui::GpuDevice& gpu,
                                         nxui::Renderer& renderer,
                                         nxui::ThreadPool* threadPool)
    : m_gpu(gpu), m_renderer(renderer), m_threadPool(threadPool) {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setFocusable(false);
    setFrameworkTouchEnabled(false);
    m_fade.setImmediate(1.f);
}

void SteamGridDbBackdrop::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled && m_pendingDecode && m_pendingDecode->state)
        m_pendingDecode->state->cancelled.store(true);
    setVisible(enabled);
}

bool SteamGridDbBackdrop::hasGpuArtwork(std::uint64_t titleId) const {
    return std::any_of(m_sets.begin(), m_sets.end(), [titleId](const ArtworkSet& set) {
        return set.titleId == titleId && (set.hasHero || set.hasLogo);
    });
}

void SteamGridDbBackdrop::setPreloadTitles(std::vector<std::uint64_t> titleIds) {
    m_preloadTitleIds.clear();
    for (std::uint64_t titleId : titleIds) {
        if (titleId == 0 || titleId == m_requestedTitleId
            || std::find(m_preloadTitleIds.begin(), m_preloadTitleIds.end(), titleId)
                != m_preloadTitleIds.end())
            continue;
        m_preloadTitleIds.push_back(titleId);
        if (m_preloadTitleIds.size() >= 8)
            break;
    }
}

void SteamGridDbBackdrop::showTitle(std::uint64_t titleId, bool forceReload) {
    if (!m_enabled) return;
    if (!forceReload && m_requestedTitleId == titleId) return;

    if (forceReload) {
        m_decodedCache.erase(
            std::remove_if(m_decodedCache.begin(), m_decodedCache.end(),
                           [titleId](const auto& artwork) { return artwork.titleId == titleId; }),
            m_decodedCache.end());
        m_missingArtworkTitleIds.erase(
            std::remove(m_missingArtworkTitleIds.begin(), m_missingArtworkTitleIds.end(), titleId),
            m_missingArtworkTitleIds.end());
    }

    if (!forceReload
        && std::find(m_missingArtworkTitleIds.begin(), m_missingArtworkTitleIds.end(), titleId)
            != m_missingArtworkTitleIds.end()) {
        m_requestedTitleId = titleId;
        ++m_requestGeneration;
        m_appliedGeneration = m_requestGeneration;
        return;
    }

    if (!forceReload && titleId != 0) {
        for (int i = 0; i < static_cast<int>(m_sets.size()); ++i) {
            const auto& cached = m_sets[i];
            if (cached.titleId != titleId
                || (!cached.hasHero && !cached.hasLogo))
                continue;

            if (m_pendingDecode && m_pendingDecode->state)
                m_pendingDecode->state->cancelled.store(true);
            m_readyArtwork.reset();
            m_uploadStage = 0;
            m_requestedTitleId = titleId;
            ++m_requestGeneration;
            m_appliedGeneration = m_requestGeneration;
            if (i != m_current)
                beginCrossfade(i, titleId);
            DebugLog::log("[steamgriddb-ui] gpu cache hit title=%016llX",
                          static_cast<unsigned long long>(titleId));
            return;
        }

        auto decoded = std::find_if(m_decodedCache.begin(), m_decodedCache.end(),
            [titleId](const auto& artwork) { return artwork.titleId == titleId; });
        if (decoded != m_decodedCache.end()) {
            if (m_pendingDecode && m_pendingDecode->state)
                m_pendingDecode->state->cancelled.store(true);
            m_requestedTitleId = titleId;
            ++m_requestGeneration;
            decoded->generation = m_requestGeneration;
            m_readyArtwork = std::move(*decoded);
            m_decodedCache.erase(decoded);
            m_uploadStage = 0;
            DebugLog::log("[steamgriddb-ui] decoded cache hit title=%016llX",
                          static_cast<unsigned long long>(titleId));
            return;
        }
    }

    m_requestedTitleId = titleId;
    ++m_requestGeneration;
    // The focus animation already absorbs rapid left/right repeats. Starting
    // immediately makes cached SD artwork appear noticeably sooner.
    m_decodeDebounce = 0.f;
    m_readyArtwork.reset();
    m_uploadStage = 0;
    if (m_pendingDecode && m_pendingDecode->state)
        m_pendingDecode->state->cancelled.store(true);
    DebugLog::log("[steamgriddb-ui] cache miss title=%016llX",
                  static_cast<unsigned long long>(titleId));
}

SteamGridDbBackdrop::DecodedImage SteamGridDbBackdrop::decodeImage(
    const std::string& path, int outputWidth, int outputHeight, bool fill) {
    DecodedImage out;
    const auto loadStarted = std::chrono::steady_clock::now();
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) stbi_image_free(raw);
        return out;
    }
    const auto loadedAt = std::chrono::steady_clock::now();

    out.width = std::max(1, outputWidth);
    out.height = std::max(1, outputHeight);
    if (width == out.width && height == out.height) {
        out.rgba.assign(raw, raw + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(raw);
        DebugLog::log("[steamgriddb-ui] image path=%s source=%dx%d load=%ldms scale=0ms",
                      path.c_str(), width, height,
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          loadedAt - loadStarted).count());
        return out;
    }

    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    const float scaleX = static_cast<float>(out.width) / width;
    const float scaleY = static_cast<float>(out.height) / height;
    const float scale = fill ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
    const float drawnWidth = width * scale;
    const float drawnHeight = height * scale;
    const float offsetX = (out.width - drawnWidth) * 0.5f;
    const float offsetY = (out.height - drawnHeight) * 0.5f;

    // Precompute the mappings once. The previous implementation repeated two
    // floating-point divisions and clamps for every output pixel (~921k for a
    // hero), which dominates decode time on the Switch CPU.
    std::vector<int> sourceXs(static_cast<std::size_t>(out.width), -1);
    std::vector<int> sourceYs(static_cast<std::size_t>(out.height), -1);
    const float inverseScale = 1.f / scale;
    for (int x = 0; x < out.width; ++x) {
        const float source = (x - offsetX) * inverseScale;
        if (fill || (source >= 0.f && source < width))
            sourceXs[static_cast<std::size_t>(x)] =
                std::clamp(static_cast<int>(source), 0, width - 1);
    }
    for (int y = 0; y < out.height; ++y) {
        const float source = (y - offsetY) * inverseScale;
        if (fill || (source >= 0.f && source < height))
            sourceYs[static_cast<std::size_t>(y)] =
                std::clamp(static_cast<int>(source), 0, height - 1);
    }
    const auto* sourcePixels = reinterpret_cast<const std::uint32_t*>(raw);
    auto* destinationPixels = reinterpret_cast<std::uint32_t*>(out.rgba.data());
    for (int y = 0; y < out.height; ++y) {
        const int sourceY = sourceYs[static_cast<std::size_t>(y)];
        if (sourceY < 0) continue;
        auto* destinationRow = destinationPixels + static_cast<std::size_t>(y) * out.width;
        const auto* sourceRow = sourcePixels + static_cast<std::size_t>(sourceY) * width;
        for (int x = 0; x < out.width; ++x) {
            const int sourceX = sourceXs[static_cast<std::size_t>(x)];
            if (sourceX >= 0)
                destinationRow[x] = sourceRow[sourceX];
        }
    }
    stbi_image_free(raw);
    const auto scaledAt = std::chrono::steady_clock::now();
    DebugLog::log("[steamgriddb-ui] image path=%s source=%dx%d load=%ldms scale=%ldms",
                  path.c_str(), width, height,
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      loadedAt - loadStarted).count(),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      scaledAt - loadedAt).count());
    return out;
}

void SteamGridDbBackdrop::startPendingDecode(std::uint64_t titleId) {
    auto state = std::make_shared<DecodeState>();
    state->artwork.titleId = titleId;
    state->artwork.generation = titleId == m_requestedTitleId ? m_requestGeneration : 0;

    auto work = [state, titleId]() {
        const auto started = std::chrono::steady_clock::now();
        if (titleId != 0 && SteamGridDbManager::hasArtwork(titleId)) {
            // Fixed canvases let both double-buffer slots reuse their existing
            // MemBlocks. Variable source dimensions previously triggered a
            // free + waitIdle on the render thread during focus changes.
            state->artwork.hero = decodeImage(
                SteamGridDbManager::heroPath(titleId), 1280, 720, true);
            if (!state->cancelled.load())
                state->artwork.logo = decodeImage(
                    SteamGridDbManager::logoPath(titleId), 640, 180, false);
        }
        state->artwork.decodeMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    };

    PendingDecode pending;
    pending.state = state;
    if (m_threadPool)
        pending.future = m_threadPool->submit(std::move(work));
    else
        pending.future = std::async(std::launch::async, std::move(work));
    m_pendingDecode = std::move(pending);
}

void SteamGridDbBackdrop::beginCrossfade(int nextSet, std::uint64_t titleId) {
    m_sets[nextSet].titleId = titleId;
    m_current = nextSet;
    m_fade.setImmediate(0.f);
    m_fade.set(1.f, 0.52f, nxui::Easing::outCubic);
}

nxui::Rect SteamGridDbBackdrop::fillRect(const nxui::Texture& texture,
                                         const nxui::Rect& area) {
    if (!texture.valid()) return area;
    const float scale = std::max(area.width / texture.width(), area.height / texture.height());
    const float w = texture.width() * scale;
    const float h = texture.height() * scale;
    return {area.x + (area.width - w) * 0.5f, area.y + (area.height - h) * 0.5f, w, h};
}

nxui::Rect SteamGridDbBackdrop::containRect(const nxui::Texture& texture,
                                            const nxui::Rect& area) {
    if (!texture.valid()) return area;
    const float scale = std::min(area.width / texture.width(), area.height / texture.height());
    const float w = texture.width() * scale;
    const float h = texture.height() * scale;
    return {area.x + (area.width - w) * 0.5f, area.y + (area.height - h) * 0.5f, w, h};
}

void SteamGridDbBackdrop::drawSet(nxui::Renderer& renderer,
                                  const ArtworkSet& set,
                                  float alpha) const {
    if (alpha <= 0.002f) return;
    const nxui::Rect screen = rect();

    if (set.hasHero && set.hero.valid()) {
        const float heroAlpha = m_layoutMode == AppLayoutMode::DynamicLine ? 0.56f : 0.16f;
        renderer.pushClipRect(screen);
        renderer.drawTexture(&set.hero, fillRect(set.hero, screen),
                             nxui::Color::white().withAlpha(alpha * heroAlpha));
        renderer.popClipRect();
        if (m_layoutMode == AppLayoutMode::DynamicLine) {
            renderer.drawGradientRect(
                screen,
                nxui::Color(0.f, 0.f, 0.f, 0.04f * alpha),
                nxui::Color(0.f, 0.f, 0.f, 0.46f * alpha));
        }
    }

    if (m_layoutMode != AppLayoutMode::DynamicLine) return;

    if (set.hasLogo && set.logo.valid()) {
        const nxui::Rect logoArea{370.f, 174.f, 540.f, 150.f};
        renderer.drawTexture(&set.logo, containRect(set.logo, logoArea),
                             nxui::Color::white().withAlpha(0.96f * alpha));
    }
}

void SteamGridDbBackdrop::onUpdate(float dt) {
    m_fade.update(std::min(dt, 0.04f));

    if (m_pendingDecode
        && m_pendingDecode->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            m_pendingDecode->future.get();
        } catch (...) {
        }
        auto state = std::move(m_pendingDecode->state);
        m_pendingDecode.reset();
        if (state && !state->cancelled.load()
            && state->artwork.titleId == m_requestedTitleId
            && state->artwork.generation == m_requestGeneration) {
            DebugLog::log("[steamgriddb-ui] decoded title=%016llX in %ldms hero=%d logo=%d",
                          static_cast<unsigned long long>(state->artwork.titleId),
                          state->artwork.decodeMilliseconds,
                          state->artwork.hero.rgba.empty() ? 0 : 1,
                          state->artwork.logo.rgba.empty() ? 0 : 1);
            const bool hasArtwork = !state->artwork.hero.rgba.empty()
                                 || !state->artwork.logo.rgba.empty();
            if (hasArtwork) {
                m_readyArtwork = std::move(state->artwork);
                m_uploadStage = 0;
            } else {
                m_missingArtworkTitleIds.push_back(state->artwork.titleId);
                if (m_missingArtworkTitleIds.size() > kMissingCacheLimit)
                    m_missingArtworkTitleIds.erase(m_missingArtworkTitleIds.begin());
                m_appliedGeneration = m_requestGeneration;
            }
        } else if (state && !state->cancelled.load()
                   && std::find(m_preloadTitleIds.begin(), m_preloadTitleIds.end(),
                                state->artwork.titleId) != m_preloadTitleIds.end()) {
            DebugLog::log("[steamgriddb-ui] prefetched title=%016llX in %ldms hero=%d logo=%d",
                          static_cast<unsigned long long>(state->artwork.titleId),
                          state->artwork.decodeMilliseconds,
                          state->artwork.hero.rgba.empty() ? 0 : 1,
                          state->artwork.logo.rgba.empty() ? 0 : 1);
            m_decodedCache.erase(
                std::remove_if(m_decodedCache.begin(), m_decodedCache.end(),
                    [&state](const auto& artwork) {
                        return artwork.titleId == state->artwork.titleId;
                    }),
                m_decodedCache.end());
            const bool hasArtwork = !state->artwork.hero.rgba.empty()
                                 || !state->artwork.logo.rgba.empty();
            if (hasArtwork) {
                m_decodedCache.push_back(std::move(state->artwork));
                if (m_decodedCache.size() > kDecodedCacheLimit)
                    m_decodedCache.erase(m_decodedCache.begin());
            } else if (std::find(m_missingArtworkTitleIds.begin(),
                                 m_missingArtworkTitleIds.end(), state->artwork.titleId)
                       == m_missingArtworkTitleIds.end()) {
                m_missingArtworkTitleIds.push_back(state->artwork.titleId);
                if (m_missingArtworkTitleIds.size() > kMissingCacheLimit)
                    m_missingArtworkTitleIds.erase(m_missingArtworkTitleIds.begin());
            }
        }
    }

    if (m_readyArtwork && m_readyArtwork->generation != m_requestGeneration) {
        m_readyArtwork.reset();
        m_uploadStage = 0;
    }

    if (m_readyArtwork) {
        auto& decoded = *m_readyArtwork;
        auto& target = m_sets[1 - m_current];
        if (m_uploadStage == 0) {
            target.hasHero = !decoded.hero.rgba.empty()
                && target.hero.loadFromPixels(m_gpu, m_renderer, decoded.hero.rgba.data(),
                                              decoded.hero.width, decoded.hero.height);
            decoded.hero.rgba.clear();
        } else if (m_uploadStage == 1) {
            target.hasLogo = !decoded.logo.rgba.empty()
                && target.logo.loadFromPixels(m_gpu, m_renderer, decoded.logo.rgba.data(),
                                              decoded.logo.width, decoded.logo.height);
            decoded.logo.rgba.clear();
        }
        ++m_uploadStage;
        if (m_uploadStage >= 2) {
            const int next = 1 - m_current;
            const std::uint64_t titleId = decoded.titleId;
            m_appliedGeneration = decoded.generation;
            m_readyArtwork.reset();
            m_uploadStage = 0;
            beginCrossfade(next, titleId);
        }
        return; // Strictly cap artwork uploads to one per frame.
    }

    if (!m_pendingDecode && m_appliedGeneration != m_requestGeneration) {
        m_decodeDebounce = std::max(0.f, m_decodeDebounce - dt);
        if (m_decodeDebounce <= 0.f)
            startPendingDecode(m_requestedTitleId);
    } else if (!m_pendingDecode && !m_readyArtwork) {
        for (std::uint64_t titleId : m_preloadTitleIds) {
            const bool decoded = std::any_of(
                m_decodedCache.begin(), m_decodedCache.end(),
                [titleId](const auto& artwork) { return artwork.titleId == titleId; });
            const bool missing = std::find(m_missingArtworkTitleIds.begin(),
                                           m_missingArtworkTitleIds.end(), titleId)
                != m_missingArtworkTitleIds.end();
            if (!decoded && !missing && !hasGpuArtwork(titleId)) {
                DebugLog::log("[steamgriddb-ui] prefetch scheduled title=%016llX",
                              static_cast<unsigned long long>(titleId));
                startPendingDecode(titleId);
                break;
            }
        }
    }
}

void SteamGridDbBackdrop::onRender(nxui::Renderer& renderer) {
    if (!m_enabled || opacity() <= 0.002f) return;
    const float t = std::clamp(m_fade.value(), 0.f, 1.f);
    drawSet(renderer, m_sets[1 - m_current], (1.f - t) * opacity());
    drawSet(renderer, m_sets[m_current], t * opacity());
}
