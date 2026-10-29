#include "SteamGridDbManager.hpp"

#include "ArtworkCache.hpp"
#include "core/DebugLog.hpp"
#include "themeshop/ThemeHttp.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <list>
#include <nlohmann/json.hpp>
#include <set>
#include <system_error>

namespace {

constexpr const char* kApiBase = "https://www.steamgriddb.com/api/v2";
constexpr int kMatcherVersion = 4; // v4: short query tokens cover longer titles.

struct ArtworkKindInfo {
    const char* endpoint = "/icons";
    const char* label = "Icon";
    const char* flag = "icon";
    const char* idKey = "selectedIconId";
    const char* indexKey = "selectedIconIndex";
    // Appended after nsfw/humor (and optional types=static).
    const char* querySuffix = "&mimes=image/png";
    bool useStaticTypes = false;
    bool requireLandscape = false;
    bool requirePortrait = false;
    bool prepareCache = false;
    int prepW = 0;
    int prepH = 0;
};

ArtworkKindInfo kindInfo(SteamGridDbManager::ArtworkKind kind) {
    using Kind = SteamGridDbManager::ArtworkKind;
    switch (kind) {
    case Kind::Hero:
        // Cap to ~1080p-class heroes (skip 3840x1240). UI cards use 640x360.
        return {"/heroes", "Hero", "hero", "selectedHeroId", "selectedHeroIndex",
                "&mimes=image/png,image/jpeg&dimensions=1920x620,1600x650",
                true, true, false, true, 640, 360};
    case Kind::Logo:
        // Logos have no SGDB dimension enum; PNG for alpha. UI uses 384x192.
        return {"/logos", "Logo", "logo", "selectedLogoId", "selectedLogoIndex",
                "&mimes=image/png", true, false, false, true, 384, 192};
    case Kind::Poster:
        // SteamGridDB "grids" — portrait covers only for SwitchU Poster tiles.
        // prepW/H are the IconStreamer fit box (see prepareFit), not letterboxed.
        // Drop 660x930; 600x900 / 342x482 cover the 320x480 fit target.
        return {"/grids", "Poster", "poster", "selectedPosterId", "selectedPosterIndex",
                "&mimes=image/png,image/jpeg&dimensions=600x900,342x482",
                true, false, true, true, 320, 480};
    case Kind::GridWide:
        // Landscape SGDB grids for 2x1 / 3x2 HOME tiles (separate from Hero+Logo).
        return {"/grids", "Poster wide", "gridWide", "selectedGridWideId",
                "selectedGridWideIndex",
                "&mimes=image/png,image/jpeg&dimensions=460x215,920x430",
                true, true, false, true, 460, 215};
    case Kind::Icon:
    default:
        // Icons are PNG-only on SGDB. Grid icons draw at 160px; prefer 256/512,
        // keep 1024 so games without smaller uploads still appear (ranked down).
        return {"/icons", "Icon", "icon", "selectedIconId", "selectedIconIndex",
                "&mimes=image/png&dimensions=256,512,1024",
                false, false, false, false, 0, 0};
    }
}

std::string pathForKind(SteamGridDbManager::ArtworkKind kind, std::uint64_t titleId) {
    using Kind = SteamGridDbManager::ArtworkKind;
    switch (kind) {
    case Kind::Hero:     return SteamGridDbManager::heroPath(titleId);
    case Kind::Logo:     return SteamGridDbManager::logoPath(titleId);
    case Kind::Poster:   return SteamGridDbManager::posterPath(titleId);
    case Kind::GridWide: return SteamGridDbManager::gridWidePath(titleId);
    case Kind::Icon:
    default:             return SteamGridDbManager::iconPath(titleId);
    }
}

bool looksLikeHttpsClockFailure(const std::string& error) {
    if (error.empty())
        return true;
    std::string lower = error;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("ssl") != std::string::npos
        || lower.find("tls") != std::string::npos
        || lower.find("certificate") != std::string::npos
        || lower.find("cert verify") != std::string::npos
        || lower.find("peer certificate") != std::string::npos
        || lower.find("certificate has expired") != std::string::npos
        || lower.find("https failed") != std::string::npos;
}

// Wrong console clocks break TLS to SteamGridDB and used to surface as a blank
// picker error (empty curl what()). Point people at System clock sync.
std::string friendlySteamGridDbTransportError(const std::string& error) {
    if (!looksLikeHttpsClockFailure(error))
        return error.empty() ? "SteamGridDB search failed." : error;
    return nxui::I18n::instance().tr(
        "settings.steamgriddb.https_clock_hint",
        "HTTPS failed. If the console clock is wrong, fix it in Settings > System - "
        "try Synchronize Clock via Public Servers.");
}

std::string titleDirectory(std::uint64_t titleId) {
    char id[17]{};
    std::snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(titleId));
    return std::string(SteamGridDbManager::kCacheRoot) + "/" + id;
}

std::string percentEncode(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 2);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 15]);
        }
    }
    return out;
}

std::string normalized(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c))
            out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::set<std::string> titleTokens(const std::string& value) {
    std::set<std::string> tokens;
    std::string token;
    for (unsigned char c : value) {
        if (std::isalnum(c)) {
            token.push_back(static_cast<char>(std::tolower(c)));
        } else if (!token.empty()) {
            tokens.insert(std::move(token));
            token.clear();
        }
    }
    if (!token.empty()) tokens.insert(std::move(token));
    return tokens;
}

float titleSimilarity(const std::string& requested, const std::string& candidate) {
    const std::string a = normalized(requested);
    const std::string b = normalized(candidate);
    if (a.empty() || b.empty()) return 0.f;
    if (a == b) return 1.f;

    const auto aTokens = titleTokens(requested);
    const auto bTokens = titleTokens(candidate);
    if (aTokens.empty() || bTokens.empty()) return 0.f;

    int intersection = 0;
    for (const auto& token : aTokens)
        if (bTokens.count(token)) ++intersection;
    const int unionCount = static_cast<int>(aTokens.size() + bTokens.size()) - intersection;
    const int smallerCount = static_cast<int>(std::min(aTokens.size(), bTokens.size()));
    const float jaccard = unionCount > 0 ? static_cast<float>(intersection) / unionCount : 0.f;
    const float coverage = smallerCount > 0 ? static_cast<float>(intersection) / smallerCount : 0.f;

    // Query tokens fully covered by the candidate ("ocarina" → "Ocarina of Time").
    const bool queryCovered = !aTokens.empty()
        && intersection == static_cast<int>(aTokens.size());

    // Containment helps with edition/subtitle suffixes and short query prefixes.
    const std::size_t shorter = std::min(a.size(), b.size());
    const std::size_t longer = std::max(a.size(), b.size());
    const bool contained = a.find(b) != std::string::npos || b.find(a) != std::string::npos;
    const float containmentRatio = longer > 0 ? static_cast<float>(shorter) / longer : 0.f;
    float tokenScore = coverage * 0.65f + jaccard * 0.35f;
    if (queryCovered)
        tokenScore = std::max(tokenScore, 0.88f);
    return contained && containmentRatio >= 0.50f
        ? std::max(tokenScore, 0.84f)
        : tokenScore;
}

nlohmann::json apiData(const std::string& url, const std::list<std::string>& headers,
                       const themeshop::http::AbortCheck& shouldAbort = {}) {
    auto body = themeshop::http::getText(url, headers, shouldAbort);
    auto json = nlohmann::json::parse(body);
    if (!json.value("success", false)) {
        std::string error = "SteamGridDB request failed";
        if (json.contains("errors") && json["errors"].is_array() && !json["errors"].empty())
            error = json["errors"].front().get<std::string>();
        throw std::runtime_error(error);
    }
    return json.value("data", nlohmann::json::array());
}

const nlohmann::json* chooseGame(const nlohmann::json& games, const std::string& title,
                                 float& outScore, std::string& outClosest,
                                 bool& outAmbiguous) {
    constexpr float kMinimumMatchScore = 0.78f;
    outScore = 0.f;
    outClosest.clear();
    outAmbiguous = false;
    if (!games.is_array() || games.empty()) return nullptr;
    const nlohmann::json* best = nullptr;
    float secondBestScore = 0.f;
    for (const auto& game : games) {
        if (!game.is_object() || !game.contains("id")) continue;
        float score = titleSimilarity(title, game.value("name", std::string()));
        if (game.value("verified", false) && score < 1.f)
            score = std::min(1.f, score + 0.02f);
        if (!best || score > outScore) {
            secondBestScore = outScore;
            best = &game;
            outScore = score;
            outClosest = game.value("name", std::string());
        } else if (score > secondBestScore) {
            secondBestScore = score;
        }
    }
    outAmbiguous = outScore < 0.999f && secondBestScore >= outScore - 0.04f;
    if (outAmbiguous) return nullptr;
    return outScore >= kMinimumMatchScore ? best : nullptr;
}

bool cacheUsesCurrentMatcher(std::uint64_t titleId) {
    std::ifstream input(titleDirectory(titleId) + "/metadata.json");
    if (!input.is_open()) return false;
    try {
        nlohmann::json metadata;
        input >> metadata;
        const bool manuallySelected = metadata.contains("selectedHeroId")
                                   || metadata.contains("selectedLogoId");
        return manuallySelected
            || metadata.value("matcherVersion", 0) == kMatcherVersion;
    } catch (...) {
        return false;
    }
}

void removeCachedArtwork(std::uint64_t titleId) {
    std::error_code ec;
    const std::string hero = SteamGridDbManager::heroPath(titleId);
    const std::string logo = SteamGridDbManager::logoPath(titleId);
    const std::string gridWide = SteamGridDbManager::gridWidePath(titleId);
    std::filesystem::remove(hero, ec);
    ec.clear();
    std::filesystem::remove(logo, ec);
    ec.clear();
    std::filesystem::remove(gridWide, ec);
    ec.clear();
    std::filesystem::remove(hero + ".640x360.rgba-cache", ec);
    ec.clear();
    std::filesystem::remove(hero + ".1280x720.rgba-cache", ec); // legacy prep size
    ec.clear();
    std::filesystem::remove(logo + ".384x192.rgba-cache", ec);
    ec.clear();
    std::filesystem::remove(logo + ".640x180.rgba-cache", ec); // legacy prep size
    ec.clear();
    std::filesystem::remove(gridWide + ".460x215.rgba-cache", ec);
    ec.clear();
    std::filesystem::remove(titleDirectory(titleId) + "/metadata.json", ec);
}

const nlohmann::json* chooseImage(const nlohmann::json& images, bool portrait) {
    if (!images.is_array()) return nullptr;
    const nlohmann::json* best = nullptr;
    long bestScore = -1000000;
    for (const auto& image : images) {
        if (!image.is_object() || !image.contains("url")) continue;
        std::string url = image.value("url", std::string());
        std::string lowerUrl = url;
        std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::size_t query = lowerUrl.find('?');
        if (query != std::string::npos) lowerUrl.resize(query);
        const bool supported = lowerUrl.ends_with(".png")
                            || lowerUrl.ends_with(".jpg")
                            || lowerUrl.ends_with(".jpeg");
        if (!supported) continue;
        const int w = image.value("width", 0);
        const int h = image.value("height", 0);
        const bool orientationOk = portrait ? (h > w) : (w >= h);
        const long score = image.value("score", 0L)
                         + image.value("upvotes", 0L)
                         - image.value("downvotes", 0L)
                         + (orientationOk ? 100000L : 0L);
        if (!best || score > bestScore) {
            best = &image;
            bestScore = score;
        }
    }
    return best;
}

std::vector<const nlohmann::json*> rankedImages(const nlohmann::json& images,
                                                bool requireLandscape,
                                                bool requirePortrait = false) {
    std::vector<const nlohmann::json*> ranked;
    if (!images.is_array()) return ranked;
    for (const auto& image : images) {
        if (!image.is_object() || !image.contains("url")) continue;
        std::string url = image.value("url", std::string());
        std::transform(url.begin(), url.end(), url.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::size_t query = url.find('?');
        if (query != std::string::npos) url.resize(query);
        if (!url.ends_with(".png") && !url.ends_with(".jpg") && !url.ends_with(".jpeg"))
            continue;
        const int w = image.value("width", 0);
        const int h = image.value("height", 0);
        if (requireLandscape && w < h)
            continue;
        if (requirePortrait && h <= w)
            continue;
        ranked.push_back(&image);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto* a, const auto* b) {
        const long scoreA = a->value("score", 0L) + a->value("upvotes", 0L)
                          - a->value("downvotes", 0L);
        const long scoreB = b->value("score", 0L) + b->value("upvotes", 0L)
                          - b->value("downvotes", 0L);
        return scoreA > scoreB;
    });
    return ranked;
}

nlohmann::json readMetadata(std::uint64_t titleId) {
    std::ifstream input(titleDirectory(titleId) + "/metadata.json");
    if (!input.is_open()) return nlohmann::json::object();
    try {
        nlohmann::json metadata;
        input >> metadata;
        return metadata.is_object() ? metadata : nlohmann::json::object();
    } catch (...) {
        return nlohmann::json::object();
    }
}

void writeMetadata(std::uint64_t titleId, const nlohmann::json& metadata) {
    if (titleId == 0) return;
    std::error_code ec;
    std::filesystem::create_directories(titleDirectory(titleId), ec);
    std::ofstream output(titleDirectory(titleId) + "/metadata.json", std::ios::trunc);
    if (output.is_open())
        output << metadata.dump(2);
}

bool saveBytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out.good()) return false;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

bool fetchImage(const nlohmann::json& images, bool portrait,
                const std::string& path,
                const themeshop::http::ProgressCallback& onProgress = {},
                const themeshop::http::AbortCheck& shouldAbort = {}) {
    const auto* image = chooseImage(images, portrait);
    if (!image) return false;
    const std::string url = image->value("url", std::string());
    if (url.empty()) return false;
    return saveBytes(path, themeshop::http::getBytes(url, {}, onProgress, shouldAbort));
}

bool prepareArtwork(SteamGridDbManager::ArtworkKind kind, const std::string& path) {
    const auto info = kindInfo(kind);
    if (!info.prepareCache)
        return true;
    if (kind == SteamGridDbManager::ArtworkKind::Poster)
        return steamgriddb::artwork::prepareFit(path, info.prepW, info.prepH);
    return steamgriddb::artwork::prepare(path, info.prepW, info.prepH,
                                         kind == SteamGridDbManager::ArtworkKind::Hero
                                         || kind == SteamGridDbManager::ArtworkKind::GridWide);
}

void removePreparedArtwork(SteamGridDbManager::ArtworkKind kind, const std::string& path) {
    const auto info = kindInfo(kind);
    if (!info.prepareCache)
        return;
    if (kind == SteamGridDbManager::ArtworkKind::Poster) {
        steamgriddb::artwork::removeFit(path, info.prepW, info.prepH);
        // Legacy fixed-canvas cache from earlier poster prep.
        steamgriddb::artwork::remove(path, 600, 900);
        return;
    }
    steamgriddb::artwork::remove(path, info.prepW, info.prepH);
}

/// Cheap existence check for the selected artwork kind. Network failures return
/// true so a flaky probe does not wipe the whole result list.
bool probeGameHasArtwork(const std::string& apiKey, long long gameId,
                         SteamGridDbManager::ArtworkKind kind) {
    if (apiKey.empty() || gameId <= 0 || kind == SteamGridDbManager::ArtworkKind::None)
        return false;
    const std::list<std::string> headers = {
        "Authorization: Bearer " + apiKey,
        "Accept: application/json",
    };
    try {
        const auto info = kindInfo(kind);
        std::string url = std::string(kApiBase) + info.endpoint + "/game/"
                        + std::to_string(gameId) + "?nsfw=false&humor=false";
        if (info.useStaticTypes) url += "&types=static";
        url += info.querySuffix;
        const auto images = apiData(url, headers);
        const auto ranked = rankedImages(images, info.requireLandscape,
                                         info.requirePortrait);
        for (const auto* image : ranked) {
            if (image && !image->value("url", std::string()).empty())
                return true;
        }
        return false;
    } catch (...) {
        return true;
    }
}

} // namespace

SteamGridDbManager::~SteamGridDbManager() {
    cancelAndWait();
}

SteamGridDbManager::BrowseResult SteamGridDbManager::browse(
    const std::string& apiKey, std::uint64_t titleId, const std::string& title,
    const std::string& query, ArtworkKind kind) {
    BrowseResult result;
    result.titleId = titleId;
    result.title = title;
    result.query = query.empty() ? title : query;
    result.kind = kind;
    if (apiKey.empty() || titleId == 0 || result.query.empty() || kind == ArtworkKind::None) {
        result.error = "Invalid SteamGridDB search";
        return result;
    }

    const std::list<std::string> headers = {
        "Authorization: Bearer " + apiKey,
        "Accept: application/json",
    };
    try {
        const auto games = apiData(std::string(kApiBase) + "/search/autocomplete/"
                                   + percentEncode(result.query), headers);
        if (!games.is_array() || games.empty())
            throw std::runtime_error("No SteamGridDB games for '" + result.query + "'");

        std::vector<GameMatch> ranked;
        ranked.reserve(games.size());
        for (const auto& game : games) {
            if (!game.is_object() || !game.contains("id")) continue;
            GameMatch match;
            match.id = game.at("id").get<long long>();
            match.name = game.value("name", std::string());
            match.score = titleSimilarity(result.query, match.name);
            if (game.value("verified", false) && match.score < 1.f)
                match.score = std::min(1.f, match.score + 0.02f);
            // Keep loose matches so short queries like "ocarina" still surface
            // "Ocarina of Time" for the user to pick.
            if (match.score >= 0.40f || ranked.size() < 8)
                ranked.push_back(std::move(match));
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const GameMatch& a, const GameMatch& b) { return a.score > b.score; });
        if (ranked.size() > 12)
            ranked.resize(12);
        // Drop trailing junk once we have at least one decent hit.
        while (ranked.size() > 1 && ranked.back().score < 0.40f)
            ranked.pop_back();
        if (ranked.empty())
            throw std::runtime_error("No sufficiently close game match for '" + result.query + "'");

        // Probe each ranked game for the requested artwork kind so empty
        // SteamGridDB entries (no icons/heroes/logos) never reach the picker.
        {
            std::vector<std::future<bool>> probes;
            probes.reserve(ranked.size());
            for (const auto& match : ranked) {
                probes.push_back(std::async(std::launch::async,
                    [apiKey, kind, id = match.id]() {
                        return probeGameHasArtwork(apiKey, id, kind);
                    }));
            }
            std::vector<GameMatch> withArt;
            withArt.reserve(ranked.size());
            for (std::size_t i = 0; i < ranked.size(); ++i) {
                bool keep = true;
                try {
                    keep = probes[i].get();
                } catch (...) {
                    keep = true;
                }
                if (keep)
                    withArt.push_back(std::move(ranked[i]));
            }
            ranked = std::move(withArt);
        }
        if (ranked.empty())
            throw std::runtime_error(
                "No games with " + std::string(kindInfo(kind).label) +
                " artwork for '" + result.query + "'");

        result.gameMatches = ranked;
        result.matchScore = ranked.front().score;

        // Only auto-open artwork when one game clearly wins. Otherwise the
        // picker shows the ranked game list so the user can drill down.
        const bool clearWinner = ranked.size() == 1
            || (ranked.front().score >= 0.92f
                && ranked.front().score >= ranked[1].score + 0.08f);
        if (!clearWinner) {
            result.success = true;
            result.error.clear();
            return result;
        }

        auto artwork = browseGame(apiKey, titleId, title, result.query, kind,
                                  ranked.front().id, ranked.front().name);
        // Keep the list available for B→games when search was ambiguous-but-clear.
        if (ranked.size() > 1)
            artwork.gameMatches = ranked;
        artwork.matchScore = ranked.front().score;
        return artwork;
    } catch (const std::exception& ex) {
        result.error = friendlySteamGridDbTransportError(ex.what() ? ex.what() : "");
        DebugLog::log("[steamgriddb] browse '%s' failed: %s",
                      result.query.c_str(), result.error.c_str());
    }
    return result;
}

SteamGridDbManager::BrowseResult SteamGridDbManager::browseGame(
    const std::string& apiKey, std::uint64_t titleId, const std::string& title,
    const std::string& query, ArtworkKind kind, long long gameId,
    const std::string& gameName) {
    BrowseResult result;
    result.titleId = titleId;
    result.title = title;
    result.query = query.empty() ? title : query;
    result.kind = kind;
    result.gameId = gameId;
    result.gameName = gameName.empty() ? result.query : gameName;
    if (apiKey.empty() || titleId == 0 || gameId <= 0 || kind == ArtworkKind::None) {
        result.error = "Invalid SteamGridDB game";
        return result;
    }

    const std::list<std::string> headers = {
        "Authorization: Bearer " + apiKey,
        "Accept: application/json",
    };
    try {
        const auto info = kindInfo(kind);
        std::string url = std::string(kApiBase) + info.endpoint + "/game/"
                        + std::to_string(gameId) + "?nsfw=false&humor=false";
        if (info.useStaticTypes) url += "&types=static";
        url += info.querySuffix;
        const auto images = apiData(url, headers);
        const auto ranked = rankedImages(images, info.requireLandscape,
                                         info.requirePortrait);
        constexpr std::size_t kMaxCandidates = 18;
        for (std::size_t i = 0; i < ranked.size() && i < kMaxCandidates; ++i) {
            Candidate candidate;
            candidate.id = ranked[i]->value("id", 0LL);
            candidate.url = ranked[i]->value("url", std::string());
            candidate.thumbnailUrl = ranked[i]->value("thumb", candidate.url);
            candidate.width = ranked[i]->value("width", 0);
            candidate.height = ranked[i]->value("height", 0);
            if (!candidate.url.empty()) result.candidates.push_back(std::move(candidate));
        }
        if (result.candidates.empty()) throw std::runtime_error("No artwork available");
        result.success = true;
    } catch (const std::exception& ex) {
        result.error = friendlySteamGridDbTransportError(ex.what() ? ex.what() : "");
        DebugLog::log("[steamgriddb] browseGame id=%lld failed: %s",
                      static_cast<long long>(gameId), result.error.c_str());
    }
    return result;
}

SteamGridDbManager::ApplyResult SteamGridDbManager::applyCandidate(
    const BrowseResult& browseResult, const Candidate& candidate,
    const ProgressCallback& onProgress,
    const AbortCheck& shouldAbort) {
    ApplyResult result;
    result.titleId = browseResult.titleId;
    result.kind = browseResult.kind;
    auto aborted = [&]() {
        return shouldAbort && shouldAbort();
    };
    try {
        if (aborted()) {
            result.cancelled = true;
            result.message = "Cancelled";
            return result;
        }
        const auto info = kindInfo(result.kind);
        const std::string destination = pathForKind(result.kind, result.titleId);
        std::error_code ec;
        std::filesystem::create_directories(titleDirectory(result.titleId), ec);
        if (info.prepareCache)
            removePreparedArtwork(result.kind, destination);
        if (onProgress) onProgress("Downloading artwork...", 0.02f);
        DebugLog::log("[steamgriddb] apply start title=%016llX kind=%d url=%s %dx%d",
                      static_cast<unsigned long long>(result.titleId),
                      static_cast<int>(result.kind),
                      candidate.url.c_str(),
                      candidate.width,
                      candidate.height);
        auto downloadProgress = [&](std::uint64_t downloaded, std::uint64_t total) {
            if (!onProgress) return;
            const float networkProgress = total > 0
                ? std::clamp(static_cast<float>(downloaded) / static_cast<float>(total), 0.f, 1.f)
                : 0.f;
            onProgress("Downloading artwork...", 0.02f + networkProgress * 0.68f);
        };
        if (candidate.url.empty())
            throw std::runtime_error("Artwork download failed");
        auto bytes = themeshop::http::getBytes(candidate.url, {}, downloadProgress, shouldAbort);
        if (aborted()) {
            result.cancelled = true;
            result.message = "Cancelled";
            DebugLog::log("[steamgriddb] apply cancelled during download title=%016llX",
                          static_cast<unsigned long long>(result.titleId));
            return result;
        }
        if (bytes.empty() || !saveBytes(destination, bytes))
            throw std::runtime_error("Artwork download failed");
        DebugLog::log("[steamgriddb] apply downloaded title=%016llX bytes=%zu",
                      static_cast<unsigned long long>(result.titleId),
                      bytes.size());

        if (info.prepareCache) {
            if (aborted()) {
                result.cancelled = true;
                result.message = "Cancelled";
                return result;
            }
            if (onProgress) onProgress("Preparing artwork cache...", 0.74f);
            if (!prepareArtwork(result.kind, destination))
                throw std::runtime_error("Artwork decode failed");
        }
        if (aborted()) {
            result.cancelled = true;
            result.message = "Cancelled";
            return result;
        }
        if (onProgress) onProgress("Saving artwork...", 0.96f);

        nlohmann::json metadata = readMetadata(result.titleId);
        metadata["titleId"] = result.titleId;
        metadata["title"] = browseResult.title;
        metadata["steamGridDbGameId"] = browseResult.gameId;
        metadata["steamGridDbName"] = browseResult.gameName;
        metadata["matcherVersion"] = kMatcherVersion;
        metadata["matchScore"] = browseResult.matchScore;
        if (!browseResult.query.empty())
            metadata["searchQuery"] = browseResult.query;
        metadata[info.flag] = true;
        metadata[info.idKey] = candidate.id;
        writeMetadata(result.titleId, metadata);
        result.success = true;
        result.message = std::string(info.label) + " applied";
        if (onProgress) onProgress(result.message, 1.f);
        DebugLog::log("[steamgriddb] apply done title=%016llX kind=%d",
                      static_cast<unsigned long long>(result.titleId),
                      static_cast<int>(result.kind));
    } catch (const std::exception& ex) {
        if (aborted()) {
            result.cancelled = true;
            result.message = "Cancelled";
            DebugLog::log("[steamgriddb] apply cancelled title=%016llX",
                          static_cast<unsigned long long>(result.titleId));
        } else {
            result.message = ex.what();
            DebugLog::log("[steamgriddb] apply candidate failed: %s", ex.what());
        }
    }
    return result;
}

void SteamGridDbManager::wait() {
    if (m_task.valid()) m_task.wait();
}

void SteamGridDbManager::cancelAndWait() {
    m_cancelRequested.store(true);
    wait();
}

void SteamGridDbManager::requestCancel() {
    m_cancelRequested.store(true);
    updateStatus([](Status& status) {
        status.message = "Cancelling...";
        ++status.revision;
    });
}

std::string SteamGridDbManager::heroPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/hero.img";
}

std::string SteamGridDbManager::logoPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/logo.img";
}

std::string SteamGridDbManager::iconPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/icon.img";
}

std::string SteamGridDbManager::posterPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/poster.img";
}

std::string SteamGridDbManager::gridWidePath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/gridwide.img";
}

bool SteamGridDbManager::hasArtwork(std::uint64_t titleId) {
    std::error_code ec;
    return cacheUsesCurrentMatcher(titleId)
        && (std::filesystem::exists(heroPath(titleId), ec)
        || std::filesystem::exists(logoPath(titleId), ec));
}

bool SteamGridDbManager::hasIcon(std::uint64_t titleId) {
    std::error_code ec;
    return titleId != 0 && std::filesystem::exists(iconPath(titleId), ec);
}

bool SteamGridDbManager::hasPoster(std::uint64_t titleId) {
    std::error_code ec;
    return titleId != 0 && std::filesystem::exists(posterPath(titleId), ec);
}

bool SteamGridDbManager::hasGridWide(std::uint64_t titleId) {
    std::error_code ec;
    return titleId != 0 && std::filesystem::exists(gridWidePath(titleId), ec);
}

std::string SteamGridDbManager::savedSearchQuery(std::uint64_t titleId) {
    if (titleId == 0) return {};
    return readMetadata(titleId).value("searchQuery", std::string());
}

void SteamGridDbManager::saveSearchQuery(std::uint64_t titleId, const std::string& query) {
    if (titleId == 0) return;
    std::string trimmed = query;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    if (trimmed.empty()) return;
    nlohmann::json metadata = readMetadata(titleId);
    metadata["titleId"] = titleId;
    metadata["searchQuery"] = trimmed;
    writeMetadata(titleId, metadata);
}

bool SteamGridDbManager::clearArtwork(std::uint64_t titleId, ArtworkKind kind) {
    if (titleId == 0)
        return false;

    std::error_code ec;
    bool removed = false;
    auto removeFile = [&](const std::string& path) {
        if (std::filesystem::remove(path, ec))
            removed = true;
        ec.clear();
    };

    const bool clearHero = kind == ArtworkKind::None || kind == ArtworkKind::Hero;
    const bool clearLogo = kind == ArtworkKind::None || kind == ArtworkKind::Logo;
    const bool clearIcon = kind == ArtworkKind::None || kind == ArtworkKind::Icon;
    const bool clearPoster = kind == ArtworkKind::None || kind == ArtworkKind::Poster;
    const bool clearGridWide = kind == ArtworkKind::None || kind == ArtworkKind::GridWide;

    if (clearHero) {
        removeFile(heroPath(titleId));
        removeFile(heroPath(titleId) + ".640x360.rgba-cache");
        removeFile(heroPath(titleId) + ".1280x720.rgba-cache"); // legacy
    }
    if (clearLogo) {
        removeFile(logoPath(titleId));
        removeFile(logoPath(titleId) + ".384x192.rgba-cache");
        removeFile(logoPath(titleId) + ".640x180.rgba-cache"); // legacy
    }
    if (clearIcon)
        removeFile(iconPath(titleId));
    if (clearPoster) {
        removeFile(posterPath(titleId));
        removeFile(posterPath(titleId) + ".fit320x480.rgba-cache");
        removeFile(posterPath(titleId) + ".600x900.rgba-cache");
    }
    if (clearGridWide) {
        removeFile(gridWidePath(titleId));
        removeFile(gridWidePath(titleId) + ".460x215.rgba-cache");
    }

    nlohmann::json metadata = readMetadata(titleId);
    if (clearHero) {
        metadata["hero"] = false;
        metadata.erase("selectedHeroId");
        metadata.erase("selectedHeroIndex");
    }
    if (clearLogo) {
        metadata["logo"] = false;
        metadata.erase("selectedLogoId");
        metadata.erase("selectedLogoIndex");
    }
    if (clearIcon) {
        metadata["icon"] = false;
        metadata.erase("selectedIconId");
        metadata.erase("selectedIconIndex");
    }
    if (clearPoster) {
        metadata["poster"] = false;
        metadata.erase("selectedPosterId");
        metadata.erase("selectedPosterIndex");
    }
    if (clearGridWide) {
        metadata["gridWide"] = false;
        metadata.erase("selectedGridWideId");
        metadata.erase("selectedGridWideIndex");
    }

    const bool anyLeft = metadata.value("hero", false)
                      || metadata.value("logo", false)
                      || metadata.value("icon", false)
                      || metadata.value("poster", false)
                      || metadata.value("gridWide", false)
                      || std::filesystem::exists(heroPath(titleId), ec)
                      || std::filesystem::exists(logoPath(titleId), ec)
                      || std::filesystem::exists(iconPath(titleId), ec)
                      || std::filesystem::exists(posterPath(titleId), ec)
                      || std::filesystem::exists(gridWidePath(titleId), ec);
    ec.clear();
    if (!anyLeft) {
        removeFile(titleDirectory(titleId) + "/metadata.json");
        std::filesystem::remove(titleDirectory(titleId), ec);
    } else {
        std::ofstream output(titleDirectory(titleId) + "/metadata.json", std::ios::trunc);
        if (output.is_open())
            output << metadata.dump(2);
    }
    return removed;
}

bool SteamGridDbManager::start(const std::string& apiKey,
                               const std::vector<AppEntry>& apps) {
    if (apiKey.empty() || apps.empty() || m_running.exchange(true)) return false;
    if (m_task.valid()) m_task.get();
    m_cancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::uint64_t nextRevision = m_status.revision + 1;
        m_status = {};
        m_status.running = true;
        m_status.total = static_cast<int>(apps.size());
        m_status.message = "Starting SteamGridDB scan...";
        m_status.progress01 = 0.f;
        m_status.revision = nextRevision;
    }
    m_task = std::async(std::launch::async,
                        [this, apiKey, apps]() mutable { scrape(std::move(apiKey), std::move(apps)); });
    return true;
}

bool SteamGridDbManager::startSelectNext(const std::string& apiKey,
                                         std::uint64_t titleId,
                                         const std::string& title,
                                         ArtworkKind kind, int direction) {
    if (apiKey.empty() || titleId == 0 || title.empty() || kind == ArtworkKind::None
        || m_running.exchange(true))
        return false;
    if (m_task.valid()) m_task.get();
    m_cancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::uint64_t nextRevision = m_status.revision + 1;
        m_status = {};
        m_status.running = true;
        m_status.total = 1;
        m_status.currentTitle = title;
        m_status.message = "Loading SteamGridDB choices...";
        m_status.progress01 = 0.f;
        m_status.selectedKind = kind;
        m_status.revision = nextRevision;
    }
    m_task = std::async(std::launch::async,
                        [this, apiKey, titleId, title, kind, direction]() mutable {
                            selectNext(std::move(apiKey), titleId, std::move(title), kind,
                                       direction < 0 ? -1 : 1);
                        });
    return true;
}

void SteamGridDbManager::selectNext(std::string apiKey, std::uint64_t titleId,
                                    std::string title, ArtworkKind kind, int direction) {
    const std::list<std::string> headers = {
        "Authorization: Bearer " + apiKey,
        "Accept: application/json",
    };
    bool saved = false;
    int selectedIndex = -1;
    int selectionCount = 0;
    std::string message;
    try {
        nlohmann::json metadata = readMetadata(titleId);
        long long gameId = metadata.value("steamGridDbGameId", 0LL);
        std::string matchedName = metadata.value("steamGridDbName", std::string());
        float matchScore = metadata.value("matchScore", 0.f);
        if (gameId <= 0 || metadata.value("matcherVersion", 0) != kMatcherVersion) {
            const auto games = apiData(std::string(kApiBase) + "/search/autocomplete/"
                                       + percentEncode(title), headers);
            std::string closest;
            bool ambiguous = false;
            const auto* game = chooseGame(games, title, matchScore, closest, ambiguous);
            if (!game) throw std::runtime_error("No sufficiently close game match");
            gameId = game->at("id").get<long long>();
            matchedName = game->value("name", title);
        }

        const auto info = kindInfo(kind);
        const std::string destination = pathForKind(kind, titleId);
        std::string url = std::string(kApiBase) + info.endpoint + "/game/"
                        + std::to_string(gameId) + "?nsfw=false&humor=false";
        if (info.useStaticTypes)
            url += "&types=static";
        url += info.querySuffix;
        const auto images = apiData(url, headers);
        const auto ranked = rankedImages(images, info.requireLandscape,
                                         info.requirePortrait);
        selectionCount = static_cast<int>(ranked.size());
        if (ranked.empty()) throw std::runtime_error("No artwork available");
        std::error_code existingEc;
        const bool hasCurrent = metadata.value(info.flag, false)
            && std::filesystem::exists(destination, existingEc);
        const int currentIndex = hasCurrent ? metadata.value(info.indexKey, 0)
                                           : (direction > 0 ? -1 : 0);
        selectedIndex = (currentIndex + direction + selectionCount) % selectionCount;
        const std::string imageUrl = ranked[(size_t)selectedIndex]->value("url", std::string());
        std::error_code ec;
        std::filesystem::create_directories(titleDirectory(titleId), ec);
        if (info.prepareCache)
            removePreparedArtwork(kind, destination);
        saved = !imageUrl.empty() && saveBytes(destination, themeshop::http::getBytes(imageUrl));
        if (!saved) throw std::runtime_error("Artwork download failed");
        if (!prepareArtwork(kind, destination))
            throw std::runtime_error("Artwork decode failed");

        metadata["titleId"] = titleId;
        metadata["title"] = title;
        metadata["steamGridDbGameId"] = gameId;
        metadata["steamGridDbName"] = matchedName;
        metadata["matcherVersion"] = kMatcherVersion;
        metadata["matchScore"] = matchScore;
        metadata[info.indexKey] = selectedIndex;
        metadata[info.flag] = true;
        std::ofstream output(titleDirectory(titleId) + "/metadata.json", std::ios::trunc);
        if (output.is_open()) output << metadata.dump(2);
        message = std::string(info.label) + " " + std::to_string(selectedIndex + 1) + "/"
                + std::to_string(selectionCount) + " applied";
    } catch (const std::exception& ex) {
        message = ex.what();
        DebugLog::log("[steamgriddb] manual selection '%s' failed: %s",
                      title.c_str(), ex.what());
    }

    updateStatus([&](Status& status) {
        status.running = false;
        status.finished = true;
        status.completed = 1;
        status.matched = saved ? 1 : 0;
        status.failed = saved ? 0 : 1;
        status.currentTitle.clear();
        status.message = message;
        status.lastCompletedTitleId = titleId;
        status.selectedKind = kind;
        status.selectedIndex = selectedIndex;
        status.selectionCount = selectionCount;
        status.progress01 = 1.f;
    });
    m_running.store(false);
}

SteamGridDbManager::Status SteamGridDbManager::status() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void SteamGridDbManager::updateStatus(const std::function<void(Status&)>& change) {
    std::lock_guard<std::mutex> lock(m_mutex);
    change(m_status);
    ++m_status.revision;
}

void SteamGridDbManager::scrape(std::string apiKey, std::vector<AppEntry> apps) {
    const std::list<std::string> headers = {
        "Authorization: Bearer " + apiKey,
        "Accept: application/json",
    };
    const themeshop::http::AbortCheck shouldAbort = [this]() {
        return m_cancelRequested.load();
    };

    std::error_code ec;
    std::filesystem::create_directories(kCacheRoot, ec);
    std::string fatalError;

    for (const auto& app : apps) {
        if (m_cancelRequested.load()) break;
        if (!app.isApplication() || app.titleId == 0) {
            updateStatus([](Status& s) {
                ++s.completed;
                s.progress01 = s.total > 0
                    ? static_cast<float>(s.completed) / static_cast<float>(s.total) : 1.f;
            });
            continue;
        }

        updateStatus([&](Status& s) {
            s.currentTitle = app.title;
            s.message = "Searching " + app.title;
            s.progress01 = s.total > 0
                ? static_cast<float>(s.completed) / static_cast<float>(s.total) : 0.f;
        });

        bool matched = false;
        // Incremental: keep trusted hero/logo choices, but still fill a missing
        // Poster (and landscape grid for 2x1 tiles) from the cached game id.
        if (hasArtwork(app.titleId)) {
            bool posterFilled = hasPoster(app.titleId);
            bool gridWideFilled = hasGridWide(app.titleId);
            const bool wantsGridWide =
                app.widgetColumns == 2 && app.widgetRows == 1;
            try {
                nlohmann::json metadata = readMetadata(app.titleId);
                const long long gameId = metadata.value("steamGridDbGameId", 0LL);
                if (gameId > 0 && cacheUsesCurrentMatcher(app.titleId)) {
                    auto fillKind = [&](ArtworkKind kind, bool portrait,
                                        const std::string& destination,
                                        const char* metaKey) -> bool {
                        const auto info = kindInfo(kind);
                        const std::string url = std::string(kApiBase) + info.endpoint
                            + "/game/" + std::to_string(gameId)
                            + "?nsfw=false&humor=false&types=static" + info.querySuffix;
                        const auto images = apiData(url, headers, shouldAbort);
                        const bool ok = fetchImage(images, portrait, destination,
                                                   {}, shouldAbort);
                        if (ok) {
                            prepareArtwork(kind, destination);
                            metadata[metaKey] = true;
                        }
                        return ok;
                    };
                    if (!posterFilled)
                        posterFilled = fillKind(ArtworkKind::Poster, true,
                                                posterPath(app.titleId), "poster");
                    if (wantsGridWide && !gridWideFilled)
                        gridWideFilled = fillKind(ArtworkKind::GridWide, false,
                                                  gridWidePath(app.titleId), "gridWide");
                    if (posterFilled || gridWideFilled) {
                        std::ofstream meta(titleDirectory(app.titleId) + "/metadata.json",
                                           std::ios::trunc);
                        if (meta.is_open()) meta << metadata.dump(2);
                    }
                }
            } catch (const std::exception& ex) {
                if (std::string(ex.what()) == "Cancelled" || m_cancelRequested.load())
                    break;
                DebugLog::log("[steamgriddb] '%s' incremental fill-in failed: %s",
                              app.title.c_str(), ex.what());
            }
            updateStatus([&](Status& s) {
                ++s.completed;
                ++s.matched;
                if (posterFilled || gridWideFilled)
                    s.lastCompletedTitleId = app.titleId;
                s.progress01 = s.total > 0
                    ? static_cast<float>(s.completed) / static_cast<float>(s.total) : 1.f;
            });
            continue;
        }

        // Version 1 accepted the first autocomplete result as a fallback. Its
        // cached files cannot be trusted (for example Nintendo Labo -> Land).
        removeCachedArtwork(app.titleId);

        try {
            if (m_cancelRequested.load()) break;
            const std::string& searchTitle = app.steamGridDbTitle();
            const auto games = apiData(std::string(kApiBase) + "/search/autocomplete/"
                                       + percentEncode(searchTitle), headers, shouldAbort);
            if (m_cancelRequested.load()) break;
            float matchScore = 0.f;
            std::string closest;
            bool ambiguous = false;
            const auto* game = chooseGame(games, searchTitle, matchScore, closest, ambiguous);
            if (!game) {
                DebugLog::log("[steamgriddb] display='%s' search='%s' rejected candidates=%d closest='%s' score=%.2f ambiguous=%d",
                              app.title.c_str(), searchTitle.c_str(),
                              games.is_array() ? static_cast<int>(games.size()) : 0,
                              closest.empty() ? "none" : closest.c_str(), matchScore,
                              ambiguous ? 1 : 0);
                throw std::runtime_error("No sufficiently close game match");
            }
            DebugLog::log("[steamgriddb] display='%s' search='%s' matched '%s' score=%.2f id=%lld",
                          app.title.c_str(), searchTitle.c_str(),
                          game->value("name", std::string()).c_str(),
                          matchScore, game->at("id").get<long long>());
            const long long gameId = game->at("id").get<long long>();

            const std::string dir = titleDirectory(app.titleId);
            std::filesystem::create_directories(dir, ec);
            const std::string gamePath = "/game/" + std::to_string(gameId);
            auto fetchArtwork = [&](ArtworkKind kind,
                                    bool portrait,
                                    const std::string& destination,
                                    float phaseStart, float phaseSpan) {
                if (m_cancelRequested.load())
                    throw std::runtime_error("Cancelled");
                const auto info = kindInfo(kind);
                try {
                    const std::string url = std::string(kApiBase) + info.endpoint + gamePath
                                          + "?nsfw=false&humor=false"
                                          + (info.useStaticTypes ? "&types=static" : "")
                                          + info.querySuffix;
                    const auto images = apiData(url, headers, shouldAbort);
                    if (m_cancelRequested.load())
                        throw std::runtime_error("Cancelled");
                    auto networkProgress = [&](std::uint64_t downloaded, std::uint64_t total) {
                        const float fraction = total > 0
                            ? std::clamp(static_cast<float>(downloaded)
                                         / static_cast<float>(total), 0.f, 1.f)
                            : 0.f;
                        updateStatus([&](Status& s) {
                            s.message = "Downloading artwork for " + app.title;
                            s.progress01 = s.total > 0
                                ? (static_cast<float>(s.completed)
                                   + phaseStart + phaseSpan * fraction)
                                    / static_cast<float>(s.total)
                                : 0.f;
                        });
                    };
                    const bool saved = fetchImage(images, portrait, destination,
                                                  networkProgress, shouldAbort);
                    if (m_cancelRequested.load())
                        throw std::runtime_error("Cancelled");
                    if (saved && info.prepareCache) {
                        updateStatus([&](Status& s) {
                            s.message = "Preparing artwork for " + app.title;
                            s.progress01 = s.total > 0
                                ? (static_cast<float>(s.completed) + phaseStart + phaseSpan)
                                    / static_cast<float>(s.total)
                                : 0.f;
                        });
                        if (!prepareArtwork(kind, destination))
                            throw std::runtime_error("Artwork decode failed");
                    }
                    DebugLog::log("[steamgriddb] '%s' %s candidates=%d saved=%d",
                                  app.title.c_str(), info.endpoint,
                                  images.is_array() ? static_cast<int>(images.size()) : 0,
                                  saved ? 1 : 0);
                    return saved;
                } catch (const std::exception& ex) {
                    if (std::string(ex.what()) == "Cancelled" || m_cancelRequested.load())
                        throw;
                    // A missing or unsupported asset must not discard others.
                    DebugLog::log("[steamgriddb] '%s' %s failed: %s",
                                  app.title.c_str(), info.endpoint, ex.what());
                    return false;
                }
            };

            const bool heroOk = fetchArtwork(ArtworkKind::Hero, false,
                                             heroPath(app.titleId), 0.08f, 0.20f);
            if (m_cancelRequested.load()) break;
            const bool logoOk = fetchArtwork(ArtworkKind::Logo, false,
                                             logoPath(app.titleId), 0.30f, 0.20f);
            if (m_cancelRequested.load()) break;
            const bool posterOk = fetchArtwork(ArtworkKind::Poster, true,
                                               posterPath(app.titleId), 0.52f, 0.20f);
            if (m_cancelRequested.load()) break;
            bool gridWideOk = false;
            if (app.widgetColumns == 2 && app.widgetRows == 1) {
                gridWideOk = fetchArtwork(ArtworkKind::GridWide, false,
                                          gridWidePath(app.titleId), 0.74f, 0.20f);
                if (m_cancelRequested.load()) break;
            }
            matched = heroOk || logoOk || posterOk || gridWideOk;

            nlohmann::json metadata;
            metadata["titleId"] = app.titleId;
            metadata["title"] = app.title;
            metadata["searchTitle"] = searchTitle;
            metadata["steamGridDbGameId"] = gameId;
            metadata["steamGridDbName"] = game->value("name", app.title);
            metadata["matcherVersion"] = kMatcherVersion;
            metadata["matchScore"] = matchScore;
            metadata["hero"] = heroOk;
            metadata["logo"] = logoOk;
            metadata["poster"] = posterOk;
            metadata["gridWide"] = gridWideOk;
            std::ofstream meta(dir + "/metadata.json", std::ios::trunc);
            if (meta.is_open()) meta << metadata.dump(2);
        } catch (const std::exception& ex) {
            if (std::string(ex.what()) == "Cancelled" || m_cancelRequested.load())
                break;
            DebugLog::log("[steamgriddb] '%s' failed: %s", app.title.c_str(), ex.what());
            const std::string error = ex.what();
            if (error.find("HTTP error 401") != std::string::npos
                || error.find("HTTP error 403") != std::string::npos) {
                fatalError = "The SteamGridDB API key was rejected";
            } else if (error.find("HTTP error 429") != std::string::npos) {
                fatalError = "SteamGridDB rate limit reached; try again later";
            }
        }

        if (m_cancelRequested.load())
            break;

        updateStatus([&](Status& s) {
            ++s.completed;
            if (matched) ++s.matched;
            else ++s.failed;
            s.lastCompletedTitleId = app.titleId;
            s.progress01 = s.total > 0
                ? static_cast<float>(s.completed) / static_cast<float>(s.total) : 1.f;
        });
        if (!fatalError.empty()) {
            updateStatus([&](Status& s) {
                s.failed += std::max(0, s.total - s.completed);
                s.completed = s.total;
                s.message = fatalError;
            });
            break;
        }
    }

    updateStatus([&](Status& s) {
        s.running = false;
        s.finished = true;
        s.currentTitle.clear();
        if (m_cancelRequested.load())
            s.message = "SteamGridDB scan cancelled";
        else if (!fatalError.empty())
            s.message = fatalError;
        else
            s.message = "SteamGridDB scan complete";
        s.progress01 = 1.f;
    });
    m_running.store(false);
}
