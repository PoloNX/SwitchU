#include "SteamGridDbManager.hpp"

#include "core/DebugLog.hpp"
#include "themeshop/ThemeHttp.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <list>
#include <nlohmann/json.hpp>
#include <set>
#include <system_error>

namespace {

constexpr const char* kApiBase = "https://www.steamgriddb.com/api/v2";

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

    // Containment helps with harmless edition/subtitle suffixes, but only when
    // the shorter normalized title still represents most of the longer one.
    const std::size_t shorter = std::min(a.size(), b.size());
    const std::size_t longer = std::max(a.size(), b.size());
    const bool contained = a.find(b) != std::string::npos || b.find(a) != std::string::npos;
    const float containmentRatio = longer > 0 ? static_cast<float>(shorter) / longer : 0.f;
    const float tokenScore = coverage * 0.65f + jaccard * 0.35f;
    return contained && containmentRatio >= 0.68f
        ? std::max(tokenScore, 0.84f)
        : tokenScore;
}

nlohmann::json apiData(const std::string& url, const std::list<std::string>& headers) {
    auto body = themeshop::http::getText(url, headers);
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
        return metadata.value("matcherVersion", 0) == 2;
    } catch (...) {
        return false;
    }
}

void removeCachedArtwork(std::uint64_t titleId) {
    std::error_code ec;
    std::filesystem::remove(SteamGridDbManager::gridPath(titleId), ec);
    ec.clear();
    std::filesystem::remove(SteamGridDbManager::heroPath(titleId), ec);
    ec.clear();
    std::filesystem::remove(SteamGridDbManager::logoPath(titleId), ec);
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
                const std::string& path) {
    const auto* image = chooseImage(images, portrait);
    if (!image) return false;
    const std::string url = image->value("url", std::string());
    if (url.empty()) return false;
    return saveBytes(path, themeshop::http::getBytes(url));
}

} // namespace

SteamGridDbManager::~SteamGridDbManager() {
    cancelAndWait();
}

void SteamGridDbManager::wait() {
    if (m_task.valid()) m_task.wait();
}

void SteamGridDbManager::cancelAndWait() {
    m_cancelRequested.store(true);
    wait();
}

std::string SteamGridDbManager::heroPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/hero.img";
}

std::string SteamGridDbManager::logoPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/logo.img";
}

std::string SteamGridDbManager::gridPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/grid.img";
}

bool SteamGridDbManager::hasArtwork(std::uint64_t titleId) {
    std::error_code ec;
    return cacheUsesCurrentMatcher(titleId)
        && (std::filesystem::exists(heroPath(titleId), ec)
        || std::filesystem::exists(logoPath(titleId), ec)
        || std::filesystem::exists(gridPath(titleId), ec));
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
        m_status.revision = nextRevision;
    }
    m_task = std::async(std::launch::async,
                        [this, apiKey, apps]() mutable { scrape(std::move(apiKey), std::move(apps)); });
    return true;
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

    std::error_code ec;
    std::filesystem::create_directories(kCacheRoot, ec);
    std::string fatalError;

    for (const auto& app : apps) {
        if (m_cancelRequested.load()) break;
        if (!app.isApplication() || app.titleId == 0) {
            updateStatus([](Status& s) { ++s.completed; });
            continue;
        }

        updateStatus([&](Status& s) {
            s.currentTitle = app.title;
            s.message = "Searching " + app.title;
        });

        bool matched = false;
        std::error_code cacheEc;
        const bool fullyCached = cacheUsesCurrentMatcher(app.titleId)
            && std::filesystem::exists(gridPath(app.titleId), cacheEc)
            && std::filesystem::exists(heroPath(app.titleId), cacheEc)
            && std::filesystem::exists(logoPath(app.titleId), cacheEc);
        if (fullyCached) {
            updateStatus([](Status& s) {
                ++s.completed;
                ++s.matched;
            });
            continue;
        }

        // Version 1 accepted the first autocomplete result as a fallback. Its
        // cached files cannot be trusted (for example Nintendo Labo -> Land).
        removeCachedArtwork(app.titleId);

        try {
            const auto games = apiData(std::string(kApiBase) + "/search/autocomplete/"
                                       + percentEncode(app.title), headers);
            float matchScore = 0.f;
            std::string closest;
            bool ambiguous = false;
            const auto* game = chooseGame(games, app.title, matchScore, closest, ambiguous);
            if (!game) {
                DebugLog::log("[steamgriddb] '%s' rejected candidates=%d closest='%s' score=%.2f ambiguous=%d",
                              app.title.c_str(),
                              games.is_array() ? static_cast<int>(games.size()) : 0,
                              closest.empty() ? "none" : closest.c_str(), matchScore,
                              ambiguous ? 1 : 0);
                throw std::runtime_error("No sufficiently close game match");
            }
            DebugLog::log("[steamgriddb] '%s' matched '%s' score=%.2f id=%lld",
                          app.title.c_str(), game->value("name", std::string()).c_str(),
                          matchScore, game->at("id").get<long long>());
            const long long gameId = game->at("id").get<long long>();

            const std::string dir = titleDirectory(app.titleId);
            std::filesystem::create_directories(dir, ec);
            const std::string gamePath = "/game/" + std::to_string(gameId);
            auto fetchArtwork = [&](const char* endpoint,
                                    const char* mimeFilter,
                                    bool portrait,
                                    const std::string& destination) {
                try {
                    // SteamGridDB expects complete MIME values. Logos only
                    // support PNG/WebP, whereas grids and heroes accept JPEG.
                    const std::string url = std::string(kApiBase) + endpoint + gamePath
                                          + "?nsfw=false&humor=false&types=static&mimes="
                                          + mimeFilter;
                    const auto images = apiData(url, headers);
                    const bool saved = fetchImage(images, portrait, destination);
                    DebugLog::log("[steamgriddb] '%s' %s candidates=%d saved=%d",
                                  app.title.c_str(), endpoint,
                                  images.is_array() ? static_cast<int>(images.size()) : 0,
                                  saved ? 1 : 0);
                    return saved;
                } catch (const std::exception& ex) {
                    // A missing or unsupported logo must not discard a valid
                    // hero and cover for the same game.
                    DebugLog::log("[steamgriddb] '%s' %s failed: %s",
                                  app.title.c_str(), endpoint, ex.what());
                    return false;
                }
            };

            const bool gridOk = fetchArtwork("/grids", "image/png,image/jpeg", true,
                                             gridPath(app.titleId));
            const bool heroOk = fetchArtwork("/heroes", "image/png,image/jpeg", false,
                                             heroPath(app.titleId));
            const bool logoOk = fetchArtwork("/logos", "image/png", false,
                                             logoPath(app.titleId));
            matched = gridOk || heroOk || logoOk;

            nlohmann::json metadata;
            metadata["titleId"] = app.titleId;
            metadata["title"] = app.title;
            metadata["steamGridDbGameId"] = gameId;
            metadata["steamGridDbName"] = game->value("name", app.title);
            metadata["matcherVersion"] = 2;
            metadata["matchScore"] = matchScore;
            metadata["grid"] = gridOk;
            metadata["hero"] = heroOk;
            metadata["logo"] = logoOk;
            std::ofstream meta(dir + "/metadata.json", std::ios::trunc);
            if (meta.is_open()) meta << metadata.dump(2);
        } catch (const std::exception& ex) {
            DebugLog::log("[steamgriddb] '%s' failed: %s", app.title.c_str(), ex.what());
            const std::string error = ex.what();
            if (error.find("HTTP error 401") != std::string::npos
                || error.find("HTTP error 403") != std::string::npos) {
                fatalError = "The SteamGridDB API key was rejected";
            } else if (error.find("HTTP error 429") != std::string::npos) {
                fatalError = "SteamGridDB rate limit reached; try again later";
            }
        }

        updateStatus([&](Status& s) {
            ++s.completed;
            if (matched) ++s.matched;
            else ++s.failed;
            s.lastCompletedTitleId = app.titleId;
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
    });
    m_running.store(false);
}
