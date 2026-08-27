#pragma once

#include "steamgriddb/SteamGridDbManager.hpp"

#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/widgets/GlassWidget.hpp>

#include <array>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

class SteamGridDbPickerScreen final : public nxui::GlassWidget {
public:
    using BrowseResult = SteamGridDbManager::BrowseResult;
    using Candidate = SteamGridDbManager::Candidate;

    SteamGridDbPickerScreen(nxui::GpuDevice& gpu, nxui::Renderer& renderer,
                            nxui::ThreadPool& threadPool);

    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void showLoading(std::uint64_t titleId, const std::string& title,
                     const std::string& query, SteamGridDbManager::ArtworkKind kind);
    void setResult(BrowseResult result);
    void setMessage(const std::string& message, bool loading = false);
    void hide();
    void wait();
    bool isActive() const { return m_active; }
    const std::string& query() const { return m_result.query; }
    std::uint64_t titleId() const { return m_result.titleId; }
    SteamGridDbManager::ArtworkKind artworkKind() const { return m_result.kind; }
    const std::string& title() const { return m_result.title; }

    void onClosed(std::function<void()> cb) { m_closedCb = std::move(cb); }
    void onSearch(std::function<void()> cb) { m_searchCb = std::move(cb); }
    void onApply(std::function<void(const BrowseResult&, const Candidate&)> cb) {
        m_applyCb = std::move(cb);
    }
    void handleTouch(nxui::Input& input);

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;

private:
    struct DecodedImage {
        std::vector<std::uint8_t> rgba;
        int width = 0;
        int height = 0;
    };
    struct DecodeState {
        DecodedImage image;
        std::atomic<bool> cancelled{false};
    };
    struct PreviewSlot {
        Candidate candidate;
        nxui::Texture texture;
        std::shared_ptr<DecodeState> state;
        std::future<void> future;
        bool ready = false;
        bool failed = false;
        std::uint64_t generation = 0;
    };

    static DecodedImage decodePreview(const std::vector<std::uint8_t>& bytes);
    static nxui::Rect containRect(const nxui::Texture& texture, const nxui::Rect& area);
    void schedulePreviews();
    void schedulePreview(std::size_t index);
    void moveSelection(int dx, int dy);
    void activateSelection();
    std::string kindLabel() const;

    nxui::GpuDevice& m_gpu;
    nxui::Renderer& m_renderer;
    nxui::ThreadPool& m_threadPool;
    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    BrowseResult m_result;
    std::array<PreviewSlot, 18> m_slots;
    std::vector<std::future<void>> m_retiredFutures;
    std::array<nxui::Rect, 12> m_visibleRects{};
    std::uint64_t m_generation = 0;
    int m_selected = 0;
    bool m_active = false;
    bool m_loading = false;
    std::string m_message;
    float m_spinner = 0.f;
    std::function<void()> m_closedCb;
    std::function<void()> m_searchCb;
    std::function<void(const BrowseResult&, const Candidate&)> m_applyCb;
};
