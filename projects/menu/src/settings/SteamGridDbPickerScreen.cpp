#include "SteamGridDbPickerScreen.hpp"

#include "themeshop/ThemeHttp.hpp"

#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {
constexpr int kColumns = 4;
constexpr int kRows = 3;
constexpr int kPerPage = kColumns * kRows;
}

SteamGridDbPickerScreen::SteamGridDbPickerScreen(nxui::GpuDevice& gpu,
                                                 nxui::Renderer& renderer,
                                                 nxui::ThreadPool& threadPool)
    : m_gpu(gpu), m_renderer(renderer), m_threadPool(threadPool) {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setVisible(false);
    setFocusable(true);
    setFrameworkTouchEnabled(false);

    auto addDirection = [this](nxui::Button button, int dx, int dy) {
        addAction(static_cast<std::uint64_t>(button), [this, dx, dy]() {
            moveSelection(dx, dy);
        });
    };
    addDirection(nxui::Button::DLeft, -1, 0);
    addDirection(nxui::Button::LStickL, -1, 0);
    addDirection(nxui::Button::DRight, 1, 0);
    addDirection(nxui::Button::LStickR, 1, 0);
    addDirection(nxui::Button::DUp, 0, -1);
    addDirection(nxui::Button::LStickU, 0, -1);
    addDirection(nxui::Button::DDown, 0, 1);
    addDirection(nxui::Button::LStickD, 0, 1);
    addAction(static_cast<std::uint64_t>(nxui::Button::A), [this]() { activateSelection(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::X), [this]() {
        if (m_searchCb) m_searchCb();
    });
    addAction(static_cast<std::uint64_t>(nxui::Button::B), [this]() { hide(); });
}

void SteamGridDbPickerScreen::showLoading(std::uint64_t titleId,
                                          const std::string& title,
                                          const std::string& query,
                                          SteamGridDbManager::ArtworkKind kind) {
    ++m_generation;
    for (auto& slot : m_slots) {
        if (slot.state) slot.state->cancelled.store(true);
        slot.ready = false;
        slot.failed = false;
    }
    m_result = {};
    m_result.titleId = titleId;
    m_result.title = title;
    m_result.query = query;
    m_result.kind = kind;
    m_selected = 0;
    m_loading = true;
    m_message = "Searching SteamGridDB...";
    m_active = true;
    setVisible(true);
}

void SteamGridDbPickerScreen::setResult(BrowseResult result) {
    m_result = std::move(result);
    m_loading = false;
    m_selected = 0;
    m_message = m_result.success
        ? std::to_string(m_result.candidates.size()) + " choices for " + m_result.gameName
        : m_result.error;
    ++m_generation;
    schedulePreviews();
}

void SteamGridDbPickerScreen::setMessage(const std::string& message, bool loading) {
    m_message = message;
    m_loading = loading;
}

void SteamGridDbPickerScreen::hide() {
    if (!m_active) return;
    m_active = false;
    setVisible(false);
    for (auto& slot : m_slots)
        if (slot.state) slot.state->cancelled.store(true);
    if (m_closedCb) m_closedCb();
}

void SteamGridDbPickerScreen::wait() {
    for (auto& slot : m_slots) {
        if (slot.state) slot.state->cancelled.store(true);
        if (slot.future.valid()) slot.future.wait();
    }
    for (auto& future : m_retiredFutures)
        if (future.valid()) future.wait();
    m_retiredFutures.clear();
}

SteamGridDbPickerScreen::DecodedImage SteamGridDbPickerScreen::decodePreview(
    const std::vector<std::uint8_t>& bytes) {
    DecodedImage out;
    if (bytes.empty()) return out;
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                         &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) stbi_image_free(raw);
        return out;
    }
    // Every preview uses the same small canvas. Slots can therefore reuse
    // their GPU allocation across searches instead of freeing a variable-size
    // texture (which requires a graphics lifetime synchronisation).
    out.width = 240;
    out.height = 112;
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    const float scale = std::min(static_cast<float>(out.width) / width,
                                 static_cast<float>(out.height) / height);
    const int drawnWidth = std::max(1, static_cast<int>(width * scale));
    const int drawnHeight = std::max(1, static_cast<int>(height * scale));
    const int offsetX = (out.width - drawnWidth) / 2;
    const int offsetY = (out.height - drawnHeight) / 2;
    for (int y = 0; y < drawnHeight; ++y) {
        const int sourceY = y * height / drawnHeight;
        for (int x = 0; x < drawnWidth; ++x) {
            const int sourceX = x * width / drawnWidth;
            std::memcpy(out.rgba.data()
                            + (static_cast<std::size_t>(y + offsetY) * out.width
                               + x + offsetX) * 4,
                        raw + (static_cast<std::size_t>(sourceY) * width + sourceX) * 4,
                        4);
        }
    }
    stbi_image_free(raw);
    return out;
}

void SteamGridDbPickerScreen::schedulePreviews() {
    const std::size_t count = std::min(m_result.candidates.size(), m_slots.size());
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        auto& slot = m_slots[i];
        if (slot.state) slot.state->cancelled.store(true);
        if (slot.future.valid())
            m_retiredFutures.push_back(std::move(slot.future));
        slot.state.reset();
        slot.ready = false;
        slot.failed = i >= count;
        if (i >= count) continue;
        slot.candidate = m_result.candidates[i];
    }
}

void SteamGridDbPickerScreen::schedulePreview(std::size_t index) {
    if (index >= m_slots.size()) return;
    auto& slot = m_slots[index];
    if (slot.ready || slot.failed || slot.future.valid() || slot.state) return;
    slot.generation = m_generation;
    slot.state = std::make_shared<DecodeState>();
    auto state = slot.state;
    const std::string url = slot.candidate.thumbnailUrl.empty()
        ? slot.candidate.url : slot.candidate.thumbnailUrl;
    slot.future = m_threadPool.submit([state, url]() {
        try {
            if (!state->cancelled.load())
                state->image = decodePreview(themeshop::http::getBytes(url));
        } catch (...) {
        }
    });
}

void SteamGridDbPickerScreen::onUpdate(float dt) {
    if (!m_active) return;
    m_spinner += dt;

    for (std::size_t i = 0; i < m_retiredFutures.size();) {
        if (m_retiredFutures[i].wait_for(std::chrono::seconds(0))
            != std::future_status::ready) {
            ++i;
            continue;
        }
        try { m_retiredFutures[i].get(); } catch (...) {}
        m_retiredFutures[i] = std::move(m_retiredFutures.back());
        m_retiredFutures.pop_back();
    }

    int uploads = 0;
    for (auto& slot : m_slots) {
        if (uploads >= 1 || slot.ready || slot.failed || !slot.future.valid()) continue;
        if (slot.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        try { slot.future.get(); } catch (...) {}
        if (!slot.state || slot.state->cancelled.load() || slot.generation != m_generation) {
            slot.failed = true;
            continue;
        }
        auto& image = slot.state->image;
        slot.ready = !image.rgba.empty()
            && slot.texture.loadFromPixels(m_gpu, m_renderer, image.rgba.data(),
                                           image.width, image.height);
        slot.failed = !slot.ready;
        image.rgba.clear();
        ++uploads;
    }

    // Network/decode work is intentionally limited. Launching all eighteen
    // previews at once saturated the shared worker pool and caused audible
    // underruns while navigating the gallery.
    int activeJobs = 0;
    for (const auto& slot : m_slots)
        if (slot.future.valid()) ++activeJobs;
    const int count = static_cast<int>(std::min(m_result.candidates.size(), m_slots.size()));
    const int pageStart = count > 0 ? (m_selected / kPerPage) * kPerPage : 0;
    for (int offset = 0; offset < kPerPage && activeJobs < 2; ++offset) {
        const int index = pageStart + offset;
        if (index >= count) break;
        auto& slot = m_slots[(size_t)index];
        if (!slot.ready && !slot.failed && !slot.future.valid() && !slot.state) {
            schedulePreview((size_t)index);
            ++activeJobs;
        }
    }
}

nxui::Rect SteamGridDbPickerScreen::containRect(const nxui::Texture& texture,
                                                const nxui::Rect& area) {
    if (!texture.valid()) return area;
    const float scale = std::min(area.width / texture.width(), area.height / texture.height());
    const float width = texture.width() * scale;
    const float height = texture.height() * scale;
    return {area.x + (area.width - width) * 0.5f,
            area.y + (area.height - height) * 0.5f, width, height};
}

std::string SteamGridDbPickerScreen::kindLabel() const {
    return m_result.kind == SteamGridDbManager::ArtworkKind::Hero ? "Hero"
         : m_result.kind == SteamGridDbManager::ArtworkKind::Logo ? "Logo" : "Icon";
}

void SteamGridDbPickerScreen::onRender(nxui::Renderer& renderer) {
    if (!m_active || !m_theme) return;
    renderer.drawRect(rect(), nxui::Color(0.f, 0.f, 0.f, 0.62f));
    const nxui::Rect panel{70.f, 38.f, 1140.f, 644.f};
    renderer.drawRoundedRect(panel, m_theme->panelBase.withAlpha(0.98f), 28.f);
    renderer.drawRoundedRectOutline(panel, m_theme->panelBorder.withAlpha(0.45f), 28.f, 1.5f);
    if (m_font)
        renderer.drawText("SteamGridDB - " + kindLabel(), {108.f, 68.f}, m_font,
                          m_theme->textPrimary, 1.f);
    if (m_smallFont) {
        renderer.drawText(m_result.title, {108.f, 110.f}, m_smallFont,
                          m_theme->textSecondary, 0.78f);
        renderer.drawText("Search: " + m_result.query + "   [X] Change name",
                          {108.f, 139.f}, m_smallFont, m_theme->textSecondary, 0.72f);
        renderer.drawText(m_message, {108.f, 169.f}, m_smallFont,
                          m_theme->textPrimary, 0.72f);
    }

    if (m_loading) {
        const float pulse = 0.45f + 0.35f * std::sin(m_spinner * 5.f);
        renderer.drawRoundedRect({570.f, 340.f, 140.f, 12.f},
                                 m_theme->cursorNormal.withAlpha(pulse), 6.f);
        return;
    }

    const int count = static_cast<int>(std::min(m_result.candidates.size(), m_slots.size()));
    const int page = count > 0 ? m_selected / kPerPage : 0;
    const int start = page * kPerPage;
    constexpr float cardW = 250.f;
    constexpr float cardH = 130.f;
    constexpr float gapX = 20.f;
    constexpr float gapY = 18.f;
    for (int local = 0; local < kPerPage; ++local) {
        const int index = start + local;
        const nxui::Rect card{105.f + (local % kColumns) * (cardW + gapX),
                              210.f + (local / kColumns) * (cardH + gapY), cardW, cardH};
        m_visibleRects[(size_t)local] = card;
        if (index >= count) continue;
        renderer.drawRoundedRect(card, m_theme->panelBorder.withAlpha(0.22f), 16.f);
        const auto& slot = m_slots[(size_t)index];
        if (slot.ready && slot.texture.valid()) {
            const nxui::Rect imageArea = card.shrunk(8.f);
            renderer.drawTexture(&slot.texture, containRect(slot.texture, imageArea),
                                 nxui::Color::white());
        } else {
            renderer.drawRoundedRect(card.shrunk(12.f),
                                     m_theme->panelHighlight.withAlpha(0.10f), 12.f);
        }
        if (index == m_selected)
            renderer.drawRoundedRectOutline(card.expanded(4.f), m_theme->cursorNormal,
                                            19.f, 4.f);
    }
    if (m_smallFont && count > 0) {
        const int pages = (count + kPerPage - 1) / kPerPage;
        renderer.drawText("A Apply   B Back   X Search   Page " + std::to_string(page + 1)
                          + "/" + std::to_string(pages),
                          {108.f, 651.f}, m_smallFont, m_theme->textSecondary, 0.72f);
    }
}

void SteamGridDbPickerScreen::moveSelection(int dx, int dy) {
    const int count = static_cast<int>(std::min(m_result.candidates.size(), m_slots.size()));
    if (count <= 0 || m_loading) return;
    int target = m_selected + dx + dy * kColumns;
    if (target >= 0 && target < count) m_selected = target;
}

void SteamGridDbPickerScreen::activateSelection() {
    const int count = static_cast<int>(std::min(m_result.candidates.size(), m_slots.size()));
    if (m_loading || m_selected < 0 || m_selected >= count) return;
    m_loading = true;
    m_message = "Applying selection...";
    if (m_applyCb) m_applyCb(m_result, m_result.candidates[(size_t)m_selected]);
}

void SteamGridDbPickerScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_loading || !input.touchUp()) return;
    const int count = static_cast<int>(std::min(m_result.candidates.size(), m_slots.size()));
    const int page = count > 0 ? m_selected / kPerPage : 0;
    for (int local = 0; local < kPerPage; ++local) {
        const int index = page * kPerPage + local;
        if (index >= count || !m_visibleRects[(size_t)local].contains(input.touchX(), input.touchY()))
            continue;
        if (m_selected == index) activateSelection();
        else m_selected = index;
        return;
    }
}
