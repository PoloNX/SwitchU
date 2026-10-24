#include "ArtworkCache.hpp"

#include "core/DebugLog.hpp"

#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace {

constexpr std::uint32_t kArtworkCacheMagic = 0x43415553; // "SUAC"
constexpr std::uint32_t kArtworkCacheVersion = 1;

// Decoded fit RGBA in RAM so neighbour poster warm after layout morph skips
// repeated SD .fit320x480.rgba-cache reads (~10–30ms each). ~16×320×480×4 ≈ 9.4MB.
constexpr std::size_t kFitRamMaxEntries = 16;
constexpr std::size_t kFitRamMaxBytes = 10ull * 1024ull * 1024ull;

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

std::string fitCachePathFor(const std::string& source, int maxWidth, int maxHeight) {
    return source + ".fit" + std::to_string(maxWidth) + "x" + std::to_string(maxHeight)
        + ".rgba-cache";
}

std::string fitRamKey(const std::string& source, int maxWidth, int maxHeight) {
    return source + "\n" + std::to_string(maxWidth) + "x" + std::to_string(maxHeight);
}

struct FitRamEntry {
    std::string key;
    SourceStamp stamp;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct FitRamCache {
    std::mutex mutex;
    std::list<FitRamEntry> order; // front = most recently used
    std::unordered_map<std::string, std::list<FitRamEntry>::iterator> index;
    std::size_t bytes = 0;

    void evictIfNeeded() {
        while ((!order.empty())
               && (order.size() > kFitRamMaxEntries || bytes > kFitRamMaxBytes)) {
            const FitRamEntry& old = order.back();
            bytes -= old.rgba.size();
            index.erase(old.key);
            order.pop_back();
        }
    }

    bool tryGet(const std::string& key, const SourceStamp& stamp,
                std::vector<std::uint8_t>& rgba, int& outWidth, int& outHeight) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = index.find(key);
        if (found == index.end())
            return false;
        auto it = found->second;
        if (!stamp.valid
            || it->stamp.size != stamp.size
            || it->stamp.writeTime != stamp.writeTime
            || it->rgba.empty()) {
            bytes -= it->rgba.size();
            order.erase(it);
            index.erase(found);
            return false;
        }
        order.splice(order.begin(), order, it);
        rgba = it->rgba;
        outWidth = it->width;
        outHeight = it->height;
        return true;
    }

    void put(const std::string& key, const SourceStamp& stamp,
             int width, int height, const std::vector<std::uint8_t>& rgba) {
        if (!stamp.valid || width <= 0 || height <= 0 || rgba.empty())
            return;
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = index.find(key);
        if (found != index.end()) {
            bytes -= found->second->rgba.size();
            order.erase(found->second);
            index.erase(found);
        }
        order.push_front(FitRamEntry{key, stamp, width, height, rgba});
        index[key] = order.begin();
        bytes += rgba.size();
        evictIfNeeded();
    }

    void erase(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = index.find(key);
        if (found == index.end())
            return;
        bytes -= found->second->rgba.size();
        order.erase(found->second);
        index.erase(found);
    }
};

FitRamCache& fitRamCache() {
    static FitRamCache cache;
    return cache;
}

bool loadFit(const std::string& source, int maxWidth, int maxHeight,
             std::vector<std::uint8_t>& rgba, int& outWidth, int& outHeight) {
    const SourceStamp stamp = sourceStamp(source);
    if (!stamp.valid) return false;
    const std::string ramKey = fitRamKey(source, maxWidth, maxHeight);
    if (fitRamCache().tryGet(ramKey, stamp, rgba, outWidth, outHeight)) {
        DebugLog::log("[steamgriddb-ui] fit ram hit path=%s %dx%d",
                      source.c_str(), outWidth, outHeight);
        return true;
    }

    const std::string cachePath = fitCachePathFor(source, maxWidth, maxHeight);
    const auto started = std::chrono::steady_clock::now();
    std::ifstream input(cachePath, std::ios::binary);
    if (!input) return false;

    ArtworkCacheHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!input || header.magic != kArtworkCacheMagic
        || header.version != kArtworkCacheVersion
        || header.width == 0 || header.height == 0
        || header.width > static_cast<std::uint32_t>(maxWidth)
        || header.height > static_cast<std::uint32_t>(maxHeight)
        || header.sourceSize != stamp.size
        || header.sourceWriteTime != stamp.writeTime
        || header.payloadSize
            != static_cast<std::uint64_t>(header.width) * header.height * 4ULL) {
        return false;
    }

    rgba.resize(static_cast<std::size_t>(header.payloadSize));
    input.read(reinterpret_cast<char*>(rgba.data()),
               static_cast<std::streamsize>(header.payloadSize));
    if (!input) {
        rgba.clear();
        return false;
    }
    outWidth = static_cast<int>(header.width);
    outHeight = static_cast<int>(header.height);
    fitRamCache().put(ramKey, stamp, outWidth, outHeight, rgba);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    DebugLog::log("[steamgriddb-ui] fit cache hit path=%s %dx%d read=%ldms",
                  cachePath.c_str(), outWidth, outHeight, elapsed);
    return true;
}

void saveFit(const std::string& source, int maxWidth, int maxHeight,
             int width, int height, const std::vector<std::uint8_t>& rgba) {
    const SourceStamp stamp = sourceStamp(source);
    const std::uint64_t expected = static_cast<std::uint64_t>(width)
                                 * static_cast<std::uint64_t>(height) * 4ULL;
    if (!stamp.valid || width <= 0 || height <= 0 || width > maxWidth
        || height > maxHeight || rgba.size() != expected) {
        return;
    }

    ArtworkCacheHeader header;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.sourceSize = stamp.size;
    header.sourceWriteTime = stamp.writeTime;
    header.payloadSize = expected;

    const std::string cachePath = fitCachePathFor(source, maxWidth, maxHeight);
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
    DebugLog::log("[steamgriddb-ui] fit cache saved path=%s %dx%d write=%ldms",
                  cachePath.c_str(), width, height, elapsed);
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

DecodedImage decodeFit(const std::string& source, int maxWidth, int maxHeight) {
    DecodedImage out;
    const int limitW = std::max(1, maxWidth);
    const int limitH = std::max(1, maxHeight);
    if (loadFit(source, limitW, limitH, out.rgba, out.width, out.height))
        return out;

    const auto loadStarted = std::chrono::steady_clock::now();
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load(source.c_str(), &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) stbi_image_free(raw);
        return out;
    }
    const auto loadedAt = std::chrono::steady_clock::now();

    int dstW = width;
    int dstH = height;
    if (width > limitW || height > limitH) {
        const float scale = std::min(static_cast<float>(limitW) / static_cast<float>(width),
                                     static_cast<float>(limitH) / static_cast<float>(height));
        dstW = std::max(1, static_cast<int>(static_cast<float>(width) * scale + 0.5f));
        dstH = std::max(1, static_cast<int>(static_cast<float>(height) * scale + 0.5f));
    }

    out.width = dstW;
    out.height = dstH;
    const SourceStamp stamp = sourceStamp(source);
    const std::string ramKey = fitRamKey(source, limitW, limitH);
    if (dstW == width && dstH == height) {
        out.rgba.assign(raw, raw + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(raw);
        saveFit(source, limitW, limitH, out.width, out.height, out.rgba);
        fitRamCache().put(ramKey, stamp, out.width, out.height, out.rgba);
        DebugLog::log("[steamgriddb-ui] fit path=%s source=%dx%d load=%ldms scale=0ms",
                      source.c_str(), width, height,
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          loadedAt - loadStarted).count());
        return out;
    }

    out.rgba.assign(static_cast<std::size_t>(dstW) * dstH * 4, 0);
    const float scaleX = static_cast<float>(width) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(height) / static_cast<float>(dstH);
    for (int y = 0; y < dstH; ++y) {
        const float srcYf = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        int y0 = static_cast<int>(srcYf);
        if (y0 < 0) y0 = 0;
        int y1 = y0 + 1;
        if (y1 >= height) y1 = height - 1;
        const float fy = srcYf - static_cast<float>(y0);
        for (int x = 0; x < dstW; ++x) {
            const float srcXf = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            int x0 = static_cast<int>(srcXf);
            if (x0 < 0) x0 = 0;
            int x1 = x0 + 1;
            if (x1 >= width) x1 = width - 1;
            const float fx = srcXf - static_cast<float>(x0);
            const std::uint8_t* p00 = raw + (static_cast<std::size_t>(y0) * width + x0) * 4;
            const std::uint8_t* p10 = raw + (static_cast<std::size_t>(y0) * width + x1) * 4;
            const std::uint8_t* p01 = raw + (static_cast<std::size_t>(y1) * width + x0) * 4;
            const std::uint8_t* p11 = raw + (static_cast<std::size_t>(y1) * width + x1) * 4;
            std::uint8_t* dst = out.rgba.data()
                + (static_cast<std::size_t>(y) * dstW + x) * 4;
            for (int c = 0; c < 4; ++c) {
                dst[c] = static_cast<std::uint8_t>(
                    p00[c] * (1.f - fx) * (1.f - fy)
                    + p10[c] * fx * (1.f - fy)
                    + p01[c] * (1.f - fx) * fy
                    + p11[c] * fx * fy + 0.5f);
            }
        }
    }
    stbi_image_free(raw);
    saveFit(source, limitW, limitH, out.width, out.height, out.rgba);
    fitRamCache().put(ramKey, stamp, out.width, out.height, out.rgba);
    DebugLog::log("[steamgriddb-ui] fit path=%s source=%dx%d -> %dx%d load=%ldms scale=%ldms",
                  source.c_str(), width, height, dstW, dstH,
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      loadedAt - loadStarted).count(),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - loadedAt).count());
    return out;
}

bool prepareFit(const std::string& source, int maxWidth, int maxHeight) {
    return !decodeFit(source, maxWidth, maxHeight).rgba.empty();
}

void removeFit(const std::string& source, int maxWidth, int maxHeight) {
    fitRamCache().erase(fitRamKey(source, maxWidth, maxHeight));
    std::error_code ec;
    std::filesystem::remove(fitCachePathFor(source, maxWidth, maxHeight), ec);
}

} // namespace steamgriddb::artwork
