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
        std::uint64_t revision = 0;
    };

    SteamGridDbManager() = default;
    ~SteamGridDbManager();

    bool start(const std::string& apiKey, const std::vector<AppEntry>& apps);
    void wait();
    void cancelAndWait();
    Status status() const;
    bool running() const { return m_running.load(); }

    static constexpr const char* kCacheRoot = "sdmc:/config/SwitchU/steamgriddb";
    static std::string heroPath(std::uint64_t titleId);
    static std::string logoPath(std::uint64_t titleId);
    static std::string gridPath(std::uint64_t titleId);
    static bool hasArtwork(std::uint64_t titleId);

private:
    void scrape(std::string apiKey, std::vector<AppEntry> apps);
    void updateStatus(const std::function<void(Status&)>& change);

    mutable std::mutex m_mutex;
    Status m_status;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelRequested{false};
    std::future<void> m_task;
};
