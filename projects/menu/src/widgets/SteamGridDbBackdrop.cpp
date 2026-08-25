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

void SteamGridDbBackdrop::showTitle(std::uint64_t titleId, bool forceReload) {
    if (!m_enabled) return;
    if (!forceReload && m_requestedTitleId == titleId) return;

    if (!forceReload && titleId != 0) {
        for (int i = 0; i < static_cast<int>(m_sets.size()); ++i) {
            const auto& cached = m_sets[i];
            if (cached.titleId != titleId
                || (!cached.hasHero && !cached.hasGrid && !cached.hasLogo))
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
            return;
        }
    }

    m_requestedTitleId = titleId;
    ++m_requestGeneration;
    m_decodeDebounce = titleId == 0 ? 0.f : 0.065f;
    m_readyArtwork.reset();
    m_uploadStage = 0;
    if (m_pendingDecode && m_pendingDecode->state)
        m_pendingDecode->state->cancelled.store(true);
}

SteamGridDbBackdrop::DecodedImage SteamGridDbBackdrop::decodeImage(
    const std::string& path, int maxSide) {
    DecodedImage out;
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) stbi_image_free(raw);
        return out;
    }

    const bool scaleNeeded = maxSide > 0 && (width > maxSide || height > maxSide);
    if (!scaleNeeded) {
        out.rgba.assign(raw, raw + static_cast<std::size_t>(width) * height * 4);
        out.width = width;
        out.height = height;
        stbi_image_free(raw);
        return out;
    }

    const float scale = std::min(static_cast<float>(maxSide) / width,
                                 static_cast<float>(maxSide) / height);
    const int dstWidth = std::max(1, static_cast<int>(width * scale));
    const int dstHeight = std::max(1, static_cast<int>(height * scale));
    out.rgba.resize(static_cast<std::size_t>(dstWidth) * dstHeight * 4);
    for (int y = 0; y < dstHeight; ++y) {
        const int srcY = y * height / dstHeight;
        for (int x = 0; x < dstWidth; ++x) {
            const int srcX = x * width / dstWidth;
            std::memcpy(out.rgba.data() + (static_cast<std::size_t>(y) * dstWidth + x) * 4,
                        raw + (static_cast<std::size_t>(srcY) * width + srcX) * 4, 4);
        }
    }
    stbi_image_free(raw);
    out.width = dstWidth;
    out.height = dstHeight;
    return out;
}

void SteamGridDbBackdrop::startPendingDecode() {
    auto state = std::make_shared<DecodeState>();
    state->artwork.titleId = m_requestedTitleId;
    state->artwork.generation = m_requestGeneration;
    const std::uint64_t titleId = m_requestedTitleId;

    auto work = [state, titleId]() {
        const auto started = std::chrono::steady_clock::now();
        if (titleId != 0 && SteamGridDbManager::hasArtwork(titleId)) {
            state->artwork.hero = decodeImage(SteamGridDbManager::heroPath(titleId), 1280);
            if (!state->cancelled.load())
                state->artwork.grid = decodeImage(SteamGridDbManager::gridPath(titleId), 600);
            if (!state->cancelled.load())
                state->artwork.logo = decodeImage(SteamGridDbManager::logoPath(titleId), 640);
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

nxui::Rect SteamGridDbBackdrop::coverRect(const nxui::Texture& texture,
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
        renderer.drawTexture(&set.hero, coverRect(set.hero, screen),
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

    if (set.hasGrid && set.grid.valid()) {
        const nxui::Rect cardArea{76.f, 212.f, 176.f, 264.f};
        renderer.drawRoundedRect({cardArea.x - 8.f, cardArea.y - 8.f,
                                  cardArea.width + 16.f, cardArea.height + 16.f},
                                 nxui::Color(0.f, 0.f, 0.f, 0.28f * alpha), 18.f);
        renderer.pushClipRect(cardArea);
        renderer.drawTexture(&set.grid, coverRect(set.grid, cardArea),
                             nxui::Color::white().withAlpha(0.94f * alpha));
        renderer.popClipRect();
    }

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
            && state->artwork.generation == m_requestGeneration) {
            DebugLog::log("[steamgriddb-ui] decoded title=%016llX in %ldms hero=%d grid=%d logo=%d",
                          static_cast<unsigned long long>(state->artwork.titleId),
                          state->artwork.decodeMilliseconds,
                          state->artwork.hero.rgba.empty() ? 0 : 1,
                          state->artwork.grid.rgba.empty() ? 0 : 1,
                          state->artwork.logo.rgba.empty() ? 0 : 1);
            m_readyArtwork = std::move(state->artwork);
            m_uploadStage = 0;
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
            target.hasGrid = !decoded.grid.rgba.empty()
                && target.grid.loadFromPixels(m_gpu, m_renderer, decoded.grid.rgba.data(),
                                              decoded.grid.width, decoded.grid.height);
            decoded.grid.rgba.clear();
        } else if (m_uploadStage == 2) {
            target.hasLogo = !decoded.logo.rgba.empty()
                && target.logo.loadFromPixels(m_gpu, m_renderer, decoded.logo.rgba.data(),
                                              decoded.logo.width, decoded.logo.height);
            decoded.logo.rgba.clear();
        }
        ++m_uploadStage;
        if (m_uploadStage >= 3) {
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
            startPendingDecode();
    }
}

void SteamGridDbBackdrop::onRender(nxui::Renderer& renderer) {
    if (!m_enabled || opacity() <= 0.002f) return;
    const float t = std::clamp(m_fade.value(), 0.f, 1.f);
    drawSet(renderer, m_sets[1 - m_current], (1.f - t) * opacity());
    drawSet(renderer, m_sets[m_current], t * opacity());
}
