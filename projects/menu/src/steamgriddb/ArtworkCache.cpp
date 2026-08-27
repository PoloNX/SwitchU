#include "ArtworkCache.hpp"

#include "core/DebugLog.hpp"

#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

constexpr std::uint32_t kArtworkCacheMagic = 0x43415553; // "SUAC"
constexpr std::uint32_t kArtworkCacheVersion = 1;

struct ArtworkCacheHeader {
    std::uint32_t magic = kArtworkCacheMagic;
    std::uint32_t version = kArtworkCacheVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t sourceSize = 0;
    std::int64_t sourceWriteTime = 0;
    std::uint64_t payloadSize = 0;
};
static_assert(sizeof(ArtworkCacheHeader) == 40);

struct SourceStamp {
    std::uint64_t size = 0;
    std::int64_t writeTime = 0;
    bool valid = false;
};

SourceStamp sourceStamp(const std::string& path) {
    std::error_code ec;
    SourceStamp stamp;
    stamp.size = std::filesystem::file_size(path, ec);
    if (ec) return stamp;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) return stamp;
    stamp.writeTime = static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    stamp.valid = true;
    return stamp;
}

std::string cachePathFor(const std::string& source, int width, int height) {
    return source + "." + std::to_string(width) + "x" + std::to_string(height)
        + ".rgba-cache";
}

bool load(const std::string& source, int width, int height,
          std::vector<std::uint8_t>& rgba) {
    const SourceStamp stamp = sourceStamp(source);
    if (!stamp.valid) return false;
    const std::string cachePath = cachePathFor(source, width, height);
    const auto started = std::chrono::steady_clock::now();
    std::ifstream input(cachePath, std::ios::binary);
    if (!input) return false;

    ArtworkCacheHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    const std::uint64_t expected = static_cast<std::uint64_t>(width)
                                 * static_cast<std::uint64_t>(height) * 4ULL;
    if (!input || header.magic != kArtworkCacheMagic
        || header.version != kArtworkCacheVersion
        || header.width != static_cast<std::uint32_t>(width)
        || header.height != static_cast<std::uint32_t>(height)
        || header.sourceSize != stamp.size
        || header.sourceWriteTime != stamp.writeTime
        || header.payloadSize != expected) {
        return false;
    }

    rgba.resize(static_cast<std::size_t>(expected));
    input.read(reinterpret_cast<char*>(rgba.data()), static_cast<std::streamsize>(expected));
    if (!input) {
        rgba.clear();
        return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    DebugLog::log("[steamgriddb-ui] raw cache hit path=%s bytes=%llu read=%ldms",
                  cachePath.c_str(), static_cast<unsigned long long>(expected), elapsed);
    return true;
}

void save(const std::string& source, int width, int height,
          const std::vector<std::uint8_t>& rgba) {
    const SourceStamp stamp = sourceStamp(source);
    const std::uint64_t expected = static_cast<std::uint64_t>(width)
                                 * static_cast<std::uint64_t>(height) * 4ULL;
    if (!stamp.valid || rgba.size() != expected) return;

    ArtworkCacheHeader header;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.sourceSize = stamp.size;
    header.sourceWriteTime = stamp.writeTime;
    header.payloadSize = expected;

    const std::string cachePath = cachePathFor(source, width, height);
    const std::string temporaryPath = cachePath + ".tmp";
    const auto started = std::chrono::steady_clock::now();
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(rgba.data()),
                 static_cast<std::streamsize>(rgba.size()));
    output.close();
    if (!output) {
        std::error_code cleanupEc;
        std::filesystem::remove(temporaryPath, cleanupEc);
        return;
    }
    std::error_code ec;
    std::filesystem::remove(cachePath, ec);
    ec.clear();
    std::filesystem::rename(temporaryPath, cachePath, ec);
    if (ec) {
        std::filesystem::remove(temporaryPath, ec);
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    DebugLog::log("[steamgriddb-ui] raw cache saved path=%s bytes=%llu write=%ldms",
                  cachePath.c_str(), static_cast<unsigned long long>(expected), elapsed);
}

} // namespace

namespace steamgriddb::artwork {

DecodedImage decode(const std::string& source, int outputWidth, int outputHeight,
                    bool fill) {
    DecodedImage out;
    out.width = std::max(1, outputWidth);
    out.height = std::max(1, outputHeight);
    if (load(source, out.width, out.height, out.rgba)) return out;

    const auto loadStarted = std::chrono::steady_clock::now();
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load(source.c_str(), &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) stbi_image_free(raw);
        return out;
    }
    const auto loadedAt = std::chrono::steady_clock::now();

    if (width == out.width && height == out.height) {
        out.rgba.assign(raw, raw + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(raw);
        save(source, out.width, out.height, out.rgba);
        DebugLog::log("[steamgriddb-ui] image path=%s source=%dx%d load=%ldms scale=0ms",
                      source.c_str(), width, height,
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          loadedAt - loadStarted).count());
        return out;
    }

    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    const float scaleX = static_cast<float>(out.width) / width;
    const float scaleY = static_cast<float>(out.height) / height;
    const float scale = fill ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
    const float drawnWidth = width * scale;
    const float drawnHeight = height * scale;
    const float offsetX = (out.width - drawnWidth) * 0.5f;
    const float offsetY = (out.height - drawnHeight) * 0.5f;
    const float inverseScale = 1.f / scale;

    std::vector<int> sourceXs(static_cast<std::size_t>(out.width), -1);
    std::vector<int> sourceYs(static_cast<std::size_t>(out.height), -1);
    for (int x = 0; x < out.width; ++x) {
        const float pixel = (x - offsetX) * inverseScale;
        if (fill || (pixel >= 0.f && pixel < width))
            sourceXs[static_cast<std::size_t>(x)] =
                std::clamp(static_cast<int>(pixel), 0, width - 1);
    }
    for (int y = 0; y < out.height; ++y) {
        const float pixel = (y - offsetY) * inverseScale;
        if (fill || (pixel >= 0.f && pixel < height))
            sourceYs[static_cast<std::size_t>(y)] =
                std::clamp(static_cast<int>(pixel), 0, height - 1);
    }

    const auto* sourcePixels = reinterpret_cast<const std::uint32_t*>(raw);
    auto* destinationPixels = reinterpret_cast<std::uint32_t*>(out.rgba.data());
    for (int y = 0; y < out.height; ++y) {
        const int sourceY = sourceYs[static_cast<std::size_t>(y)];
        if (sourceY < 0) continue;
        auto* destinationRow = destinationPixels + static_cast<std::size_t>(y) * out.width;
        const auto* sourceRow = sourcePixels + static_cast<std::size_t>(sourceY) * width;
        for (int x = 0; x < out.width; ++x) {
            const int sourceX = sourceXs[static_cast<std::size_t>(x)];
            if (sourceX >= 0) destinationRow[x] = sourceRow[sourceX];
        }
    }
    const auto scaledAt = std::chrono::steady_clock::now();
    stbi_image_free(raw);
    save(source, out.width, out.height, out.rgba);
    DebugLog::log("[steamgriddb-ui] image path=%s source=%dx%d load=%ldms scale=%ldms",
                  source.c_str(), width, height,
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      loadedAt - loadStarted).count(),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      scaledAt - loadedAt).count());
    return out;
}

bool prepare(const std::string& source, int outputWidth, int outputHeight, bool fill) {
    return !decode(source, outputWidth, outputHeight, fill).rgba.empty();
}

void remove(const std::string& source, int outputWidth, int outputHeight) {
    std::error_code ec;
    std::filesystem::remove(cachePathFor(source, outputWidth, outputHeight), ec);
}

} // namespace steamgriddb::artwork
