#include "ThemeShopScreen.hpp"

#include "ThemeHttp.hpp"
#include "widgets/AdaptiveIconLoad.hpp"

#include "core/DebugLog.hpp"

#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>

#include <curlpp/Easy.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>
#include <switch.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>

namespace {

constexpr int kCommunityPreviewGridCols = 2;
constexpr int kCommunityPreviewVisibleRows = 2;
constexpr int kCommunityPreviewEntriesPerPage = kCommunityPreviewGridCols * kCommunityPreviewVisibleRows;
constexpr int kMaxPreviewUploadsPerFrame = 1;
constexpr int kMaxPreviewDownloadsInFlight = kCommunityPreviewEntriesPerPage;
constexpr int kMaxInstalledPreviewDecodesInFlight = kCommunityPreviewEntriesPerPage;
constexpr int kCommunityPreviewGridMaxSide = 384;   // keep GPU under ~64MB image budget
constexpr int kCommunityPreviewDetailMaxSide = 640;
constexpr int kCommunityPreviewMaxSide = kCommunityPreviewGridMaxSide; // legacy alias
constexpr int kInstalledPreviewCachePages = 3; // current ± 1

constexpr const char* kCommunityPreviewDiskDir = "sdmc:/config/SwitchU/community_preview_cache";

std::string communityPreviewDiskLeaf(const std::string& url) {
    // Stable, filesystem-safe fingerprint of the URL.
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : url) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx.png", static_cast<unsigned long long>(h));
    return buf;
}

std::string communityPreviewDiskPath(const std::string& url) {
    return std::string(kCommunityPreviewDiskDir) + "/" + communityPreviewDiskLeaf(url);
}

bool loadCommunityPreviewDiskCache(const std::string& url, std::vector<std::uint8_t>& outBytes) {
    outBytes.clear();
    if (url.empty())
        return false;
    const std::string path = communityPreviewDiskPath(url);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff n = in.tellg();
    if (n <= 0 || n > 12 * 1024 * 1024)
        return false;
    in.seekg(0, std::ios::beg);
    outBytes.resize(static_cast<std::size_t>(n));
    if (!in.read(reinterpret_cast<char*>(outBytes.data()), n)) {
        outBytes.clear();
        return false;
    }
    return true;
}

void saveCommunityPreviewDiskCache(const std::string& url, const std::vector<std::uint8_t>& bytes) {
    if (url.empty() || bytes.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(kCommunityPreviewDiskDir, ec);
    const std::string path = communityPreviewDiskPath(url);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out)
            return;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
}


bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string trimLeadingSlashes(std::string value) {
    while (!value.empty() && value.front() == '/')
        value.erase(value.begin());
    return value;
}

std::string trimWhitespace(std::string value) {
    auto isSpace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };

    while (!value.empty() && isSpace((unsigned char)value.front()))
        value.erase(value.begin());
    while (!value.empty() && isSpace((unsigned char)value.back()))
        value.pop_back();
    return value;
}

std::string asciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });
    return value;
}

bool containsInsensitive(const std::string& haystack, const std::string& needleLower) {
    if (needleLower.empty())
        return true;
    return asciiLower(haystack).find(needleLower) != std::string::npos;
}

bool matchesSearch(const ThemeShopScreen::ThemeShopEntry& entry, const std::string& needleLower) {
    return needleLower.empty()
        || containsInsensitive(entry.name, needleLower)
        || containsInsensitive(entry.author, needleLower)
        || containsInsensitive(entry.id, needleLower)
        || containsInsensitive(entry.version, needleLower)
        || containsInsensitive(entry.source, needleLower)
        || containsInsensitive(entry.soundPreset, needleLower);
}

bool matchesSearch(const ThemeCatalogClient::Entry& entry, const std::string& needleLower) {
    return needleLower.empty()
        || containsInsensitive(entry.name, needleLower)
        || containsInsensitive(entry.id, needleLower)
        || containsInsensitive(entry.version, needleLower)
        || containsInsensitive(entry.author, needleLower);
}

} // namespace

ThemeShopScreen::ThemeShopScreen()
    : TabbedOverlayScreen(ScreenMode::ThemeShop) {
}

void ThemeShopScreen::setThreadPool(nxui::ThreadPool* pool) {
    m_threadPool = pool;
}

void ThemeShopScreen::setRenderContext(nxui::GpuDevice* gpu, nxui::Renderer* renderer) {
    m_gpu = gpu;
    m_renderer = renderer;
}

void ThemeShopScreen::refreshCommunityCatalog(bool forceNetwork) {
    if (!m_threadPool)
        return;

    // Only bust disk cache on explicit Refresh - auto-open must stay disk-first.
    if (forceNetwork) {
        std::error_code ec;
        std::filesystem::remove("sdmc:/config/SwitchU/community_preview_cache/catalog_index.json", ec);
    }

    m_catalogClient.refresh(*m_threadPool);
    syncCommunityCatalog(m_catalogClient.snapshot());
}

void ThemeShopScreen::setThemeShopState(const std::vector<ThemeShopEntry>& entries,
                                        const std::string& activeId) {
    m_allThemeShopEntries = entries;
    m_installedPreviewCache.clear();
    applySearchFilter();

    auto hasId = [&](const std::string& id) {
        return std::any_of(m_themeShopEntries.begin(), m_themeShopEntries.end(),
                           [&](const ThemeShopEntry& entry) {
                               return entry.id == id;
                           });
    };

    if (m_themeShopEntries.empty()) {
        m_themeShopSelectedId.clear();
        return;
    }

    if (!hasId(m_themeShopSelectedId))
        m_themeShopSelectedId = activeId;
    if (!hasId(m_themeShopSelectedId))
        m_themeShopSelectedId = m_themeShopEntries.front().id;
}

void ThemeShopScreen::setIconPackState(const std::vector<ThemeShopEntry>& entries,
                                       const std::string& activeId) {
    m_allIconPackEntries = entries;
    applySearchFilter();

    auto hasId = [&](const std::string& id) {
        return std::any_of(m_iconPackEntries.begin(), m_iconPackEntries.end(),
                           [&](const ThemeShopEntry& entry) {
                               return entry.id == id;
                           });
    };

    if (m_iconPackEntries.empty()) {
        m_iconPackSelectedId.clear();
        return;
    }

    if (!hasId(m_iconPackSelectedId))
        m_iconPackSelectedId = activeId;
    if (!hasId(m_iconPackSelectedId))
        m_iconPackSelectedId = m_iconPackEntries.front().id;
}

void ThemeShopScreen::setPackageTransferState(const ThemeTransferState& state,
                                              std::string themeId,
                                              bool installMode) {
    m_packageTransferState = state;
    m_packageTransferThemeId = std::move(themeId);
    m_packageTransferInstallMode = installMode;

    if (m_packageTransferState.isRunning()) {
        clearCommunityPreviewCache();
        m_lastPreviewPrimeKey.clear();
    }
}

const ThemeShopScreen::ThemeShopEntry* ThemeShopScreen::selectedThemeShopEntry() const {
    if (isIconsTab()) {
        if (m_iconPackEntries.empty())
            return nullptr;
        for (const auto& entry : m_iconPackEntries) {
            if (entry.id == m_iconPackSelectedId)
                return &entry;
        }
        return &m_iconPackEntries.front();
    }

    if (m_themeShopEntries.empty())
        return nullptr;

    for (const auto& entry : m_themeShopEntries) {
        if (entry.id == m_themeShopSelectedId)
            return &entry;
    }

    return &m_themeShopEntries.front();
}

const ThemeCatalogClient::Entry* ThemeShopScreen::selectedCommunityThemeEntry() const {
    if (m_communityEntries.empty())
        return nullptr;

    for (const auto& entry : m_communityEntries) {
        if (entry.id == m_communitySelectedId)
            return &entry;
    }

    return &m_communityEntries.front();
}

const ThemeCatalogClient::Entry* ThemeShopScreen::findCommunityThemeEntry(const std::string& themeId) const {
    for (const auto& entry : m_allCommunityEntries) {
        if (entry.id == themeId)
            return &entry;
    }
    return nullptr;
}

int ThemeShopScreen::detailScreenshotCount() const {
    if (!isCommunityTab())
        return 0;

    const auto* entry = selectedCommunityThemeEntry();
    if (!entry)
        return 0;

    return (int)entry->screenshots.size();
}

std::string ThemeShopScreen::currentDetailCommunityPreviewPath() const {
    const auto* entry = selectedCommunityThemeEntry();
    if (!entry)
        return {};

    if (!entry->screenshots.empty()) {
        int index = std::clamp(m_detailScreenshotIndex, 0, (int)entry->screenshots.size() - 1);
        return entry->screenshots[(size_t)index];
    }

    return entry->cover;
}

void ThemeShopScreen::clampDetailScreenshotIndex() {
    int count = detailScreenshotCount();
    if (count <= 0) {
        m_detailScreenshotIndex = 0;
        m_detailFullscreen = false;
        if (m_detailFocusArea == DetailFocusArea::Preview)
            m_detailFocusArea = DetailFocusArea::Buttons;
        return;
    }

    m_detailScreenshotIndex = std::clamp(m_detailScreenshotIndex, 0, count - 1);
}

void ThemeShopScreen::stepDetailScreenshot(int delta) {
    int count = detailScreenshotCount();
    if (count <= 0)
        return;

    int nextIndex = std::clamp(m_detailScreenshotIndex + delta, 0, count - 1);
    if (nextIndex == m_detailScreenshotIndex)
        return;

    m_detailScreenshotIndex = nextIndex;
    primeCommunityPreview(currentDetailCommunityPreviewPath());
}

void ThemeShopScreen::applySearchFilter() {
    const std::string needleLower = asciiLower(trimWhitespace(m_searchQuery));

    m_themeShopEntries.clear();
    m_themeShopEntries.reserve(m_allThemeShopEntries.size());
    for (const auto& entry : m_allThemeShopEntries) {
        if (matchesSearch(entry, needleLower))
            m_themeShopEntries.push_back(entry);
    }

    m_communityEntries.clear();
    m_communityEntries.reserve(m_allCommunityEntries.size());
    for (const auto& entry : m_allCommunityEntries) {
        if (matchesSearch(entry, needleLower))
            m_communityEntries.push_back(entry);
    }

    m_iconPackEntries.clear();
    m_iconPackEntries.reserve(m_allIconPackEntries.size());
    for (const auto& entry : m_allIconPackEntries) {
        if (matchesSearch(entry, needleLower))
            m_iconPackEntries.push_back(entry);
    }

    auto syncSelection = [](const auto& entries, std::string& selectedId) {
        auto hasId = [&](const std::string& id) {
            return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
                return entry.id == id;
            });
        };

        if (entries.empty()) {
            selectedId.clear();
            return;
        }

        if (!hasId(selectedId))
            selectedId = entries.front().id;
    };

    syncSelection(m_themeShopEntries, m_themeShopSelectedId);
    syncSelection(m_communityEntries, m_communitySelectedId);
    syncSelection(m_iconPackEntries, m_iconPackSelectedId);

    if (m_themeShopEntries.empty())
        m_installedScrollRow = 0;
    if (m_communityEntries.empty())
        m_communityScrollRow = 0;
    if (m_iconPackEntries.empty())
        m_iconPackScrollRow = 0;
}

bool ThemeShopScreen::promptSearchQuery() {
    if (m_searchRequestCb) m_searchRequestCb(m_searchQuery);
    return true;
}

void ThemeShopScreen::setSearchQuery(std::string query) {
    m_searchQuery = trimWhitespace(query);
    closeDetail();
    applySearchFilter();
    ensureSelectionVisible();
}

bool ThemeShopScreen::pollCommunityCatalog() {
    if (!m_catalogClient.poll())
        return false;

    auto snapshot = m_catalogClient.snapshot();
    if (snapshot.revision == m_communityRevision)
        return false;

    syncCommunityCatalog(snapshot);
    return true;
}

std::string ThemeShopScreen::resolveCommunityPreviewUrl(const ThemeCatalogClient::Entry& entry) const {
    return resolveCommunityPreviewUrl(entry.cover);
}

std::string ThemeShopScreen::resolveCommunityPreviewUrl(const std::string& previewPath) const {
    if (previewPath.empty())
        return {};
    if (startsWith(previewPath, "https://") || startsWith(previewPath, "http://"))
        return previewPath;

    const std::string& catalogUrl = m_catalogClient.catalogUrl();
    std::size_t slash = catalogUrl.find_last_of('/');
    if (slash == std::string::npos)
        return previewPath;
    return catalogUrl.substr(0, slash + 1) + trimLeadingSlashes(previewPath);
}

std::shared_ptr<ThemeShopScreen::PreviewImageState> ThemeShopScreen::communityPreviewState(const std::string& previewPath) const {
    std::string url = resolveCommunityPreviewUrl(previewPath);
    if (url.empty())
        return nullptr;

    auto it = m_communityPreviewCache.find(url);
    if (it == m_communityPreviewCache.end())
        return nullptr;
    return it->second;
}

std::shared_ptr<ThemeShopScreen::PreviewImageState> ThemeShopScreen::communityPreviewState(const ThemeCatalogClient::Entry& entry) const {
    return communityPreviewState(entry.cover);
}

ThemeShopScreen::PreviewPhase ThemeShopScreen::communityPreviewPhase(const std::string& previewPath) const {
    auto state = communityPreviewState(previewPath);
    if (!state)
        return previewPath.empty() ? PreviewPhase::Failed : PreviewPhase::Idle;

    std::lock_guard<std::mutex> lk(state->mutex);
    return state->phase;
}

ThemeShopScreen::PreviewPhase ThemeShopScreen::communityPreviewPhase(const ThemeCatalogClient::Entry& entry) const {
    return communityPreviewPhase(entry.cover);
}

const nxui::Texture* ThemeShopScreen::communityPreviewTexture(const std::string& previewPath) const {
    auto state = communityPreviewState(previewPath);
    if (!state)
        return nullptr;

    std::lock_guard<std::mutex> lk(state->mutex);
    if (state->phase != PreviewPhase::Ready || !state->texture.valid())
        return nullptr;
    return &state->texture;
}

const nxui::Texture* ThemeShopScreen::communityPreviewTexture(const ThemeCatalogClient::Entry& entry) const {
    return communityPreviewTexture(entry.cover);
}

std::string ThemeShopScreen::installedPreviewCacheKey(const std::string& previewPath,
                                                      bool adaptiveLoad,
                                                      const std::string& iconPackId,
                                                      const switchu::icons::HslAdjust* hslOverride) const {
    if (!adaptiveLoad)
        return previewPath;
    // Draft detail scrubbing reuses one slot (reloads in place). Committed grid
    // previews key by HSL so they stay frozen while drafting.
    if (hslOverride)
        return previewPath + "#draft";
    const switchu::icons::HslAdjust hsl = hslAdjustForPack(iconPackId);
    const bool hasPres = hsl.enabled || hsl.flatten || hsl.grayscale;
    const std::uint32_t hslKey = hasPres
        ? ((hsl.enabled ? 1u : 0u) << 26)
          | ((hsl.flatten ? 1u : 0u) << 25)
          | ((hsl.grayscale ? 1u : 0u) << 24)
          | (static_cast<std::uint32_t>(std::lround(hsl.hue * 255.f)) << 16)
          | (static_cast<std::uint32_t>(std::lround(hsl.sat * 255.f)) << 8)
          | static_cast<std::uint32_t>(std::lround(hsl.light * 255.f))
        : 0u;
    return previewPath + "#" + std::to_string(hslKey);
}

ThemeShopScreen::PreviewPhase ThemeShopScreen::installedPreviewPhase(
    const std::string& previewPath,
    bool adaptiveLoad,
    const std::string& iconPackId,
    const switchu::icons::HslAdjust* hslOverride) const {
    if (previewPath.empty())
        return PreviewPhase::Failed;

    auto it = m_installedPreviewCache.find(
        installedPreviewCacheKey(previewPath, adaptiveLoad, iconPackId, hslOverride));
    if (it == m_installedPreviewCache.end() || !it->second)
        return PreviewPhase::Idle;

    std::lock_guard<std::mutex> lk(it->second->mutex);
    return it->second->phase;
}

const nxui::Texture* ThemeShopScreen::installedPreviewTexture(
    const std::string& previewPath,
    bool adaptiveLoad,
    const std::string& iconPackId,
    const switchu::icons::HslAdjust* hslOverride) const {
    if (previewPath.empty())
        return nullptr;

    auto it = m_installedPreviewCache.find(
        installedPreviewCacheKey(previewPath, adaptiveLoad, iconPackId, hslOverride));
    if (it == m_installedPreviewCache.end() || !it->second)
        return nullptr;

    std::lock_guard<std::mutex> lk(it->second->mutex);
    // Stale-while-revalidate: keep showing the last good frame across Idle /
    // Loading / Downloaded so HSL scrubbing never flashes the spinner.
    if (!it->second->texture.valid())
        return nullptr;
    return &it->second->texture;
}

bool ThemeShopScreen::installedPreviewAdaptive(
    const std::string& previewPath,
    bool adaptiveLoad,
    const std::string& iconPackId,
    const switchu::icons::HslAdjust* hslOverride) const {
    if (previewPath.empty())
        return false;

    auto it = m_installedPreviewCache.find(
        installedPreviewCacheKey(previewPath, adaptiveLoad, iconPackId, hslOverride));
    if (it == m_installedPreviewCache.end() || !it->second)
        return false;

    std::lock_guard<std::mutex> lk(it->second->mutex);
    return it->second->adaptive && it->second->phase == PreviewPhase::Ready;
}

const nxui::Texture* ThemeShopScreen::installedPreviewContrastMask(
    const std::string& previewPath,
    bool adaptiveLoad,
    const std::string& iconPackId,
    const switchu::icons::HslAdjust* hslOverride) const {
    if (previewPath.empty())
        return nullptr;

    auto it = m_installedPreviewCache.find(
        installedPreviewCacheKey(previewPath, adaptiveLoad, iconPackId, hslOverride));
    if (it == m_installedPreviewCache.end() || !it->second)
        return nullptr;

    std::lock_guard<std::mutex> lk(it->second->mutex);
    if (it->second->phase != PreviewPhase::Ready || !it->second->hasContrastMask
        || !it->second->contrastMask.valid())
        return nullptr;
    return &it->second->contrastMask;
}

bool ThemeShopScreen::selectedIconPackIsColour() const {
    if (!isIconsTab())
        return false;
    const auto* entry = selectedThemeShopEntry();
    return entry && entry->colourPack;
}

bool ThemeShopScreen::showSelectedIconTintControls() const {
    if (!isIconsTab())
        return false;
    const auto* entry = selectedThemeShopEntry();
    return entry && entry->installed;
}

bool ThemeShopScreen::selectedIconShowsHueSat() const {
    if (!selectedIconCustomTint())
        return false;
    // Grayscale (alone or with Flatten) hides H/S; L stays.
    return !selectedIconPackTint().grayscale;
}

ThemeShopScreen::IconPackTintState ThemeShopScreen::iconPackTintFor(const std::string& packId) const {
    auto it = m_iconPackTints.find(packId.empty() ? "default" : packId);
    if (it != m_iconPackTints.end())
        return it->second;
    return {};
}

ThemeShopScreen::IconPackTintState& ThemeShopScreen::selectedIconPackTint() {
    if (m_draftTintActive)
        return m_draftTint;
    const auto* entry = selectedThemeShopEntry();
    const std::string id = (entry && !entry->id.empty()) ? entry->id : "default";
    return m_iconPackTints[id];
}

const ThemeShopScreen::IconPackTintState& ThemeShopScreen::selectedIconPackTint() const {
    if (m_draftTintActive)
        return m_draftTint;
    const auto* entry = selectedThemeShopEntry();
    const std::string id = (entry && !entry->id.empty()) ? entry->id : "default";
    auto it = m_iconPackTints.find(id);
    if (it != m_iconPackTints.end())
        return it->second;
    static const IconPackTintState kDefault{};
    return kDefault;
}

void ThemeShopScreen::beginIconPackTintDraft() {
    if (!showSelectedIconTintControls()) {
        discardIconPackTintDraft();
        return;
    }
    const auto* entry = selectedThemeShopEntry();
    m_draftTintPackId = (entry && !entry->id.empty()) ? entry->id : "default";
    m_draftTint = iconPackTintFor(m_draftTintPackId);
    m_draftTintActive = true;
    m_tintSliderHoldTime = 0.f;
}

void ThemeShopScreen::discardIconPackTintDraft() {
    // Remember draft (toggle + sliders) in Theme Shop when closing with B.
    // Do not emit to HOME chrome - that still waits for Apply.
    if (m_draftTintActive && !m_draftTintPackId.empty())
        m_iconPackTints[m_draftTintPackId] = m_draftTint;
    m_draftTintActive = false;
    m_draftTintPackId.clear();
    m_draftTint = {};
    m_tintSliderHoldTime = 0.f;
}

void ThemeShopScreen::commitDraftIconPackTint() {
    if (!m_draftTintActive)
        return;
    const std::string id = m_draftTintPackId.empty() ? "default" : m_draftTintPackId;
    m_iconPackTints[id] = m_draftTint;
    emitIconPackTint(id, m_draftTint);
    invalidateIconPackPreviews();
    discardIconPackTintDraft();
}

void ThemeShopScreen::emitIconPackTint(const std::string& packId, const IconPackTintState& tint) {
    if (m_iconPackTintCb)
        m_iconPackTintCb(packId, tint);
}

void ThemeShopScreen::setIconCustomTintEnabled(bool enabled) {
    auto& tint = selectedIconPackTint();
    if (tint.enabled == enabled)
        return;
    tint.enabled = enabled;
    maybeSeedFlattenLightDefault();
}

void ThemeShopScreen::setIconFlattenEnabled(bool enabled) {
    auto& tint = selectedIconPackTint();
    if (tint.flatten == enabled)
        return;
    tint.flatten = enabled;
    maybeSeedFlattenLightDefault();
}

void ThemeShopScreen::setIconGrayscaleEnabled(bool enabled) {
    auto& tint = selectedIconPackTint();
    if (tint.grayscale == enabled)
        return;
    tint.grayscale = enabled;
}

void ThemeShopScreen::maybeSeedFlattenLightDefault() {
    // Flatten + Custom tint: seed L to match theme polarity once (white on dark /
    // black on light). Theme flips leave tinted flatten alone afterward.
    auto& tint = selectedIconPackTint();
    if (!tint.flatten || !tint.enabled)
        return;
    if (std::fabs(tint.light - 0.5f) > 0.0005f)
        return;
    const bool darkTheme = !m_theme || m_theme->mode != nxui::ThemeMode::Light;
    tint.light = darkTheme ? 1.f : 0.f;
}

void ThemeShopScreen::setIconTintHue(float hue01) {
    hue01 = std::clamp(hue01, 0.f, 1.f);
    auto& tint = selectedIconPackTint();
    if (std::fabs(tint.hue - hue01) < 0.0005f)
        return;
    tint.hue = hue01;
}

void ThemeShopScreen::setIconTintSat(float sat01) {
    sat01 = std::clamp(sat01, 0.f, 1.f);
    auto& tint = selectedIconPackTint();
    if (std::fabs(tint.sat - sat01) < 0.0005f)
        return;
    tint.sat = sat01;
}

void ThemeShopScreen::setIconTintLight(float light01) {
    light01 = std::clamp(light01, 0.f, 1.f);
    auto& tint = selectedIconPackTint();
    if (std::fabs(tint.light - light01) < 0.0005f)
        return;
    tint.light = light01;
}

void ThemeShopScreen::nudgeIconTintHue(float delta) {
    float hue = selectedIconPackTint().hue + delta;
    hue -= std::floor(hue);
    setIconTintHue(hue);
}

void ThemeShopScreen::nudgeIconTintSat(float delta) {
    setIconTintSat(selectedIconPackTint().sat + delta);
}

void ThemeShopScreen::nudgeIconTintLight(float delta) {
    setIconTintLight(selectedIconPackTint().light + delta);
}

switchu::icons::HslAdjust ThemeShopScreen::hslAdjustFromTint(const IconPackTintState& tint) const {
    switchu::icons::HslAdjust adj;
    adj.enabled = tint.enabled;
    adj.flatten = tint.flatten;
    adj.grayscale = tint.grayscale;
    adj.hue = std::clamp(tint.hue, 0.f, 1.f);
    adj.sat = std::clamp(tint.sat, 0.f, 1.f);
    adj.light = std::clamp(tint.light, 0.f, 1.f);
    return adj;
}

switchu::icons::HslAdjust ThemeShopScreen::hslAdjustForPack(const std::string& packId) const {
    // Committed tint only - Icons grid + HOME stay frozen while drafting.
    return hslAdjustFromTint(iconPackTintFor(packId));
}

void ThemeShopScreen::updateTintSliderAnalog(float dt) {
    if (!m_input || !m_detailOpen || !selectedIconCustomTint())
        return;
    if (m_detailFocusArea != DetailFocusArea::TintHue
        && m_detailFocusArea != DetailFocusArea::TintSat
        && m_detailFocusArea != DetailFocusArea::TintLight)
        return;

    // Stick only here - D-pad uses discrete steps in handleCustomNavLeft/Right.
    // Stick magnitude = walk→run; hold time adds a light accel curve.
    float axis = m_input->leftStickX();

    constexpr float kDead = 0.16f;
    if (std::fabs(axis) < kDead) {
        m_tintSliderHoldTime = 0.f;
        return;
    }

    m_tintSliderHoldTime += dt;
    const float mag = std::clamp((std::fabs(axis) - kDead) / (1.f - kDead), 0.f, 1.f);
    // Walk→run from stick throw; hold accel ramps slower and caps lower.
    const float speed = 0.14f + mag * mag * 1.40f;
    const float accel = 1.f + std::min(1.55f, m_tintSliderHoldTime * 0.42f);
    const float delta = std::copysign(speed * accel * dt, axis);

    if (m_detailFocusArea == DetailFocusArea::TintHue)
        nudgeIconTintHue(delta);
    else if (m_detailFocusArea == DetailFocusArea::TintSat)
        nudgeIconTintSat(delta);
    else
        nudgeIconTintLight(delta);
}

void ThemeShopScreen::invalidateIconPackPreviews() {
    for (auto& entry : m_installedPreviewCache) {
        if (!entry.second)
            continue;
        std::lock_guard<std::mutex> lk(entry.second->mutex);
        if (!entry.second->adaptiveRequested)
            continue;
        entry.second->phase = PreviewPhase::Idle;
        entry.second->hslKey = 0;
        entry.second->adaptive = false;
        entry.second->hasContrastMask = false;
        entry.second->texture = nxui::Texture();
        entry.second->contrastMask = nxui::Texture();
    }
}

void ThemeShopScreen::pausePreviewWorkForSleep() {
    // Cancel workers only — do not destroy textures (Texture dtor waitIdle can
    // wedge the GPU path if sleep/wake is already delicate).
    for (auto& entry : m_installedPreviewCache) {
        if (!entry.second)
            continue;
        std::lock_guard<std::mutex> lk(entry.second->mutex);
        entry.second->cancelled = true;
        entry.second->pixels.clear();
        entry.second->maskPixels.clear();
        if (entry.second->phase == PreviewPhase::Loading
            || entry.second->phase == PreviewPhase::Downloaded)
            entry.second->phase = entry.second->texture.valid()
                ? PreviewPhase::Ready : PreviewPhase::Idle;
    }
    for (auto& entry : m_communityPreviewCache) {
        if (!entry.second)
            continue;
        std::lock_guard<std::mutex> lk(entry.second->mutex);
        entry.second->cancelled = true;
        entry.second->bytes.clear();
        if (entry.second->phase == PreviewPhase::Loading
            || entry.second->phase == PreviewPhase::Downloaded)
            entry.second->phase = entry.second->texture.valid()
                ? PreviewPhase::Ready : PreviewPhase::Idle;
    }
    m_hasPendingInstalledPreviewWork = false;
    m_hasPendingCommunityPreviewWork = false;
}

void ThemeShopScreen::primeInstalledPreview(const std::string& previewPath,
                                            bool adaptiveLoad,
                                            const std::string& iconPackId,
                                            const switchu::icons::HslAdjust* hslOverride,
                                            int maxSide) {
    if (!m_gpu || !m_renderer || previewPath.empty())
        return;

    const int decodeSide = maxSide > 0 ? maxSide : kCommunityPreviewGridMaxSide;

    const switchu::icons::HslAdjust hsl = adaptiveLoad
        ? (hslOverride ? *hslOverride : hslAdjustForPack(iconPackId))
        : switchu::icons::HslAdjust{};
    const bool hasPres = hsl.enabled || hsl.flatten || hsl.grayscale;
    const std::uint32_t hslKey = adaptiveLoad && hasPres
        ? ((hsl.enabled ? 1u : 0u) << 26)
          | ((hsl.flatten ? 1u : 0u) << 25)
          | ((hsl.grayscale ? 1u : 0u) << 24)
          | (static_cast<std::uint32_t>(std::lround(hsl.hue * 255.f)) << 16)
          | (static_cast<std::uint32_t>(std::lround(hsl.sat * 255.f)) << 8)
          | static_cast<std::uint32_t>(std::lround(hsl.light * 255.f))
        : 0u;

    // Key by path + HSL so draft detail previews don't overwrite grid cards.
    const std::string cacheKey =
        installedPreviewCacheKey(previewPath, adaptiveLoad, iconPackId, hslOverride);
    auto& slot = m_installedPreviewCache[cacheKey];
    if (!slot)
        slot = std::make_shared<PreviewImageState>();

    {
        std::lock_guard<std::mutex> lk(slot->mutex);
        const bool identityChanged = slot->url != previewPath
            || slot->adaptiveRequested != adaptiveLoad
            || slot->hslKey != hslKey;
        // Never downgrade a Ready/in-flight higher-res decode (grid 512 after
        // fullscreen 720 was thrashing waitIdle every open/close).
        const bool needsUpgrade = !identityChanged
            && decodeSide > slot->maxSide
            && (slot->phase == PreviewPhase::Ready
                || slot->phase == PreviewPhase::Failed
                || slot->phase == PreviewPhase::Idle);
        if (identityChanged || needsUpgrade) {
            // Draft HSL scrubbing: keep the last texture on screen so the
            // preview does not flash a loading spinner between slider ticks.
            const bool keepStaleDraft = hslOverride != nullptr && slot->texture.valid();
            // Coalesce: if a draft decode is already running, cancel it and wait
            // for Idle/Ready. Next frame primes once with the latest HSL — not
            // dozens of overlapping decodes per second while scrubbing.
            if (hslOverride
                && (slot->phase == PreviewPhase::Loading
                    || slot->phase == PreviewPhase::Downloaded)) {
                slot->cancelled = true;
                slot->maxSide = std::max(slot->maxSide, decodeSide);
                return;
            }
            slot->cancelled = true; // drop any in-flight decode for this slot
            slot->url = previewPath;
            slot->adaptiveRequested = adaptiveLoad;
            slot->hslKey = hslKey;
            slot->maxSide = decodeSide;
            slot->phase = PreviewPhase::Idle;
            slot->failureCount = 0;
            slot->adaptive = keepStaleDraft ? slot->adaptive : false;
            slot->hasContrastMask = false;
            slot->pixelW = 0;
            slot->pixelH = 0;
            slot->pixels.clear();
            slot->maskPixels.clear();
            slot->future = std::future<void>();
            slot->cancelled = false;
            if (!keepStaleDraft) {
                slot->texture = nxui::Texture();
                slot->contrastMask = nxui::Texture();
            }
        } else if (slot->maxSide < decodeSide) {
            slot->maxSide = decodeSide;
        }
        if (slot->phase == PreviewPhase::Ready || slot->phase == PreviewPhase::Failed
            || slot->phase == PreviewPhase::Loading || slot->phase == PreviewPhase::Downloaded)
            return;
    }

    // Prefer async decode so page flips don't hitch the UI thread.
    if (m_threadPool) {
        int inFlight = 0;
        for (const auto& entry : m_installedPreviewCache) {
            if (!entry.second) continue;
            std::lock_guard<std::mutex> lk(entry.second->mutex);
            if (entry.second->phase == PreviewPhase::Loading
                || entry.second->phase == PreviewPhase::Downloaded)
                ++inFlight;
        }
        if (inFlight >= kMaxInstalledPreviewDecodesInFlight)
            return;

        bool shouldStart = false;
        {
            std::lock_guard<std::mutex> lk(slot->mutex);
            if (slot->phase == PreviewPhase::Idle) {
                slot->phase = PreviewPhase::Loading;
                shouldStart = true;
            }
        }
        if (!shouldStart)
            return;

        m_hasPendingInstalledPreviewWork = true;
        DebugLog::log("[themeshop] installed preview decode start: %s side=%d", previewPath.c_str(), decodeSide);
        const std::string pathCopy = previewPath;
        const bool adaptiveCopy = adaptiveLoad;
        const switchu::icons::HslAdjust hslCopy = hsl;
        const bool hasPresCopy = hasPres;
        const int sideCopy = decodeSide;
        const std::uint32_t expectedHslKey = hslKey;
        slot->future = m_threadPool->submit([slot, pathCopy, adaptiveCopy, hslCopy, hasPresCopy,
                                             sideCopy, expectedHslKey]() {
            try {
                {
                    std::lock_guard<std::mutex> lk(slot->mutex);
                    if (slot->cancelled || slot->hslKey != expectedHslKey) {
                        // Must leave Loading or scrub cancel + sleep leave the
                        // slot stuck and every later prime coalesces forever.
                        if (slot->phase == PreviewPhase::Loading)
                            slot->phase = slot->texture.valid()
                                ? PreviewPhase::Ready : PreviewPhase::Idle;
                        return;
                    }
                }

                bool ok = false;
                bool adaptive = false;
                int pw = 0, ph = 0;
                std::vector<std::uint8_t> pixels;
                std::vector<std::uint8_t> maskPixels;

                if (adaptiveCopy) {
                    switchu::icons::DecodedAdaptiveIcon decoded;
                    ok = switchu::icons::decodeMaybeAdaptiveIcon(
                        pathCopy, decoded, sideCopy,
                        hasPresCopy ? &hslCopy : nullptr,
                        /*wantContrastMask=*/true);
                    if (ok) {
                        adaptive = decoded.adaptive;
                        pw = decoded.w;
                        ph = decoded.h;
                        pixels = std::move(decoded.rgba);
                        maskPixels = std::move(decoded.contrastMask);
                    }
                } else {
                    switchu::icons::DecodedAdaptiveIcon decoded;
                    // Reuse decoder without adaptive/HSL - still gets maxSide downscale.
                    ok = switchu::icons::decodeMaybeAdaptiveIcon(
                        pathCopy, decoded, sideCopy, nullptr, false);
                    if (ok) {
                        pw = decoded.w;
                        ph = decoded.h;
                        pixels = std::move(decoded.rgba);
                    }
                }

                std::lock_guard<std::mutex> lk(slot->mutex);
                if (slot->cancelled || slot->hslKey != expectedHslKey) {
                    slot->pixels.clear();
                    slot->maskPixels.clear();
                    // Superseded draft scrub / sleep cancel: free the slot so the
                    // next prime can start with the latest HSL key.
                    if (slot->phase == PreviewPhase::Loading)
                        slot->phase = slot->texture.valid() ? PreviewPhase::Ready
                                                           : PreviewPhase::Idle;
                    return;
                }
                if (!ok || pixels.empty()) {
                    slot->failureCount += 1;
                    slot->phase = PreviewPhase::Failed;
                    slot->pixels.clear();
                    slot->maskPixels.clear();
                    return;
                }
                slot->adaptive = adaptive;
                slot->hasContrastMask = !maskPixels.empty();
                slot->pixelW = pw;
                slot->pixelH = ph;
                slot->pixels = std::move(pixels);
                slot->maskPixels = std::move(maskPixels);
                slot->failureCount = 0;
                slot->phase = PreviewPhase::Downloaded;
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lk(slot->mutex);
                if (slot->cancelled) {
                    if (slot->phase == PreviewPhase::Loading)
                        slot->phase = slot->texture.valid()
                            ? PreviewPhase::Ready : PreviewPhase::Idle;
                    return;
                }
                slot->pixels.clear();
                slot->maskPixels.clear();
                slot->failureCount += 1;
                slot->phase = PreviewPhase::Failed;
                DebugLog::log("[themeshop] installed preview decode failed: %s", ex.what());
            } catch (...) {
                std::lock_guard<std::mutex> lk(slot->mutex);
                if (slot->cancelled) {
                    if (slot->phase == PreviewPhase::Loading)
                        slot->phase = slot->texture.valid()
                            ? PreviewPhase::Ready : PreviewPhase::Idle;
                    return;
                }
                slot->pixels.clear();
                slot->maskPixels.clear();
                slot->failureCount += 1;
                slot->phase = PreviewPhase::Failed;
            }
        });
        return;
    }

    // No thread pool - keep previous synchronous path.
    {
        std::lock_guard<std::mutex> lk(slot->mutex);
        slot->phase = PreviewPhase::Loading;
    }

    nxui::Texture uploaded;
    nxui::Texture contrastMask;
    bool adaptive = false;
    bool ok = false;
    if (adaptiveLoad) {
        ok = switchu::icons::loadMaybeAdaptiveIcon(uploaded, *m_gpu, *m_renderer,
                                                   previewPath, &adaptive,
                                                   decodeSide,
                                                   nullptr,
                                                   &contrastMask,
                                                   hasPres ? &hsl : nullptr);
    } else {
        ok = uploaded.loadFromFile(*m_gpu, *m_renderer, previewPath, decodeSide);
    }

    std::lock_guard<std::mutex> lk(slot->mutex);
    if (ok) {
        slot->texture = std::move(uploaded);
        slot->adaptive = adaptive;
        slot->hasContrastMask = contrastMask.valid();
        slot->contrastMask = std::move(contrastMask);
        slot->phase = PreviewPhase::Ready;
        slot->failureCount = 0;
    } else {
        slot->adaptive = false;
        slot->hasContrastMask = false;
        slot->phase = PreviewPhase::Failed;
        slot->failureCount += 1;
    }
}

void ThemeShopScreen::syncFinishedInstalledPreviewLoads() {
    if (!m_gpu || !m_renderer || !m_hasPendingInstalledPreviewWork)
        return;

    bool hasPendingWork = false;
    int uploadsThisFrame = 0;
    for (auto& entry : m_installedPreviewCache) {
        auto state = entry.second;
        if (!state)
            continue;

        if (state->future.valid()) {
            if (state->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    state->future.get();
                } catch (const std::exception& ex) {
                    std::lock_guard<std::mutex> lk(state->mutex);
                    state->phase = PreviewPhase::Failed;
                    state->failureCount += 1;
                    state->pixels.clear();
                    state->maskPixels.clear();
                    DebugLog::log("[themeshop] installed preview task failed: %s", ex.what());
                } catch (...) {
                    std::lock_guard<std::mutex> lk(state->mutex);
                    state->phase = PreviewPhase::Failed;
                    state->failureCount += 1;
                    state->pixels.clear();
                    state->maskPixels.clear();
                }
            } else {
                hasPendingWork = true;
            }
        }

        std::vector<std::uint8_t> pixels;
        std::vector<std::uint8_t> maskPixels;
        int pw = 0, ph = 0;
        bool adaptive = false;
        bool hasMask = false;
        {
            std::lock_guard<std::mutex> lk(state->mutex);
            if (state->phase == PreviewPhase::Downloaded) {
                // Scrub cancel: drop superseded pixels; keep last texture on screen.
                if (state->cancelled) {
                    state->pixels.clear();
                    state->maskPixels.clear();
                    state->phase = state->texture.valid() ? PreviewPhase::Ready
                                                         : PreviewPhase::Idle;
                } else if (uploadsThisFrame >= kMaxPreviewUploadsPerFrame) {
                    hasPendingWork = true;
                } else {
                    pixels = std::move(state->pixels);
                    maskPixels = std::move(state->maskPixels);
                    pw = state->pixelW;
                    ph = state->pixelH;
                    adaptive = state->adaptive;
                    hasMask = state->hasContrastMask;
                    state->pixels.clear();
                    state->maskPixels.clear();
                }
            } else if (state->phase == PreviewPhase::Loading) {
                hasPendingWork = true;
            }
        }

        if (pixels.empty() || pw <= 0 || ph <= 0)
            continue;

        ++uploadsThisFrame;
        nxui::Texture uploaded;
        nxui::Texture contrastMask;
        const bool ok = uploaded.loadFromPixels(*m_gpu, *m_renderer, pixels.data(), pw, ph);
        if (ok && hasMask && !maskPixels.empty())
            contrastMask.loadFromPixels(*m_gpu, *m_renderer, maskPixels.data(), pw, ph);

        std::lock_guard<std::mutex> lk(state->mutex);
        if (ok) {
            state->texture = std::move(uploaded);
            state->contrastMask = std::move(contrastMask);
            state->adaptive = adaptive;
            state->hasContrastMask = contrastMask.valid();
            state->failureCount = 0;
            state->phase = PreviewPhase::Ready;
        } else {
            state->failureCount = 8;
            state->phase = PreviewPhase::Failed;
            DebugLog::log("[themeshop] installed preview upload skipped (GPU budget): %dx%d", pw, ph);
        }
    }

    m_hasPendingInstalledPreviewWork = hasPendingWork;
}

void ThemeShopScreen::trimInstalledPreviewCache() {
    if (m_installedPreviewCache.empty())
        return;

    // Keep current page ±1 and the open detail key; drop the rest without waitIdle.
    std::unordered_set<std::string> retain;
    const bool icons = isIconsTab();
    const auto& entries = icons ? m_iconPackEntries : m_themeShopEntries;
    const int scroll = icons ? m_iconPackScrollRow : m_installedScrollRow;
    const int start = std::max(0, (scroll - 1) * kCommunityPreviewGridCols);
    const int end = std::min((int)entries.size(),
                             (scroll + kInstalledPreviewCachePages) * kCommunityPreviewGridCols);
    for (int i = start; i < end; ++i) {
        const auto& e = entries[(size_t)i];
        if (e.coverPath.empty()) continue;
        const std::string packId = icons ? e.id : std::string{};
        retain.insert(installedPreviewCacheKey(e.coverPath, icons, packId, nullptr));
    }
    if (m_detailOpen) {
        const auto* sel = selectedThemeShopEntry();
        if (sel && !sel->coverPath.empty()) {
            const std::string packId = icons ? sel->id : std::string{};
            if (icons && showSelectedIconTintControls()) {
                // Always keep the #draft slot while tint controls are open —
                // including when every toggle is off (identity / original look).
                auto draft = hslAdjustFromTint(selectedIconPackTint());
                retain.insert(installedPreviewCacheKey(sel->coverPath, true, packId, &draft));
            }
            retain.insert(installedPreviewCacheKey(sel->coverPath, icons, packId, nullptr));
        }
    }

    constexpr int kMaxDropsPerCall = 2;
    int erased = 0;
    for (auto it = m_installedPreviewCache.begin();
         it != m_installedPreviewCache.end() && erased < kMaxDropsPerCall; ) {
        if (retain.find(it->first) != retain.end()) {
            ++it;
            continue;
        }
        if (it->second) {
            std::lock_guard<std::mutex> lk(it->second->mutex);
            // Don't drop in-flight decodes mid-flight - let them finish then GC later.
            if (it->second->phase == PreviewPhase::Loading
                || it->second->phase == PreviewPhase::Downloaded) {
                ++it;
                continue;
            }
            it->second->cancelled = true;
            it->second->pixels.clear();
            it->second->maskPixels.clear();
            it->second->texture = nxui::Texture();
            it->second->contrastMask = nxui::Texture();
        }
        it = m_installedPreviewCache.erase(it);
        ++erased;
    }
}

void ThemeShopScreen::primeCommunityPreview(const std::string& previewPath) {
    if (!m_threadPool)
        return;

    constexpr int kMaxPreviewFailureCount = 2;

    std::string url = resolveCommunityPreviewUrl(previewPath);
    if (url.empty())
        return;

    auto& slot = m_communityPreviewCache[url];
    if (!slot)
        slot = std::make_shared<PreviewImageState>();

    bool canStart = false;
    {
        std::lock_guard<std::mutex> lk(slot->mutex);
        if (slot->url != url) {
            slot->phase = PreviewPhase::Idle;
            slot->failureCount = 0;
            slot->cancelled = false;
            slot->url = url;
            slot->bytes.clear();
            slot->future = std::future<void>();
            slot->texture = nxui::Texture();
        } else {
            slot->cancelled = false;
        }

        if (slot->phase == PreviewPhase::Idle
            || (slot->phase == PreviewPhase::Failed && slot->failureCount < kMaxPreviewFailureCount)) {
            canStart = true;
        }
    }

    if (!canStart)
        return;

    int inFlightDownloads = 0;
    for (const auto& entry : m_communityPreviewCache) {
        const auto& state = entry.second;
        if (!state)
            continue;

        std::lock_guard<std::mutex> lk(state->mutex);
        if (state->phase == PreviewPhase::Loading || state->phase == PreviewPhase::Downloaded)
            ++inFlightDownloads;
    }

    if (inFlightDownloads >= kMaxPreviewDownloadsInFlight)
        return;

    bool shouldStart = false;
    {
        std::lock_guard<std::mutex> lk(slot->mutex);
        if (slot->phase == PreviewPhase::Idle
            || (slot->phase == PreviewPhase::Failed && slot->failureCount < kMaxPreviewFailureCount)) {
            slot->phase = PreviewPhase::Loading;
            shouldStart = true;
        }
    }

    if (!shouldStart)
        return;

    m_hasPendingCommunityPreviewWork = true;
    slot->future = m_threadPool->submit([slot, url]() {
        try {
            {
                std::lock_guard<std::mutex> lk(slot->mutex);
                if (slot->cancelled)
                    return;
            }

            std::vector<std::uint8_t> bytes;
            if (!loadCommunityPreviewDiskCache(url, bytes)) {
                bytes = themeshop::http::getBytes(url);
                if (!bytes.empty())
                    saveCommunityPreviewDiskCache(url, bytes);
            }
            std::lock_guard<std::mutex> lk(slot->mutex);
            if (slot->cancelled) {
                slot->bytes.clear();
                return;
            }
            slot->bytes = std::move(bytes);
            if (slot->bytes.empty()) {
                slot->failureCount += 1;
                slot->phase = PreviewPhase::Failed;
            } else {
                slot->failureCount = 0;
                slot->phase = PreviewPhase::Downloaded;
            }
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lk(slot->mutex);
            if (slot->cancelled)
                return;
            slot->bytes.clear();
            slot->failureCount += 1;
            slot->phase = PreviewPhase::Failed;
            DebugLog::log("[themeshop] preview fetch failed: %s", ex.what());
        } catch (...) {
            std::lock_guard<std::mutex> lk(slot->mutex);
            if (slot->cancelled)
                return;
            slot->bytes.clear();
            slot->failureCount += 1;
            slot->phase = PreviewPhase::Failed;
            DebugLog::log("[themeshop] preview fetch failed: unknown error");
        }
    });
}

void ThemeShopScreen::primeCommunityPreview(const ThemeCatalogClient::Entry& entry) {
    primeCommunityPreview(entry.cover);
}

void ThemeShopScreen::primeVisibleCommunityPreviews() {
    if (!isCommunityTab() || m_communityEntries.empty())
        return;

    std::unordered_set<std::string> queuedUrls;
    auto queuePreview = [&](const std::string& previewPath) {
        std::string url = resolveCommunityPreviewUrl(previewPath);
        if (url.empty() || !queuedUrls.emplace(url).second)
            return;
        primeCommunityPreview(previewPath);
    };

    const int scrollRow = m_communityScrollRow;
    const int pageStart = scrollRow * kCommunityPreviewGridCols;
    const int pageEnd = std::min((int)m_communityEntries.size(),
                                 pageStart + kCommunityPreviewEntriesPerPage);

    // 1) Visible page covers first - never starve these for prefetch.
    for (int i = pageStart; i < pageEnd; ++i)
        queuePreview(m_communityEntries[(size_t)i].cover);

    const auto* selected = selectedCommunityThemeEntry();
    if (selected) {
        if (m_detailOpen) {
            std::string current = currentDetailCommunityPreviewPath();
            if (!current.empty())
                queuePreview(current);
        } else if (!selected->cover.empty()) {
            queuePreview(selected->cover);
        }
    }

    // 2) Neighbor pages only after the visible set is queued.
    const int preStart = std::max(0, (scrollRow - 1) * kCommunityPreviewGridCols);
    const int preEnd = std::min((int)m_communityEntries.size(),
                                (scrollRow + 2) * kCommunityPreviewGridCols);
    for (int i = preStart; i < preEnd; ++i)
        queuePreview(m_communityEntries[(size_t)i].cover);

    // 3) Detail screenshots last (and only while detail is open).
    if (selected && m_detailOpen) {
        for (const auto& shot : selected->screenshots)
            queuePreview(shot);
    }
}

void ThemeShopScreen::syncFinishedCommunityPreviewLoads() {
    if (!m_gpu || !m_renderer || !m_hasPendingCommunityPreviewWork)
        return;

    bool hasPendingWork = false;
    int uploadsThisFrame = 0;
    for (auto& entry : m_communityPreviewCache) {
        auto state = entry.second;
        if (!state)
            continue;

        if (state->future.valid()) {
            if (state->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    state->future.get();
                } catch (const std::exception& ex) {
                    std::lock_guard<std::mutex> lk(state->mutex);
                    state->phase = PreviewPhase::Failed;
                    state->failureCount += 1;
                    state->bytes.clear();
                    DebugLog::log("[themeshop] preview task failed: %s", ex.what());
                } catch (...) {
                    std::lock_guard<std::mutex> lk(state->mutex);
                    state->phase = PreviewPhase::Failed;
                    state->failureCount += 1;
                    state->bytes.clear();
                    DebugLog::log("[themeshop] preview task failed: unknown error");
                }
            } else {
                hasPendingWork = true;
            }
        }

        std::vector<std::uint8_t> bytes;
        {
            std::lock_guard<std::mutex> lk(state->mutex);
            if (state->phase == PreviewPhase::Downloaded) {
                if (uploadsThisFrame >= kMaxPreviewUploadsPerFrame) {
                    hasPendingWork = true;
                } else {
                    bytes = std::move(state->bytes);
                }
            }
        }

        if (bytes.empty())
            continue;

        ++uploadsThisFrame;
        // Grid-sized uploads only - detail 960px textures blew the 64MB GPU image budget.
        nxui::Texture uploaded;
        bool ok = uploaded.loadFromMemory(*m_gpu, *m_renderer, bytes.data(), bytes.size(), kCommunityPreviewGridMaxSide);
        std::lock_guard<std::mutex> lk(state->mutex);
        if (ok) {
            state->bytes.clear();
            state->texture = std::move(uploaded);
            state->failureCount = 0;
            state->phase = PreviewPhase::Ready;
        } else {
            // Budget miss: keep bytes on SD cache path, do not re-download storm.
            state->bytes = std::move(bytes);
            state->failureCount = 8;
            state->phase = PreviewPhase::Failed;
            DebugLog::log("[themeshop] community preview upload skipped (GPU budget)");
        }
    }

    m_hasPendingCommunityPreviewWork = hasPendingWork;
}

void ThemeShopScreen::clearCommunityPreviewCache() {
    if (m_communityPreviewCache.empty())
        return;

    // Avoid device-wide waitIdle on tab hide - cancel + drop textures instead.
    for (auto& entry : m_communityPreviewCache) {
        auto& state = entry.second;
        if (!state)
            continue;

        std::lock_guard<std::mutex> lk(state->mutex);
        state->cancelled = true;
        state->bytes.clear();
        state->texture = nxui::Texture();
    }

    m_communityPreviewCache.clear();
    m_hasPendingCommunityPreviewWork = false;
}

void ThemeShopScreen::trimCommunityPreviewCache() {
    if (m_communityPreviewCache.empty())
        return;

    // Soft LRU: keep a wide Ready set on Community so page flips don't
    // waitIdle-thrash. Away from Community, drip-release (1-2/frame) so tab
    // cycling stays smooth instead of nuking the whole cache at once.
    const std::size_t kMaxReadyCommunityPreviews = isCommunityTab() ? 28u : 0u;
    // Texture::~Texture waitIdle's - never drop more than a couple per tick.
    const int kMaxDropsPerCall = isCommunityTab() ? 2 : 2;

    std::unordered_set<std::string> preferred;
    auto prefer = [&](const std::string& previewPath) {
        std::string url = resolveCommunityPreviewUrl(previewPath);
        if (!url.empty())
            preferred.insert(std::move(url));
    };

    if (isCommunityTab()) {
        const int start = std::max(0, (m_communityScrollRow - 1) * kCommunityPreviewGridCols);
        const int end = std::min((int)m_communityEntries.size(),
                                 (m_communityScrollRow + 3) * kCommunityPreviewGridCols);
        for (int i = start; i < end; ++i)
            prefer(m_communityEntries[(size_t)i].cover);

        const auto* selected = selectedCommunityThemeEntry();
        if (selected) {
            prefer(selected->cover);
            if (m_detailOpen) {
                prefer(currentDetailCommunityPreviewPath());
                // Pin only the visible screenshot, not the whole gallery.
            }
        }
    }

    std::size_t readyCount = 0;
    for (const auto& entry : m_communityPreviewCache) {
        if (!entry.second) continue;
        std::lock_guard<std::mutex> lk(entry.second->mutex);
        if (entry.second->phase == PreviewPhase::Ready)
            ++readyCount;
    }
    if (readyCount <= kMaxReadyCommunityPreviews)
        return;

    int erased = 0;
    for (auto it = m_communityPreviewCache.begin();
         it != m_communityPreviewCache.end()
             && readyCount > kMaxReadyCommunityPreviews
             && erased < kMaxDropsPerCall; ) {
        if (preferred.find(it->first) != preferred.end()) {
            ++it;
            continue;
        }
        if (!it->second) {
            it = m_communityPreviewCache.erase(it);
            continue;
        }
        bool drop = false;
        {
            std::lock_guard<std::mutex> lk(it->second->mutex);
            if (it->second->phase == PreviewPhase::Ready) {
                it->second->texture = nxui::Texture();
                it->second->phase = PreviewPhase::Idle;
                it->second->failureCount = 0;
                drop = true;
                --readyCount;
            } else if (it->second->phase == PreviewPhase::Failed) {
                drop = true;
            } else if (it->second->phase == PreviewPhase::Loading
                       || it->second->phase == PreviewPhase::Downloaded) {
                ++it;
                continue;
            }
        }
        if (drop) {
            it = m_communityPreviewCache.erase(it);
            ++erased;
        } else {
            ++it;
        }
    }

    if (erased > 0) {
        DebugLog::log("[themeshop] preview cache soft-trim: erased=%d size=%zu ready=%zu",
                      erased, m_communityPreviewCache.size(), readyCount);
    }
}
void ThemeShopScreen::syncCommunityCatalog(const ThemeCatalogClient::Snapshot& snapshot) {
    m_communityTransferState = snapshot.loading;
    m_allCommunityEntries = snapshot.entries;
    m_communityRevision = snapshot.revision;
    const bool iconsChanged = m_catalogIconEntries.size() != snapshot.icons.size()
        || !std::equal(m_catalogIconEntries.begin(), m_catalogIconEntries.end(),
                       snapshot.icons.begin(),
                       [](const ThemeCatalogClient::IconEntry& lhs,
                          const ThemeCatalogClient::IconEntry& rhs) {
                           return lhs.id == rhs.id && lhs.version == rhs.version;
                       });
    m_catalogIconEntries = snapshot.icons;

    std::unordered_set<std::string> validUrls;
    for (const auto& entry : m_allCommunityEntries) {
        std::string coverUrl = resolveCommunityPreviewUrl(entry.cover);
        if (!coverUrl.empty())
            validUrls.insert(std::move(coverUrl));
        for (const auto& screenshot : entry.screenshots) {
            std::string screenshotUrl = resolveCommunityPreviewUrl(screenshot);
            if (!screenshotUrl.empty())
                validUrls.insert(std::move(screenshotUrl));
        }
    }
    for (auto it = m_communityPreviewCache.begin(); it != m_communityPreviewCache.end();) {
        if (validUrls.find(it->first) == validUrls.end())
            it = m_communityPreviewCache.erase(it);
        else
            ++it;
    }

    applySearchFilter();

    auto hasId = [&](const std::string& id) {
        return std::any_of(m_communityEntries.begin(), m_communityEntries.end(),
                           [&](const ThemeCatalogClient::Entry& entry) {
                               return entry.id == id;
                           });
    };

    if (m_communityEntries.empty()) {
        m_communitySelectedId.clear();
        return;
    }

    if (!hasId(m_communitySelectedId))
        m_communitySelectedId = m_communityEntries.front().id;

    // The Icons tab lists catalog packs too, so let the app rebuild it once the
    // icon list actually changed (install/remove/refresh).
    if (iconsChanged && m_iconCatalogUpdatedCb)
        m_iconCatalogUpdatedCb();
}
