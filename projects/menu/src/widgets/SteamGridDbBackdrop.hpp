#pragma once

#include "core/AppLayoutMode.hpp"

#include <nxui/core/Animation.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/widgets/Widget.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <vector>

class SteamGridDbBackdrop final : public nxui::Widget {
public:
    SteamGridDbBackdrop(nxui::GpuDevice& gpu, nxui::Renderer& renderer,
                        nxui::ThreadPool* threadPool);

    void setEnabled(bool enabled);
    void setLayoutMode(AppLayoutMode mode) { m_layoutMode = mode; }
    void showTitle(std::uint64_t titleId, bool forceReload = false);

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;

private:
    struct ArtworkSet {
        nxui::Texture hero;
        nxui::Texture logo;
        nxui::Texture grid;
        std::uint64_t titleId = 0;
        bool hasHero = false;
        bool hasLogo = false;
        bool hasGrid = false;
    };

    struct DecodedImage {
        std::vector<std::uint8_t> rgba;
        int width = 0;
        int height = 0;
    };

    struct DecodedArtwork {
        DecodedImage hero;
        DecodedImage logo;
        DecodedImage grid;
        std::uint64_t titleId = 0;
        std::uint64_t generation = 0;
        long decodeMilliseconds = 0;
    };

    struct DecodeState {
        DecodedArtwork artwork;
        std::atomic<bool> cancelled{false};
    };

    struct PendingDecode {
        std::shared_ptr<DecodeState> state;
        std::future<void> future;
    };

    static DecodedImage decodeImage(const std::string& path, int maxSide);
    static nxui::Rect coverRect(const nxui::Texture& texture, const nxui::Rect& area);
    static nxui::Rect containRect(const nxui::Texture& texture, const nxui::Rect& area);
    void drawSet(nxui::Renderer& renderer, const ArtworkSet& set, float alpha) const;
    void startPendingDecode();
    void beginCrossfade(int nextSet, std::uint64_t titleId);

    nxui::GpuDevice& m_gpu;
    nxui::Renderer& m_renderer;
    nxui::ThreadPool* m_threadPool = nullptr;
    std::array<ArtworkSet, 2> m_sets;
    int m_current = 0;
    bool m_enabled = true;
    std::uint64_t m_requestedTitleId = 0;
    std::uint64_t m_requestGeneration = 0;
    std::uint64_t m_appliedGeneration = 0;
    float m_decodeDebounce = 0.f;
    std::optional<PendingDecode> m_pendingDecode;
    std::optional<DecodedArtwork> m_readyArtwork;
    int m_uploadStage = 0;
    AppLayoutMode m_layoutMode = AppLayoutMode::Grid;
    nxui::AnimatedFloat m_fade{1.f};
};
