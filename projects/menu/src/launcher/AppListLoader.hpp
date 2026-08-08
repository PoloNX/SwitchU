#pragma once
#include "core/GridModel.hpp"
#include "launcher/IconStreamer.hpp"
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <vector>
#include <string>
#include <future>
#include <cstdint>
#include <functional>
#include <exception>
#include <memory>

struct PendingApp {
    std::string         id;
    std::string         title;
    uint64_t            titleId = 0;
    uint32_t            viewFlags = 0;
    bool                userRequired = true;
    bool                startupUserKnown = true;
    uint8_t             startupUserAccount = 1;
    uint8_t             startupUserAccountOption = 0;
    std::vector<uint8_t> iconData;
};

class AppListLoader {
public:
    using PendingTransform = std::function<void(std::vector<PendingApp>&)>;
    static std::vector<uint8_t> loadIconData(uint64_t titleId);

    // Streaming path: fetch apps and hand compressed icon data to the streamer.
    void load(GridModel& model, IconStreamer& streamer);

    void startAsync(nxui::ThreadPool& pool);

    bool isReady() const;

    bool finalize(GridModel& model, IconStreamer& streamer);

    void setPendingTransform(PendingTransform transform) {
        m_pendingTransform = std::move(transform);
    }
    void setPrefetchIcons(bool enabled) { m_prefetchIcons = enabled; }

private:
    static void fetchApps(std::vector<PendingApp>& output, bool prefetchIcons);

    struct AsyncLoadState {
        std::vector<PendingApp> pending;
        std::exception_ptr error;
    };

    std::future<void>       m_future;
    std::vector<PendingApp> m_pending;
    std::shared_ptr<AsyncLoadState> m_asyncState;
    PendingTransform        m_pendingTransform;
    bool                    m_prefetchIcons = true;
};
