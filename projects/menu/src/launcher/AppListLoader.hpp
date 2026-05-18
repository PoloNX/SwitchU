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

struct PendingApp {
    std::string         id;
    std::string         title;
    uint64_t            titleId = 0;
    uint32_t            viewFlags = 0;
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

    void finalize(GridModel& model, IconStreamer& streamer);

    void setPendingTransform(PendingTransform transform) {
        m_pendingTransform = std::move(transform);
    }

private:
    void fetchApps();

    std::future<void>       m_future;
    std::vector<PendingApp> m_pending;
    PendingTransform        m_pendingTransform;
};
