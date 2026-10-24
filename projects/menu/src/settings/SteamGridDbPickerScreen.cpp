#include "SteamGridDbPickerScreen.hpp"

#include "themeshop/ThemeHttp.hpp"

#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
// Panel ends at y=682; footer hints sit near 651 — six rows fit without overflow.
constexpr int kGameListVisible = 6;
constexpr float kContentTop = 236.f;
constexpr float kGameListClipBottom = 640.f;

std::vector<std::string> wrapLines(nxui::Font* font, const std::string& text,
                                   float maxWidth, float scale) {
    std::vector<std::string> lines;
    if (!font || text.empty() || maxWidth <= 1.f) {
        if (!text.empty()) lines.push_back(text);
        return lines;
    }

    std::string word;
    std::string line;
    auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string candidate = line.empty() ? word : (line + " " + word);
        if (font->measure(candidate).x * scale <= maxWidth || line.empty()) {
            line = candidate;
        } else {
            lines.push_back(line);
            line = word;
        }
        word.clear();
    };

    for (char ch : text) {
        if (ch == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
            continue;
        }
        if (ch == ' ' || ch == '\t') {
            flushWord();
            continue;
        }
        word.push_back(ch);
    }
    flushWord();
    if (!line.empty() || lines.empty())
        lines.push_back(line);
    return lines;
}

bool looksLikeTransportError(const std::string& message) {
    std::string lower = message;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("https failed") != std::string::npos
        || lower.find("couldn't reach steamgriddb") != std::string::npos
        || lower.find("console clock") != std::string::npos
        || lower.find("ssl") != std::string::npos
        || lower.find("certificate") != std::string::npos;
}
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
    addAction(static_cast<std::uint64_t>(nxui::Button::B), [this]() { goBack(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::ZL), [this]() { adjustZoom(-1); });
    addAction(static_cast<std::uint64_t>(nxui::Button::ZR), [this]() { adjustZoom(+1); });
}

void SteamGridDbPickerScreen::adjustZoom(int delta) {
    if (m_loading || m_pickingGame) return;
    const int previous = m_zoomLevel;
    m_zoomLevel = std::clamp(m_zoomLevel + delta, 0, 4);
    if (m_zoomLevel == previous) return;
    // Redecode at the new canvas size so zoom-in stays sharp.
    ++m_generation;
    schedulePreviews();
}

namespace {
constexpr float kPickerPanelX = 70.f;
constexpr float kPickerPanelY = 38.f;
constexpr float kPickerPanelW = 1140.f;
constexpr float kPickerPanelH = 644.f;
constexpr float kPickerGridPadX = 28.f;
constexpr float kPickerGridTop = 220.f;
constexpr float kPickerGridBottomPad = 58.f; // leave room for footer hints

float zoomScale(int level) {
    switch (std::clamp(level, 0, 4)) {
    case 0: return 0.72f;
    case 1: return 0.88f;
    case 2: return 1.00f;
    case 3: return 1.22f;
    default: return 1.48f;
    }
}
} // namespace

void SteamGridDbPickerScreen::previewCanvasSize(int& outW, int& outH) const {
    const float zoom = zoomScale(m_zoomLevel);
    using Kind = SteamGridDbManager::ArtworkKind;
    if (m_result.kind == Kind::Poster) {
        outW = std::max(96, (int)(180.f * zoom));
        outH = std::max(144, (int)(270.f * zoom));
    } else if (m_result.kind == Kind::Icon) {
        outW = outH = std::max(96, (int)(180.f * zoom));
    } else if (m_result.kind == Kind::Logo) {
        outW = std::max(160, (int)(280.f * zoom));
        outH = std::max(48, (int)(80.f * zoom));
    } else {
        outW = std::max(160, (int)(280.f * zoom));
        outH = std::max(90, (int)(158.f * zoom));
    }
}

void SteamGridDbPickerScreen::cardMetrics(float& outW, float& outH, float& outGapX,
                                          float& outGapY, int& outColumns,
                                          int& outRows) const {
    const float zoom = zoomScale(m_zoomLevel);
    using Kind = SteamGridDbManager::ArtworkKind;
    outGapX = 14.f;
    outGapY = 12.f;
    if (m_result.kind == Kind::Poster) {
        outColumns = m_zoomLevel >= 4 ? 2 : (m_zoomLevel >= 3 ? 3 : 4);
        outRows = m_zoomLevel >= 3 ? 1 : 2;
        outW = 168.f * zoom;
        outH = 252.f * zoom;
    } else if (m_result.kind == Kind::Icon) {
        outColumns = m_zoomLevel >= 4 ? 2 : (m_zoomLevel >= 3 ? 3 : 4);
        outRows = m_zoomLevel >= 3 ? 2 : 3;
        outW = outH = 168.f * zoom;
    } else if (m_result.kind == Kind::Logo) {
        outColumns = m_zoomLevel >= 4 ? 2 : (m_zoomLevel >= 3 ? 3 : 4);
        outRows = m_zoomLevel >= 3 ? 2 : 3;
        outW = 250.f * zoom;
        outH = 110.f * zoom;
    } else {
        outColumns = m_zoomLevel >= 4 ? 2 : (m_zoomLevel >= 3 ? 3 : 4);
        outRows = m_zoomLevel >= 3 ? 2 : 3;
        outW = 250.f * zoom;
        outH = 130.f * zoom;
    }

    // Fit inside the panel so zoom never spills past the rounded window.
    const float availW = kPickerPanelW - kPickerGridPadX * 2.f;
    const float availH = (kPickerPanelY + kPickerPanelH - kPickerGridBottomPad) - kPickerGridTop;
    if (outColumns > 1)
        outW = std::min(outW, (availW - outGapX * (outColumns - 1)) / outColumns);
    else
        outW = std::min(outW, availW);
    if (outRows > 1)
        outH = std::min(outH, (availH - outGapY * (outRows - 1)) / outRows);
    else
        outH = std::min(outH, availH);

    if (m_result.kind == Kind::Poster) {
        constexpr float kAspect = 2.f / 3.f; // w/h
        if (outW / std::max(1.f, outH) > kAspect)
            outW = outH * kAspect;
        else
            outH = outW / kAspect;
    } else if (m_result.kind == Kind::Icon) {
        const float side = std::min(outW, outH);
        outW = outH = side;
    }
}

int SteamGridDbPickerScreen::perPage() const {
    float w, h, gx, gy;
    int cols = 4, rows = 3;
    cardMetrics(w, h, gx, gy, cols, rows);
    return std::max(1, cols * rows);
}

void SteamGridDbPickerScreen::showLoading(std::uint64_t titleId,
                                          const std::string& title,
                                          const std::string& query,
                                          SteamGridDbManager::ArtworkKind kind,
                                          bool preserveGameMatches) {
    ++m_generation;
    for (auto& slot : m_slots) {
        if (slot.state) slot.state->cancelled.store(true);
        slot.ready = false;
        slot.failed = false;
    }
    // Keep title/query/kind for the loading header; clear candidates so we do
    // not flash stale artwork while a new search runs.
    const auto keptMatches = m_gameMatches;
    if (!query.empty())
        m_searchQuery = query;
    m_result = {};
    m_result.titleId = titleId;
    m_result.title = title;
    m_result.query = m_searchQuery;
    m_result.kind = kind;
    if (preserveGameMatches && !keptMatches.empty())
        m_gameMatches = keptMatches;
    else
        m_gameMatches.clear();
    m_selected = 0;
    m_loading = true;
    m_pickingGame = false;
    m_message = "Searching SteamGridDB...";
    m_active = true;
    setVisible(true);
}

void SteamGridDbPickerScreen::showGameList(const std::vector<GameMatch>& matches) {
    m_gameMatches = matches;
    m_pickingGame = true;
    m_loading = false;
    m_selected = 0;
    m_result.candidates.clear();
    m_result.gameMatches = matches;
    m_result.query = m_searchQuery;
    m_result.success = !matches.empty();
    m_message = matches.empty()
        ? "No games found"
        : std::to_string(matches.size()) + " games — pick one";
}

void SteamGridDbPickerScreen::setResult(BrowseResult result) {
    const auto previousMatches = m_gameMatches;
    // Never drop the active search string when a browseGame/list payload omits it.
    if (!result.query.empty())
        m_searchQuery = result.query;
    else
        result.query = m_searchQuery;
    m_result = std::move(result);
    m_result.query = m_searchQuery;
    m_loading = false;
    m_selected = 0;
    ++m_generation;

    if (m_result.success && !m_result.gameMatches.empty() && m_result.candidates.empty()) {
        showGameList(m_result.gameMatches);
        return;
    }

    if (!m_result.success && !previousMatches.empty() && m_result.candidates.empty()) {
        // Failed artwork load after a game pick — return to the list with the error.
        showGameList(previousMatches);
        m_message = m_result.error.empty()
            ? nxui::I18n::instance().tr(
                  "settings.steamgriddb.search_failed",
                  "Search failed. If HTTPS keeps failing, check the console clock in Settings > System.")
            : m_result.error;
        return;
    }

    m_pickingGame = false;
    if (!m_result.gameMatches.empty())
        m_gameMatches = m_result.gameMatches;
    else if (!previousMatches.empty())
        m_gameMatches = previousMatches;
    if (m_result.success) {
        m_message = std::to_string(m_result.candidates.size()) + " choices for " + m_result.gameName;
    } else if (m_result.error.empty()) {
        m_message = nxui::I18n::instance().tr(
            "settings.steamgriddb.search_failed",
            "Search failed. If HTTPS keeps failing, check the console clock in Settings > System.");
    } else {
        m_message = m_result.error;
    }
    schedulePreviews();
}

void SteamGridDbPickerScreen::setMessage(const std::string& message, bool loading) {
    m_message = message;
    m_loading = loading;
}

void SteamGridDbPickerScreen::hide() {
    if (!m_active) return;
    m_active = false;
    m_pickingGame = false;
    m_gameMatches.clear();
    m_searchQuery.clear();
    setVisible(false);
    for (auto& slot : m_slots)
        if (slot.state) slot.state->cancelled.store(true);
    if (m_closedCb) m_closedCb();
}

void SteamGridDbPickerScreen::goBack() {
    // From artwork gallery after a game pick, return to the ranked game list.
    if (!m_pickingGame && !m_loading && !m_gameMatches.empty()) {
        for (auto& slot : m_slots)
            if (slot.state) slot.state->cancelled.store(true);
        showGameList(m_gameMatches);
        return;
    }
    hide();
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
    const std::vector<std::uint8_t>& bytes, int maxW, int maxH) {
    DecodedImage out;
    if (bytes.empty() || maxW <= 0 || maxH <= 0) return out;
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                         &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) stbi_image_free(raw);
        return out;
    }
    // Contain into a kind-sized canvas so posters stay portrait and fill the slot.
    out.width = maxW;
    out.height = maxH;
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
    // Always prefer SGDB thumbs for the gallery — full art is only fetched on Apply.
    const std::string url = slot.candidate.thumbnailUrl.empty()
        ? slot.candidate.url
        : slot.candidate.thumbnailUrl;
    int canvasW = 240, canvasH = 112;
    previewCanvasSize(canvasW, canvasH);
    slot.future = m_threadPool.submit([state, url, canvasW, canvasH]() {
        try {
            if (!state->cancelled.load())
                state->image = decodePreview(themeshop::http::getBytes(url),
                                             canvasW, canvasH);
        } catch (...) {
        }
    });
}

void SteamGridDbPickerScreen::onUpdate(float dt) {
    if (!m_active) return;
    m_spinner += dt;
    if (m_pickingGame) return;

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
    const int count = artworkCount();
    const int pageSize = perPage();
    const int pageStart = count > 0 ? (m_selected / pageSize) * pageSize : 0;
    for (int offset = 0; offset < pageSize && activeJobs < 2; ++offset) {
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
         : m_result.kind == SteamGridDbManager::ArtworkKind::Logo ? "Logo"
         : m_result.kind == SteamGridDbManager::ArtworkKind::Poster ? "Poster"
         : "Icon";
}

int SteamGridDbPickerScreen::gameListCount() const {
    return static_cast<int>(m_gameMatches.size());
}

int SteamGridDbPickerScreen::artworkCount() const {
    return static_cast<int>(std::min(m_result.candidates.size(), m_slots.size()));
}

void SteamGridDbPickerScreen::onRender(nxui::Renderer& renderer) {
    if (!m_active || !m_theme) return;
    renderer.drawRect(rect(), nxui::Color(0.f, 0.f, 0.f, 0.62f));
    const nxui::Rect panel{70.f, 38.f, 1140.f, 644.f};
    renderer.drawRoundedRect(panel, m_theme->panelBase.withAlpha(0.98f), 28.f);
    renderer.drawRoundedRectOutline(panel, m_theme->panelBorder.withAlpha(0.45f), 28.f, 1.5f);

    nxui::Font* titleFont = m_font ? m_font : m_smallFont;
    nxui::Font* bodyFont = m_smallFont ? m_smallFont : m_font;
    if (titleFont) {
        renderer.drawText("SteamGridDB - " + kindLabel(), {108.f, 62.f}, titleFont,
                          m_theme->textPrimary, 1.f);
        if (!m_result.title.empty())
            renderer.drawText(m_result.title, {108.f, 104.f}, titleFont,
                              m_theme->textSecondary, 0.82f);
    }
    if (bodyFont) {
        // Keep search controls at near-native small-font size (old 0.72 scale
        // made the query / Change name row almost unreadable on TV).
        const std::string searchLine = "Search  " + m_searchQuery;
        renderer.drawText(searchLine, {108.f, 148.f}, bodyFont,
                          m_theme->textPrimary, 1.f);
        const float searchW = bodyFont->measure(searchLine).x;
        renderer.drawText("X  Change name", {108.f + searchW + 28.f, 148.f}, bodyFont,
                          m_theme->cursorNormal, 1.f);
    }

    const bool showStatusPanel = !m_loading && !m_pickingGame && !m_result.success
        && m_result.candidates.empty() && m_gameMatches.empty() && !m_message.empty();
    if (!showStatusPanel && bodyFont && !m_message.empty() && !m_loading) {
        renderer.drawText(m_message, {108.f, 188.f}, bodyFont,
                          m_theme->textSecondary, 0.95f);
    }

    if (m_loading) {
        const float pulse = 0.45f + 0.35f * std::sin(m_spinner * 5.f);
        renderer.drawRoundedRect({570.f, 340.f, 140.f, 12.f},
                                 m_theme->cursorNormal.withAlpha(pulse), 6.f);
        if (bodyFont && !m_message.empty())
            renderer.drawText(m_message, {108.f, 188.f}, bodyFont,
                              m_theme->textSecondary, 0.95f);
        return;
    }

    if (showStatusPanel) {
        const nxui::Rect status{170.f, 250.f, 940.f, 300.f};
        const bool transport = looksLikeTransportError(m_message);
        const nxui::Color fill = transport
            ? nxui::Color(0.42f, 0.18f, 0.14f, 0.55f)
            : m_theme->panelBorder.withAlpha(0.28f);
        const nxui::Color outline = transport
            ? nxui::Color(0.92f, 0.55f, 0.42f, 0.70f)
            : m_theme->panelBorder.withAlpha(0.45f);
        renderer.drawRoundedRect(status, fill, 22.f);
        renderer.drawRoundedRectOutline(status, outline, 22.f, 2.f);

        const std::string headline = transport
            ? nxui::I18n::instance().tr("settings.steamgriddb.https_clock_title",
                                        "Couldn't reach SteamGridDB")
            : "Search failed";
        float textY = status.y + 36.f;
        if (titleFont) {
            renderer.drawText(headline, {status.x + 40.f, textY}, titleFont,
                              m_theme->textPrimary, 0.95f);
            textY += 52.f;
        }
        if (bodyFont) {
            const auto lines = wrapLines(bodyFont, m_message, status.width - 80.f, 1.05f);
            for (const auto& line : lines) {
                renderer.drawText(line, {status.x + 40.f, textY}, bodyFont,
                                  m_theme->textPrimary, 1.05f);
                textY += 34.f;
            }
            renderer.drawText("B Back   X Try another search",
                              {status.x + 40.f, status.y + status.height - 48.f},
                              bodyFont, m_theme->textSecondary, 0.95f);
        }
        return;
    }

    if (m_pickingGame) {
        const int count = gameListCount();
        const int windowStart = std::max(0, std::min(m_selected - kGameListVisible / 2,
                                                     std::max(0, count - kGameListVisible)));
        constexpr float rowH = 52.f;
        constexpr float rowGap = 8.f;
        const nxui::Rect listClip{
            kPickerPanelX + 12.f,
            kContentTop - 4.f,
            kPickerPanelW - 24.f,
            std::max(0.f, kGameListClipBottom - (kContentTop - 4.f))};
        renderer.pushClipRect(listClip);
        for (int local = 0; local < kGameListVisible; ++local) {
            const int index = windowStart + local;
            const nxui::Rect row{105.f, kContentTop + local * (rowH + rowGap), 1070.f, rowH};
            m_visibleRects[(size_t)local] = row;
            if (index >= count) continue;
            const bool focused = index == m_selected;
            renderer.drawRoundedRect(row,
                m_theme->panelBorder.withAlpha(focused ? 0.34f : 0.18f), 14.f);
            if (focused)
                renderer.drawRoundedRectOutline(row.expanded(3.f), m_theme->cursorNormal,
                                                16.f, 3.5f);
            if (bodyFont) {
                const auto& match = m_gameMatches[(size_t)index];
                renderer.drawText(match.name, {128.f, row.y + 14.f}, bodyFont,
                                  m_theme->textPrimary, 1.f);
            }
        }
        renderer.popClipRect();
        if (bodyFont && count > 0) {
            renderer.drawText("A Choose game   B Back   X Search",
                              {108.f, 651.f}, bodyFont, m_theme->textSecondary, 0.95f);
        }
        return;
    }

    const int count = artworkCount();
    float cardW = 250.f, cardH = 130.f, gapX = 20.f, gapY = 18.f;
    int columns = 4, rows = 3;
    cardMetrics(cardW, cardH, gapX, gapY, columns, rows);
    const int pageSize = std::max(1, columns * rows);
    const int page = count > 0 ? m_selected / pageSize : 0;
    const int start = page * pageSize;
    const float gridW = columns * cardW + (columns - 1) * gapX;
    const float gridH = rows * cardH + (rows - 1) * gapY;
    const float gridLeft = kPickerPanelX + kPickerGridPadX;
    const float gridAvailW = kPickerPanelW - kPickerGridPadX * 2.f;
    const float originX = gridLeft + std::max(0.f, (gridAvailW - gridW) * 0.5f);
    const float originY = kPickerGridTop;
    // Mask the gallery to the panel interior (focus ring included).
    const nxui::Rect clip{
        kPickerPanelX + 12.f,
        kPickerGridTop - 8.f,
        kPickerPanelW - 24.f,
        (kPickerPanelY + kPickerPanelH - kPickerGridBottomPad) - (kPickerGridTop - 8.f)};
    renderer.pushClipRect(clip);
    for (int local = 0; local < pageSize; ++local) {
        const int index = start + local;
        const nxui::Rect card{originX + (local % columns) * (cardW + gapX),
                              originY + (local / columns) * (cardH + gapY), cardW, cardH};
        if (local < (int)m_visibleRects.size())
            m_visibleRects[(size_t)local] = card;
        if (index >= count) continue;
        renderer.drawRoundedRect(card, m_theme->panelBorder.withAlpha(0.22f), 16.f);
        const auto& slot = m_slots[(size_t)index];
        if (slot.ready && slot.texture.valid()) {
            const nxui::Rect imageArea = card.shrunk(6.f);
            renderer.drawTexture(&slot.texture, containRect(slot.texture, imageArea),
                                 nxui::Color::white());
        } else {
            renderer.drawRoundedRect(card.shrunk(12.f),
                                     m_theme->panelHighlight.withAlpha(0.10f), 12.f);
        }
        if (index == m_selected)
            renderer.drawRoundedRectOutline(card.expanded(3.f), m_theme->cursorNormal,
                                            18.f, 3.5f);
    }
    renderer.popClipRect();
    (void)gridH;
    if (bodyFont && count > 0) {
        const int pages = (count + pageSize - 1) / pageSize;
        const char* backHint = m_gameMatches.empty() ? "B Back" : "B Games";
        renderer.drawText(std::string("A Apply   ") + backHint
                          + "   X Search   ZL/ZR Zoom "
                          + std::to_string(m_zoomLevel + 1) + "/5   Page "
                          + std::to_string(page + 1) + "/" + std::to_string(pages),
                          {108.f, 651.f}, bodyFont, m_theme->textSecondary, 0.95f);
    }
}

void SteamGridDbPickerScreen::moveSelection(int dx, int dy) {
    if (m_loading) return;
    if (m_pickingGame) {
        const int count = gameListCount();
        if (count <= 0) return;
        int target = m_selected + dy;
        if (dx != 0) target = m_selected + dx;
        if (target >= 0 && target < count) m_selected = target;
        return;
    }
    const int count = artworkCount();
    if (count <= 0) return;
    float cardW, cardH, gapX, gapY;
    int columns = 4, rows = 3;
    cardMetrics(cardW, cardH, gapX, gapY, columns, rows);
    int target = m_selected + dx + dy * columns;
    if (target >= 0 && target < count) m_selected = target;
}

void SteamGridDbPickerScreen::activateSelection() {
    if (m_loading) return;
    if (m_pickingGame) {
        const int count = gameListCount();
        if (m_selected < 0 || m_selected >= count) return;
        const GameMatch match = m_gameMatches[(size_t)m_selected];
        m_loading = true;
        m_message = "Loading artwork for " + match.name + "...";
        if (m_pickGameCb) m_pickGameCb(m_result, match);
        return;
    }
    const int count = artworkCount();
    if (m_selected < 0 || m_selected >= count) return;
    m_loading = true;
    m_message = "Applying selection...";
    if (m_applyCb) m_applyCb(m_result, m_result.candidates[(size_t)m_selected]);
}

void SteamGridDbPickerScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_loading || !input.touchUp()) return;

    if (m_pickingGame) {
        const int count = gameListCount();
        const int windowStart = std::max(0, std::min(m_selected - kGameListVisible / 2,
                                                     std::max(0, count - kGameListVisible)));
        for (int local = 0; local < kGameListVisible; ++local) {
            const int index = windowStart + local;
            if (index >= count
                || !m_visibleRects[(size_t)local].contains(input.touchX(), input.touchY()))
                continue;
            if (m_selected == index) activateSelection();
            else m_selected = index;
            return;
        }
        return;
    }

    const int count = artworkCount();
    const int pageSize = perPage();
    const int page = count > 0 ? m_selected / pageSize : 0;
    for (int local = 0; local < pageSize; ++local) {
        const int index = page * pageSize + local;
        if (index >= count || local >= (int)m_visibleRects.size()
            || !m_visibleRects[(size_t)local].contains(input.touchX(), input.touchY()))
            continue;
        if (m_selected == index) activateSelection();
        else m_selected = index;
        return;
    }
}
