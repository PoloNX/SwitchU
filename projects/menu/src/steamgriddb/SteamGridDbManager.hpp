#pragma once

#include "core/GridModel.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class SteamGridDbManager {
public:
    using ProgressCallback = std::function<void(const std::string&, float)>;
    enum class ArtworkKind { None, Hero, Logo, Icon };

    struct Candidate {
        long long id = 0;
        std::string url;
        std::string thumbnailUrl;
        int width = 0;
        int height = 0;
    };

    struct GameMatch {
        long long id = 0;
        std::string name;
        float score = 0.f;
    };

    struct BrowseResult {
        bool success = false;
        std::uint64_t titleId = 0;
        std::string title;
        std::string query;
        ArtworkKind kind = ArtworkKind::None;
        long long gameId = 0;
        std::string gameName;
        float matchScore = 0.f;
        std::string error;
        // When non-empty and candidates empty, the picker should let the user
        // choose a SteamGridDB game before loading artwork.
        std::vector<GameMatch> gameMatches;
        std::vector<Candidate> candidates;
    };

    struct ApplyResult {
        bool success = false;
        std::uint64_t titleId = 0;
        ArtworkKind kind = ArtworkKind::None;
        std::string message;
    };

    struct Status {
        bool running = false;
        bool finished = false;
        int completed = 0;
        int total = 0;
        int matched = 0;
        int failed = 0;
        std::string currentTitle;
        std::string message;
        std::uint64_t lastCompletedTitleId = 0;
        ArtworkKind selectedKind = ArtworkKind::None;
        int selectedIndex = -1;
        int selectionCount = 0;
        float progress01 = 0.f;
        std::uint64_t revision = 0;
    };

    SteamGridDbManager() = default;
    ~SteamGridDbManager();

    bool start(const std::string& apiKey, const std::vector<AppEntry>& apps);
    bool startSelectNext(const std::string& apiKey, std::uint64_t titleId,
                         const std::string& title, ArtworkKind kind, int direction);
    void wait();
    void cancelAndWait();
    /// Non-blocking: scrape loop stops between titles. Prefer this from UI.
    void requestCancel();
    Status status() const;
    bool running() const { return m_running.load(); }

    static BrowseResult browse(const std::string& apiKey, std::uint64_t titleId,
                               const std::string& title, const std::string& query,
                               ArtworkKind kind);
    /// Load artwork candidates for an already-chosen SteamGridDB game id.
    static BrowseResult browseGame(const std::string& apiKey, std::uint64_t titleId,
                                   const std::string& title, const std::string& query,
                                   ArtworkKind kind, long long gameId,
                                   const std::string& gameName);
    static ApplyResult applyCandidate(const BrowseResult& browse,
                                      const Candidate& candidate,
                                      const ProgressCallback& onProgress = {});
    /// Clear cached art. Pass ArtworkKind::None to clear hero+logo+icon.
    static bool clearArtwork(std::uint64_t titleId, ArtworkKind kind);
    static bool hasIcon(std::uint64_t titleId);

    static constexpr const char* kCacheRoot = "sdmc:/config/SwitchU/steamgriddb";
    static std::string heroPath(std::uint64_t titleId);
    static std::string logoPath(std::uint64_t titleId);
    static std::string iconPath(std::uint64_t titleId);
    static bool hasArtwork(std::uint64_t titleId);

private:
    void scrape(std::string apiKey, std::vector<AppEntry> apps);
    void selectNext(std::string apiKey, std::uint64_t titleId,
                    std::string title, ArtworkKind kind, int direction);
    void updateStatus(const std::function<void(Status&)>& change);

    mutable std::mutex m_mutex;
    Status m_status;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelRequested{false};
    std::future<void> m_task;
};
