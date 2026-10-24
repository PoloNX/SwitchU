#include "SteamGridDbBackdrop.hpp"

#include "steamgriddb/ArtworkCache.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"
#include "core/DebugLog.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

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

void SteamGridDbBackdrop::invalidateArtworkCaches() {
    // Forget which title each uploaded set belongs to. No texture is released
    // here: a settings change can land mid-frame while the GPU still reads these
    // images, and clearing the title id is enough to miss the gpu-cache test so
    // the next decode overwrites the slot through the upload path that already
    // waits on the fence. The decoded and "missing artwork" lists go too: a
    // SteamGridDB download is exactly what turns one of the latter into art.
    for (auto& set : m_sets)
        set.titleId = 0;
    m_decodedCache.clear();
    m_missingArtworkTitleIds.clear();
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
        // Keep this in lockstep with the decoded-cache capacity. Queuing more
        // titles than can be retained makes the oldest entry get evicted and
        // immediately decoded again forever.
        if (m_preloadTitleIds.size() >= kDecodedCacheLimit)
            break;
    }
}

void SteamGridDbBackdrop::setPreloadMinInterval(float seconds) {
    m_preloadMinInterval = std::max(0.f, seconds);
}

void SteamGridDbBackdrop::setPreloadPaused(bool paused) {
    m_preloadPaused = paused;
}

bool SteamGridDbBackdrop::isArtworkBusy() const {
    return m_pendingDecode.has_value()
        || m_readyArtwork.has_value()
        || m_uploadStage > 0;
}

void SteamGridDbBackdrop::showTitle(std::uint64_t titleId, bool forceReload) {
    // Clearing (titleId == 0) must run even when disabled so folder close /
    // view toggles never leave a stale hero on screen.
    if (!m_enabled && titleId != 0) return;
    if (!forceReload && m_requestedTitleId == titleId) return;

    // A direct artwork-to-artwork change may crossfade from the current set.
    // Once focus has visited a title without artwork, the old set is stale and
    // must never be exposed again while the next image is decoded/uploaded.
    m_showPreviousDuringCrossfade = titleId != 0
        && m_sets[m_current].titleId == m_requestedTitleId
        && (m_sets[m_current].hasHero || m_sets[m_current].hasLogo)
        && m_artworkOpacity.value() > 0.002f;

    if (titleId == 0) {
        if (m_pendingDecode && m_pendingDecode->state)
            m_pendingDecode->state->cancelled.store(true);
        m_readyArtwork.reset();
        m_uploadStage = 0;
        m_requestedTitleId = 0;
        ++m_requestGeneration;
        m_appliedGeneration = m_requestGeneration;
        m_showPreviousDuringCrossfade = false;
        if (forceReload) {
            // Instant wipe (folder close) — no fade of the previous hero.
            for (auto& set : m_sets) {
                set.titleId = 0;
                set.hasHero = false;
                set.hasLogo = false;
                set.heroLuminance = -1.f;
            }
            m_artworkOpacity.setImmediate(0.f);
            m_fade.setImmediate(1.f);
        } else {
            m_artworkOpacity.set(0.f, 0.22f, nxui::Easing::outCubic);
        }
        DebugLog::log("[steamgriddb-ui] artwork cleared: selection has no title");
        return;
    }

    if (forceReload) {
        m_waitForGpuBeforeUpload = true;
        // Evict only transient UI state. The disk cache fingerprints its source,
        // so a replaced asset invalidates itself while a freshly prepared cache
        // remains available for this reload.
        m_decodedCache.erase(
            std::remove_if(m_decodedCache.begin(), m_decodedCache.end(),
                           [titleId](const auto& artwork) { return artwork.titleId == titleId; }),
            m_decodedCache.end());
        m_missingArtworkTitleIds.erase(
            std::remove(m_missingArtworkTitleIds.begin(), m_missingArtworkTitleIds.end(), titleId),
            m_missingArtworkTitleIds.end());
    }

    auto missingArtwork = std::find(m_missingArtworkTitleIds.begin(),
                                    m_missingArtworkTitleIds.end(), titleId);
    if (!forceReload && missingArtwork != m_missingArtworkTitleIds.end()
        && SteamGridDbManager::hasArtwork(titleId)) {
        // A SteamGridDB download may have completed since this title was
        // classified as missing. Revalidate the negative cache during the same
        // menu session instead of requiring a restart.
        m_missingArtworkTitleIds.erase(missingArtwork);
        missingArtwork = m_missingArtworkTitleIds.end();
        DebugLog::log("[steamgriddb-ui] artwork became available: title=%016llX",
                      static_cast<unsigned long long>(titleId));
    }

    if (!forceReload && missingArtwork != m_missingArtworkTitleIds.end()) {
        if (m_pendingDecode && m_pendingDecode->state)
            m_pendingDecode->state->cancelled.store(true);
        m_readyArtwork.reset();
        m_uploadStage = 0;
        m_requestedTitleId = titleId;
        ++m_requestGeneration;
        m_appliedGeneration = m_requestGeneration;
        m_showPreviousDuringCrossfade = false;
        m_artworkOpacity.set(0.f, 0.22f, nxui::Easing::outCubic);
        DebugLog::log("[steamgriddb-ui] artwork cleared: title=%016llX has no assets",
                      static_cast<unsigned long long>(titleId));
        return;
    }

    // A directory lookup is much cheaper than decoding an image and lets a
    // missing-artwork selection start fading immediately instead of retaining
    // the previous game's hero until the worker completes.
    if (!forceReload && !SteamGridDbManager::hasArtwork(titleId)) {
        if (m_pendingDecode && m_pendingDecode->state)
            m_pendingDecode->state->cancelled.store(true);
        m_readyArtwork.reset();
        m_uploadStage = 0;
        m_requestedTitleId = titleId;
        ++m_requestGeneration;
        m_appliedGeneration = m_requestGeneration;
        if (std::find(m_missingArtworkTitleIds.begin(), m_missingArtworkTitleIds.end(), titleId)
            == m_missingArtworkTitleIds.end()) {
            m_missingArtworkTitleIds.push_back(titleId);
            if (m_missingArtworkTitleIds.size() > kMissingCacheLimit)
                m_missingArtworkTitleIds.erase(m_missingArtworkTitleIds.begin());
        }
        m_showPreviousDuringCrossfade = false;
        m_artworkOpacity.set(0.f, 0.22f, nxui::Easing::outCubic);
        DebugLog::log("[steamgriddb-ui] artwork fading out: title=%016llX has no files",
                      static_cast<unsigned long long>(titleId));
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
            else {
                m_fade.setImmediate(1.f);
                m_artworkOpacity.set(1.f, 0.18f, nxui::Easing::outCubic);
            }
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
    // Coalesce very rapid focus changes. GPU/decoded cache hits above remain
    // immediate, while disk decoding waits until the selection has been stable
    // briefly instead of repeatedly decoding artwork the user has passed.
    m_decodeDebounce = forceReload ? 0.f : 0.10f;
    m_readyArtwork.reset();
    m_uploadStage = 0;
    if (m_pendingDecode && m_pendingDecode->state)
        m_pendingDecode->state->cancelled.store(true);
    DebugLog::log("[steamgriddb-ui] cache miss title=%016llX",
                  static_cast<unsigned long long>(titleId));
}

SteamGridDbBackdrop::DecodedImage SteamGridDbBackdrop::decodeImage(
    const std::string& path, int outputWidth, int outputHeight, bool fill) {
    auto decoded = steamgriddb::artwork::decode(path, outputWidth, outputHeight, fill);
    DecodedImage out;
    out.rgba = std::move(decoded.rgba);
    out.width = decoded.width;
    out.height = decoded.height;
    return out;
}

float SteamGridDbBackdrop::sampleLuminance(const DecodedImage& image) {
    if (image.rgba.empty() || image.width <= 0 || image.height <= 0)
        return -1.f;
    // Sparse grid (~32×18) — cheap enough for the decode worker thread.
    const int stepX = std::max(1, image.width / 32);
    const int stepY = std::max(1, image.height / 18);
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < image.height; y += stepY) {
        for (int x = 0; x < image.width; x += stepX) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)
                                   + static_cast<std::size_t>(x)) * 4u;
            if (i + 2 >= image.rgba.size())
                continue;
            const float r = image.rgba[i] / 255.f;
            const float g = image.rgba[i + 1] / 255.f;
            const float b = image.rgba[i + 2] / 255.f;
            sum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : -1.f;
}

bool SteamGridDbBackdrop::useFullHeroOpacity() const {
    // Full-opacity heroes are single-row only; folder grid stays dimmed.
    return m_layoutMode == AppLayoutMode::DynamicLine;
}

bool SteamGridDbBackdrop::hasDisplayedHero() const {
    if (!m_enabled || !isVisible())
        return false;
    if (m_artworkOpacity.value() <= 0.05f)
        return false;
    const auto& set = m_sets[m_current];
    return set.hasHero && set.hero.valid();
}

float SteamGridDbBackdrop::displayedHeroLuminance() const {
    if (!hasDisplayedHero())
        return -1.f;
    return m_sets[m_current].heroLuminance;
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
                state->artwork.heroLuminance = sampleLuminance(state->artwork.hero);
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
    m_artworkOpacity.set(1.f, 0.18f, nxui::Easing::outCubic);
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
        const bool fullHero = useFullHeroOpacity();
        const float heroAlpha = fullHero ? 0.94f : 0.24f;
        renderer.pushClipRect(screen);
        renderer.drawTexture(&set.hero, fillRect(set.hero, screen),
                             nxui::Color::white().withAlpha(alpha * heroAlpha));
        renderer.popClipRect();
        // Soft bottom scrim so chrome stays legible over bright/dark heroes.
        if (fullHero) {
            renderer.drawGradientRect(
                screen,
                nxui::Color(0.f, 0.f, 0.f, 0.04f * alpha),
                nxui::Color(0.f, 0.f, 0.f, 0.46f * alpha));
        }
    }

    if (m_layoutMode != AppLayoutMode::DynamicLine) return;

    if (m_showLogo && set.hasLogo && set.logo.valid()) {
        // containRect centres and constrains the logo inside the gutter band.
        renderer.drawTexture(&set.logo, containRect(set.logo, m_logoArea),
                             nxui::Color::white().withAlpha(0.96f * alpha));
    }
}

void SteamGridDbBackdrop::onUpdate(float dt) {
    m_fade.update(std::min(dt, 0.04f));
    m_artworkOpacity.update(std::min(dt, 0.04f));
    if (m_preloadGapSec > 0.f)
        m_preloadGapSec = std::max(0.f, m_preloadGapSec - dt);

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
                m_artworkOpacity.set(0.f, 0.22f, nxui::Easing::outCubic);
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
            if (m_waitForGpuBeforeUpload) {
                // The inactive set may still be referenced by an in-flight
                // crossfade command buffer. Replacing its image storage before
                // the queue drains causes a GPU read page fault.
                DebugLog::log("[steamgriddb-ui] draining GPU before forced backdrop upload title=%016lX",
                              static_cast<unsigned long>(decoded.titleId));
                m_gpu.waitIdle();
#ifdef NXUI_BACKEND_DEKO3D
                m_renderer.reclaimReleasedTextureSlotsAfterIdle();
#endif
                m_waitForGpuBeforeUpload = false;
            }
            target.hasHero = !decoded.hero.rgba.empty()
                && target.hero.loadFromPixels(m_gpu, m_renderer, decoded.hero.rgba.data(),
                                              decoded.hero.width, decoded.hero.height);
            target.heroLuminance = target.hasHero ? decoded.heroLuminance : -1.f;
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
    } else if (!m_pendingDecode && !m_readyArtwork
               && !m_preloadPaused && m_preloadGapSec <= 0.f) {
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
                m_preloadGapSec = m_preloadMinInterval;
                break;
            }
        }
    }
}

void SteamGridDbBackdrop::onRender(nxui::Renderer& renderer) {
    if (!m_enabled || opacity() <= 0.002f) return;
    const float t = std::clamp(m_fade.value(), 0.f, 1.f);
    const float artworkOpacity = std::clamp(m_artworkOpacity.value(), 0.f, 1.f)
                               * opacity();
    if (m_showPreviousDuringCrossfade)
        drawSet(renderer, m_sets[1 - m_current], (1.f - t) * artworkOpacity);
    drawSet(renderer, m_sets[m_current], t * artworkOpacity);
}
