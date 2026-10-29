#include "ThemeCatalogClient.hpp"

#include "ThemeHttp.hpp"

#include "core/DebugLog.hpp"

#include <fstream>
#include <filesystem>
#include <nxui/core/ThreadPool.hpp>

#include <curlpp/Easy.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>
#include <nlohmann/json.hpp>
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace {

void readStringOpt(const nlohmann::json& j, const char* key, std::string& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string())
        return;
    out = it->get<std::string>();
}

void appendUniqueString(std::vector<std::string>& out, std::string value) {
    if (value.empty())
        return;
    if (std::find(out.begin(), out.end(), value) != out.end())
        return;
    out.push_back(std::move(value));
}

void readStringArrayOpt(const nlohmann::json& j, const char* key, std::vector<std::string>& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array())
        return;

    for (const auto& item : *it) {
        if (item.is_string())
            appendUniqueString(out, item.get<std::string>());
    }
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string trimSlashes(std::string value) {
    while (!value.empty() && value.front() == '/')
        value.erase(value.begin());
    while (!value.empty() && value.back() == '/')
        value.pop_back();
    return value;
}

std::string parentDirectory(const std::string& path) {
    std::string clean = trimSlashes(path);
    std::size_t slash = clean.find_last_of('/');
    if (slash == std::string::npos)
        return {};
    return clean.substr(0, slash);
}

std::string joinPath(const std::string& base, const std::string& relative) {
    if (base.empty())
        return relative;
    if (relative.empty())
        return base;
    if (base.back() == '/')
        return base + relative;
    return base + "/" + relative;
}

std::string deriveManifestPath(const std::string& path) {
    if (path.empty())
        return {};
    return path + "/theme.json";
}

std::string resolvePreviewPath(const ThemeCatalogClient::Entry& entry, std::string previewPath) {
    previewPath = trimSlashes(std::move(previewPath));
    if (previewPath.empty())
        return {};
    if (startsWith(previewPath, "https://") || startsWith(previewPath, "http://"))
        return previewPath;

    if (!entry.path.empty())
        return joinPath(trimSlashes(entry.path), previewPath);

    return joinPath(parentDirectory(entry.manifest), previewPath);
}

void appendResolvedPreviewPath(const ThemeCatalogClient::Entry& entry,
                               std::vector<std::string>& out,
                               const std::string& previewPath) {
    appendUniqueString(out, resolvePreviewPath(entry, previewPath));
}

void readResolvedStringArrayOpt(const nlohmann::json& j,
                                const char* key,
                                const ThemeCatalogClient::Entry& entry,
                                std::vector<std::string>& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array())
        return;

    for (const auto& item : *it) {
        if (item.is_string())
            appendResolvedPreviewPath(entry, out, item.get<std::string>());
    }
}

void ensureCoverFirst(ThemeCatalogClient::Entry& entry) {
    if (entry.cover.empty()) {
        if (!entry.screenshots.empty())
            entry.cover = entry.screenshots.front();
        return;
    }

    auto it = std::find(entry.screenshots.begin(), entry.screenshots.end(), entry.cover);
    if (it == entry.screenshots.end()) {
        entry.screenshots.insert(entry.screenshots.begin(), entry.cover);
        return;
    }

    if (it != entry.screenshots.begin()) {
        std::string cover = *it;
        entry.screenshots.erase(it);
        entry.screenshots.insert(entry.screenshots.begin(), std::move(cover));
    }
}


bool jsonDeclaresIcons(const nlohmann::json& manifest) {
    auto iconsOf = [](const nlohmann::json& node) -> bool {
        if (!node.is_object() && !node.is_string())
            return false;
        if (node.is_string())
            return !node.get<std::string>().empty();
        for (const char* key : {"basePath", "base_path", "path", "base", "directory", "dir"}) {
            auto it = node.find(key);
            if (it != node.end() && it->is_string() && !it->get<std::string>().empty())
                return true;
        }
        auto filesIt = node.find("files");
        if (filesIt != node.end() && filesIt->is_array() && !filesIt->empty())
            return true;
        return false;
    };

    auto iconsIt = manifest.find("icons");
    if (iconsIt != manifest.end() && iconsOf(*iconsIt))
        return true;
    auto themeIt = manifest.find("theme");
    if (themeIt != manifest.end() && themeIt->is_object()) {
        auto themeIcons = themeIt->find("icons");
        if (themeIcons != themeIt->end() && iconsOf(*themeIcons))
            return true;
    }
    return false;
}

bool manifestDeclaresIcons(const std::string& catalogUrl, const ThemeCatalogClient::Entry& entry) {
    if (entry.manifest.empty())
        return false;
    std::size_t slash = catalogUrl.find_last_of('/');
    if (slash == std::string::npos)
        return false;
    const std::string manifestUrl = catalogUrl.substr(0, slash + 1) + trimSlashes(entry.manifest);
    const std::string manifestText = themeshop::http::getText(manifestUrl);
    nlohmann::json manifest = nlohmann::json::parse(manifestText);
    return jsonDeclaresIcons(manifest);
}

struct PreviewData {
    std::string cover;
    std::vector<std::string> screenshots;
    bool hasIcons = false;
    bool hasMusic = false;
    bool hasSounds = false;
    bool fetchedManifest = false;
};

bool jsonDeclaresBundledAudioDir(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end())
        return false;
    if (it->is_boolean())
        return it->get<bool>();
    if (it->is_string())
        return !it->get<std::string>().empty();
    if (it->is_array())
        return !it->empty();
    if (it->is_object())
        return true;
    return false;
}

bool jsonDeclaresMusic(const nlohmann::json& j) {
    if (j.contains("hasMusic") && j["hasMusic"].is_boolean())
        return j["hasMusic"].get<bool>();
    if (j.contains("has_music") && j["has_music"].is_boolean())
        return j["has_music"].get<bool>();
    if (jsonDeclaresBundledAudioDir(j, "music"))
        return true;
    auto audioIt = j.find("audio");
    if (audioIt != j.end() && audioIt->is_object()) {
        if (jsonDeclaresBundledAudioDir(*audioIt, "music"))
            return true;
        std::string preset;
        if (audioIt->contains("preset") && (*audioIt)["preset"].is_string())
            preset = (*audioIt)["preset"].get<std::string>();
        else if (audioIt->contains("soundPreset") && (*audioIt)["soundPreset"].is_string())
            preset = (*audioIt)["soundPreset"].get<std::string>();
            // package:/bundled without an explicit music key still usually ships BGM.
        if (preset == "bundled" || preset == "theme" || preset == "package"
            || preset.rfind("package:", 0) == 0)
            return true;
    }
    std::string soundPreset;
    if (j.contains("soundPreset") && j["soundPreset"].is_string())
        soundPreset = j["soundPreset"].get<std::string>();
    if (soundPreset == "bundled" || soundPreset == "theme" || soundPreset == "package"
        || soundPreset.rfind("package:", 0) == 0)
        return true;
    return false;
}

bool jsonDeclaresSounds(const nlohmann::json& j) {
    if (j.contains("hasSounds") && j["hasSounds"].is_boolean())
        return j["hasSounds"].get<bool>();
    if (j.contains("has_sounds") && j["has_sounds"].is_boolean())
        return j["has_sounds"].get<bool>();
    if (j.contains("hasSfx") && j["hasSfx"].is_boolean())
        return j["hasSfx"].get<bool>();
    if (jsonDeclaresBundledAudioDir(j, "sfx") || jsonDeclaresBundledAudioDir(j, "sounds"))
        return true;
    auto audioIt = j.find("audio");
    if (audioIt != j.end() && audioIt->is_object()) {
        if (jsonDeclaresBundledAudioDir(*audioIt, "sfx")
            || jsonDeclaresBundledAudioDir(*audioIt, "sounds"))
            return true;
        // Explicit music-only packages should not claim Sounds.
        if (jsonDeclaresBundledAudioDir(*audioIt, "music"))
            return false;
    }
    if (jsonDeclaresBundledAudioDir(j, "music"))
        return false;
    std::string soundPreset;
    if (j.contains("soundPreset") && j["soundPreset"].is_string())
        soundPreset = j["soundPreset"].get<std::string>();
    if (soundPreset == "bundled" || soundPreset == "theme" || soundPreset == "package"
        || soundPreset.rfind("package:", 0) == 0)
        return true;
    return false;
}

PreviewData derivePreviewDataFromManifest(const std::string& catalogUrl,
                                          const ThemeCatalogClient::Entry& entry) {
    if (entry.manifest.empty())
        return {};

    std::size_t slash = catalogUrl.find_last_of('/');
    if (slash == std::string::npos)
        return {};

    const std::string manifestUrl = catalogUrl.substr(0, slash + 1) + trimSlashes(entry.manifest);
    const std::string manifestText = themeshop::http::getText(manifestUrl);

    nlohmann::json manifest;
    try {
        manifest = nlohmann::json::parse(manifestText);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Invalid theme manifest JSON: ") + ex.what());
    }

    PreviewData data;
    data.fetchedManifest = true;
    data.hasIcons = jsonDeclaresIcons(manifest);
    data.hasMusic = jsonDeclaresMusic(manifest);
    data.hasSounds = jsonDeclaresSounds(manifest);
    std::string cover;
    readStringOpt(manifest, "cover", cover);
    if (!cover.empty())
        data.cover = resolvePreviewPath(entry, cover);

    appendUniqueString(data.screenshots, data.cover);

    std::string screenshot;
    readStringOpt(manifest, "screenshot", screenshot);
    appendResolvedPreviewPath(entry, data.screenshots, screenshot);
    readResolvedStringArrayOpt(manifest, "screenshots", entry, data.screenshots);

    auto previewIt = manifest.find("preview");
    if (previewIt == manifest.end() || !previewIt->is_object()) {
        if (data.cover.empty() && !data.screenshots.empty())
            data.cover = data.screenshots.front();
        return data;
    }

    cover.clear();
    readStringOpt(*previewIt, "cover", cover);
    if (!cover.empty() && data.cover.empty())
        data.cover = resolvePreviewPath(entry, cover);
    appendResolvedPreviewPath(entry, data.screenshots, cover);

    auto screenshotsIt = previewIt->find("screenshots");
    if (screenshotsIt != previewIt->end() && screenshotsIt->is_array()) {
        for (const auto& screenshot : *screenshotsIt) {
            if (screenshot.is_string())
                appendResolvedPreviewPath(entry, data.screenshots, screenshot.get<std::string>());
        }
    }

    auto screenshotIt = previewIt->find("screenshot");
    if (screenshotIt != previewIt->end() && screenshotIt->is_string())
        appendResolvedPreviewPath(entry, data.screenshots, screenshotIt->get<std::string>());

    if (data.cover.empty() && !data.screenshots.empty())
        data.cover = data.screenshots.front();

    return data;
}

} // namespace

ThemeCatalogClient::ThemeCatalogClient(std::string catalogUrl)
    : m_catalogUrl(std::move(catalogUrl)) {
}

void ThemeCatalogClient::setCatalogUrl(std::string catalogUrl) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_catalogUrl = std::move(catalogUrl);
}

void ThemeCatalogClient::refresh(nxui::ThreadPool& pool) {
    if (m_future.valid() && m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    if (m_future.valid())
        m_future.get();

    std::string url;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        url = m_catalogUrl;
        m_loading.begin("Loading theme catalog...");
        ++m_revision;
    }

    DebugLog::log("[themeshop] catalog refresh start: %s", url.c_str());

    m_future = pool.submit([this, url]() {
        Snapshot loaded;
        try {
            loaded = loadCatalog(url);
            DebugLog::log("[themeshop] catalog refresh done: %d entries", (int)loaded.entries.size());
        } catch (const std::exception& ex) {
            loaded.loading.fail(ex.what());
            DebugLog::log("[themeshop] catalog refresh failed: %s", ex.what());
        } catch (...) {
            loaded.loading.fail("Unknown network error");
            DebugLog::log("[themeshop] catalog refresh failed: unknown error");
        }

        std::lock_guard<std::mutex> lk(m_mutex);
        if (loaded.loading.isReady()) {
            m_entries = std::move(loaded.entries);
            m_icons = std::move(loaded.icons);
        }
        m_loading = loaded.loading;
        ++m_revision;
    });
}

bool ThemeCatalogClient::poll() {
    if (!m_future.valid())
        return false;
    if (m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;

    m_future.get();
    return true;
}

ThemeCatalogClient::Snapshot ThemeCatalogClient::snapshot() const {
    std::lock_guard<std::mutex> lk(m_mutex);

    Snapshot snapshot;
    snapshot.loading = m_loading;
    snapshot.entries = m_entries;
    snapshot.icons = m_icons;
    snapshot.revision = m_revision;
    return snapshot;
}

ThemeCatalogClient::Snapshot ThemeCatalogClient::loadCatalog(const std::string& url) {
    Snapshot snapshot;

    // Disk-first catalog: revisiting Community should paint from SD immediately.
    // Network is used when no cache exists; Refresh can delete the cache file.
    constexpr const char* kCatalogDiskPath = "sdmc:/config/SwitchU/community_preview_cache/catalog_index.json";
    std::string body;
    {
        std::ifstream in(kCatalogDiskPath, std::ios::binary);
        if (in) {
            body.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
    }
    if (body.empty()) {
        body = themeshop::http::getText(url);
        if (body.empty())
            throw std::runtime_error("Catalog body empty");
        std::error_code ec;
        std::filesystem::create_directories("sdmc:/config/SwitchU/community_preview_cache", ec);
        std::ofstream out(kCatalogDiskPath, std::ios::binary | std::ios::trunc);
        if (out)
            out.write(body.data(), static_cast<std::streamsize>(body.size()));
        DebugLog::log("[themeshop] catalog fetched and cached (%zu bytes)", body.size());
    } else {
        DebugLog::log("[themeshop] catalog loaded from disk cache (%zu bytes)", body.size());
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(body);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Invalid catalog JSON: ") + ex.what());
    }

    // Icon packs are optional so catalogs published before the icon shop keep
    // working unchanged.
    if (auto iconsIt = root.find("icons"); iconsIt != root.end() && iconsIt->is_array()) {
        for (const auto& item : *iconsIt) {
            if (!item.is_object())
                continue;

            IconEntry icon;
            readStringOpt(item, "id", icon.id);
            readStringOpt(item, "name", icon.name);
            readStringOpt(item, "author", icon.author);
            readStringOpt(item, "version", icon.version);
            readStringOpt(item, "description", icon.description);
            readStringOpt(item, "path", icon.path);
            readStringOpt(item, "manifest", icon.manifest);
            readStringOpt(item, "cover", icon.cover);
            readStringOpt(item, "theme", icon.theme);
            if (item.contains("builtin") && item["builtin"].is_boolean())
                icon.builtin = item["builtin"].get<bool>();
            if (item.contains("colour") && item["colour"].is_boolean())
                icon.colour = item["colour"].get<bool>();

            if (icon.id.empty())
                continue;
            if (icon.name.empty())
                icon.name = icon.id;
            if (icon.path.empty())
                icon.path = "icons/" + icon.id;
            if (icon.manifest.empty())
                icon.manifest = icon.path + "/pack.json";
            if (icon.cover.empty())
                icon.cover = icon.path + "/preview.png";

            snapshot.icons.push_back(std::move(icon));
        }

        std::sort(snapshot.icons.begin(), snapshot.icons.end(),
                  [](const IconEntry& lhs, const IconEntry& rhs) {
            if (lhs.builtin != rhs.builtin)
                return lhs.builtin; // built-ins first, like the console list
            if (lhs.name != rhs.name)
                return lhs.name < rhs.name;
            return lhs.id < rhs.id;
        });
    }

    auto themesIt = root.find("themes");
    if (themesIt == root.end() || !themesIt->is_array())
        throw std::runtime_error("Catalog is missing the themes array");

    snapshot.entries.reserve(themesIt->size());
    for (const auto& item : *themesIt) {
        if (!item.is_object())
            continue;

        Entry entry;
        readStringOpt(item, "id", entry.id);
        readStringOpt(item, "name", entry.name);
        readStringOpt(item, "author", entry.author);
        readStringOpt(item, "version", entry.version);
        readStringOpt(item, "path", entry.path);
        readStringOpt(item, "manifest", entry.manifest);
        readStringOpt(item, "cover", entry.cover);
        readStringArrayOpt(item, "screenshots", entry.screenshots);

        std::string screenshot;
        readStringOpt(item, "screenshot", screenshot);
        appendUniqueString(entry.screenshots, screenshot);

        // Catalog may advertise icons/audio; otherwise peek theme.json (rare today).
        if (item.contains("hasIcons") && item["hasIcons"].is_boolean())
            entry.hasIcons = item["hasIcons"].get<bool>();
        else if (item.contains("has_icons") && item["has_icons"].is_boolean())
            entry.hasIcons = item["has_icons"].get<bool>();
        else if (item.contains("icons"))
            entry.hasIcons = jsonDeclaresIcons(item);
        entry.hasMusic = jsonDeclaresMusic(item);
        entry.hasSounds = jsonDeclaresSounds(item);

        if (entry.id.empty() || entry.name.empty())
            continue;
        if (entry.manifest.empty())
            entry.manifest = deriveManifestPath(entry.path);
        // Fetch theme.json when preview art OR audio/icon badges are unknown.
        // Catalog load is already async; theme.json is small. Skipping this when
        // covers exist left Community Music/Sounds badges blank.
        const bool needManifest = !entry.manifest.empty()
            && (entry.cover.empty() || entry.screenshots.empty()
                || !entry.hasIcons || !entry.hasMusic || !entry.hasSounds);
        if (needManifest) {
            try {
                PreviewData preview = derivePreviewDataFromManifest(url, entry);
                if (entry.cover.empty())
                    entry.cover = preview.cover;
                for (auto& screenshotPath : preview.screenshots)
                    appendUniqueString(entry.screenshots, std::move(screenshotPath));
                if (preview.fetchedManifest) {
                    if (!entry.hasIcons)
                        entry.hasIcons = preview.hasIcons;
                    if (!entry.hasMusic)
                        entry.hasMusic = preview.hasMusic;
                    if (!entry.hasSounds)
                        entry.hasSounds = preview.hasSounds;
                }
            } catch (const std::exception& ex) {
                DebugLog::log("[themeshop] manifest preview fallback failed for %s: %s",
                              entry.id.c_str(),
                              ex.what());
            }
        }

        ensureCoverFirst(entry);

        snapshot.entries.push_back(std::move(entry));
    }

    std::sort(snapshot.entries.begin(), snapshot.entries.end(), [](const Entry& lhs, const Entry& rhs) {
        if (lhs.name != rhs.name)
            return lhs.name < rhs.name;
        return lhs.id < rhs.id;
    });

    if (snapshot.entries.empty())
        snapshot.loading.succeed("Catalog is empty.");
    else
        snapshot.loading.succeed(std::to_string(snapshot.entries.size()) + " themes available.");

    return snapshot;
}