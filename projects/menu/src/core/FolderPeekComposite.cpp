#include "FolderPeekComposite.hpp"

#include "launcher/AppListLoader.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"
#include "core/DebugLog.hpp"

#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace switchu::folders::peek_composite {
namespace {

constexpr std::uint32_t kMagic = 0x43465053; // "SPFC" SwitchU Peek Folder Composite
constexpr std::uint32_t kVersion = 1;
constexpr const char* kCacheDir = "sdmc:/config/SwitchU/folder_peeks";

struct SourceStamp {
    std::uint64_t size = 0;
    std::int64_t writeTime = 0;
};

struct Header {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t folderId = 0;
    std::uint32_t colorIndex = 0;
    std::uint32_t layoutVersion = kLayoutVersion;
    std::uint32_t reserved = 0;
    std::uint64_t titleIds[kFolderPeekMax]{};
    std::uint64_t sourceSize[kFolderPeekMax]{};
    std::int64_t sourceWriteTime[kFolderPeekMax]{};
    std::uint64_t payloadSize = 0;
};
static_assert(sizeof(Header) == 184);

SourceStamp stampForTitle(std::uint64_t titleId) {
    SourceStamp stamp;
    if (titleId == 0)
        return stamp;
    const std::string path = SteamGridDbManager::iconPath(titleId);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return stamp;
    stamp.size = std::filesystem::file_size(path, ec);
    if (ec)
        return SourceStamp{};
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec)
        return SourceStamp{};
    stamp.writeTime = static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    return stamp;
}

void fillHeader(Header& header, const Folder& folder,
                const std::array<std::uint64_t, kFolderPeekMax>& titles) {
    header = Header{};
    header.width = kWidth;
    header.height = kHeight;
    header.folderId = folder.id;
    header.colorIndex = static_cast<std::uint32_t>(std::max(0, folder.colorIndex));
    header.layoutVersion = kLayoutVersion;
    for (int i = 0; i < kFolderPeekMax; ++i) {
        header.titleIds[i] = titles[static_cast<std::size_t>(i)];
        const SourceStamp stamp = stampForTitle(titles[static_cast<std::size_t>(i)]);
        header.sourceSize[i] = stamp.size;
        header.sourceWriteTime[i] = stamp.writeTime;
    }
    header.payloadSize = static_cast<std::uint64_t>(kWidth) * kHeight * 4u;
}

bool headerMatches(const Header& header, const Folder& folder,
                   const std::array<std::uint64_t, kFolderPeekMax>& titles) {
    if (header.magic != kMagic || header.version != kVersion)
        return false;
    if (header.width != static_cast<std::uint32_t>(kWidth)
        || header.height != static_cast<std::uint32_t>(kHeight))
        return false;
    if (header.folderId != folder.id)
        return false;
    if (header.colorIndex != static_cast<std::uint32_t>(std::max(0, folder.colorIndex)))
        return false;
    if (header.layoutVersion != kLayoutVersion)
        return false;
    if (header.payloadSize != static_cast<std::uint64_t>(kWidth) * kHeight * 4u)
        return false;
    for (int i = 0; i < kFolderPeekMax; ++i) {
        if (header.titleIds[i] != titles[static_cast<std::size_t>(i)])
            return false;
        const SourceStamp stamp = stampForTitle(titles[static_cast<std::size_t>(i)]);
        if (header.sourceSize[i] != stamp.size
            || header.sourceWriteTime[i] != stamp.writeTime)
            return false;
    }
    return true;
}

// Coverage of a point inside a rounded rect (1 = inside, 0 = outside).
float roundedCoverage(float px, float py, float w, float h, float rad) {
    const float r = std::min(rad, std::min(w, h) * 0.5f);
    if (r <= 0.5f)
        return (px >= 0.f && py >= 0.f && px < w && py < h) ? 1.f : 0.f;
    float cx = px;
    float cy = py;
    if (cx < r && cy < r) {
        const float dx = r - cx;
        const float dy = r - cy;
        return (dx * dx + dy * dy) <= r * r ? 1.f : 0.f;
    }
    if (cx >= w - r && cy < r) {
        const float dx = cx - (w - r);
        const float dy = r - cy;
        return (dx * dx + dy * dy) <= r * r ? 1.f : 0.f;
    }
    if (cx < r && cy >= h - r) {
        const float dx = r - cx;
        const float dy = cy - (h - r);
        return (dx * dx + dy * dy) <= r * r ? 1.f : 0.f;
    }
    if (cx >= w - r && cy >= h - r) {
        const float dx = cx - (w - r);
        const float dy = cy - (h - r);
        return (dx * dx + dy * dy) <= r * r ? 1.f : 0.f;
    }
    return (px >= 0.f && py >= 0.f && px < w && py < h) ? 1.f : 0.f;
}

void blendPixel(std::uint8_t* d, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                std::uint8_t a) {
    if (a == 0)
        return;
    if (a >= 255 || d[3] == 0) {
        d[0] = r;
        d[1] = g;
        d[2] = b;
        d[3] = a;
        return;
    }
    const float sa = a / 255.f;
    const float da = d[3] / 255.f;
    const float outA = sa + da * (1.f - sa);
    if (outA <= 0.001f) {
        d[0] = d[1] = d[2] = d[3] = 0;
        return;
    }
    d[0] = static_cast<std::uint8_t>((r * sa + d[0] * da * (1.f - sa)) / outA + 0.5f);
    d[1] = static_cast<std::uint8_t>((g * sa + d[1] * da * (1.f - sa)) / outA + 0.5f);
    d[2] = static_cast<std::uint8_t>((b * sa + d[2] * da * (1.f - sa)) / outA + 0.5f);
    d[3] = static_cast<std::uint8_t>(outA * 255.f + 0.5f);
}

void fillRounded(std::uint8_t* canvas, int canvasW, int canvasH,
                 int dx, int dy, int dw, int dh, float rad,
                 std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const float cover = roundedCoverage(x + 0.5f, y + 0.5f,
                                                static_cast<float>(dw),
                                                static_cast<float>(dh), rad);
            if (cover <= 0.f)
                continue;
            const int cx = dx + x;
            const int cy = dy + y;
            if (cx < 0 || cy < 0 || cx >= canvasW || cy >= canvasH)
                continue;
            std::uint8_t* d = canvas + (static_cast<std::size_t>(cy) * canvasW + cx) * 4;
            blendPixel(d, r, g, b, static_cast<std::uint8_t>(a * cover + 0.5f));
        }
    }
}

void blitScaledRounded(const std::uint8_t* src, int sw, int sh,
                       std::uint8_t* dstCanvas, int canvasW, int canvasH,
                       int dx, int dy, int dw, int dh, float rad) {
    if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(sh - 1, (y * sh) / dh);
        for (int x = 0; x < dw; ++x) {
            const float cover = roundedCoverage(x + 0.5f, y + 0.5f,
                                                static_cast<float>(dw),
                                                static_cast<float>(dh), rad);
            if (cover <= 0.f)
                continue;
            const int sx = std::min(sw - 1, (x * sw) / dw);
            const int cx = dx + x;
            const int cy = dy + y;
            if (cx < 0 || cy < 0 || cx >= canvasW || cy >= canvasH)
                continue;
            const std::uint8_t* s = src + (static_cast<std::size_t>(sy) * sw + sx) * 4;
            std::uint8_t* d = dstCanvas + (static_cast<std::size_t>(cy) * canvasW + cx) * 4;
            const std::uint8_t a = static_cast<std::uint8_t>(s[3] * cover + 0.5f);
            blendPixel(d, s[0], s[1], s[2], a);
        }
    }
}

// Soft white rim matching drawPeekGrid's outline.
void strokeRounded(std::uint8_t* canvas, int canvasW, int canvasH,
                   int dx, int dy, int dw, int dh, float rad,
                   std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a,
                   float stroke) {
    const float inner = std::max(0.f, rad - stroke);
    for (int y = -1; y <= dh; ++y) {
        for (int x = -1; x <= dw; ++x) {
            const float outer = roundedCoverage(x + 0.5f, y + 0.5f,
                                                static_cast<float>(dw),
                                                static_cast<float>(dh), rad);
            if (outer <= 0.f)
                continue;
            const float hole = (x >= 0 && y >= 0 && x < dw && y < dh)
                ? roundedCoverage(x + 0.5f, y + 0.5f,
                                  static_cast<float>(dw),
                                  static_cast<float>(dh), inner)
                : 0.f;
            const float edge = std::clamp(outer - hole, 0.f, 1.f);
            if (edge <= 0.01f)
                continue;
            const int cx = dx + x;
            const int cy = dy + y;
            if (cx < 0 || cy < 0 || cx >= canvasW || cy >= canvasH)
                continue;
            std::uint8_t* d = canvas + (static_cast<std::size_t>(cy) * canvasW + cx) * 4;
            blendPixel(d, r, g, b, static_cast<std::uint8_t>(a * edge + 0.5f));
        }
    }
}

} // namespace

std::array<std::uint64_t, kFolderPeekMax> memberTitleIds(const Folder& folder) {
    std::array<std::uint64_t, kFolderPeekMax> ids{};
    int count = 0;
    for (const std::uint64_t titleId : folder.titleIds) {
        if (titleId == 0)
            continue;
        ids[static_cast<std::size_t>(count++)] = titleId;
        if (count >= kFolderPeekMax)
            break;
    }
    return ids;
}

std::string cachePath(std::uint32_t folderId) {
    char name[64];
    std::snprintf(name, sizeof(name), "%08X.2x3.%dx%d.rgba-cache",
                  folderId, kWidth, kHeight);
    return std::string(kCacheDir) + "/" + name;
}

void remove(std::uint32_t folderId) {
    std::error_code ec;
    std::filesystem::remove(cachePath(folderId), ec);
}

bool save(const Folder& folder, const BakedImage& image) {
    if (folder.id == 0 || image.rgba.empty()
        || image.width != kWidth || image.height != kHeight)
        return false;
    if (image.rgba.size() != static_cast<std::size_t>(kWidth) * kHeight * 4u)
        return false;

    std::error_code ec;
    std::filesystem::create_directories(kCacheDir, ec);

    Header header{};
    fillHeader(header, folder, memberTitleIds(folder));

    const std::string path = cachePath(folder.id);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(image.rgba.data()),
                  static_cast<std::streamsize>(image.rgba.size()));
        if (!out)
            return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    return !ec;
}

bool load(const Folder& folder, BakedImage& out) {
    out = {};
    if (folder.id == 0)
        return false;
    const auto titles = memberTitleIds(folder);
    const std::string path = cachePath(folder.id);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    Header header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || !headerMatches(header, folder, titles))
        return false;
    out.rgba.resize(static_cast<std::size_t>(header.payloadSize));
    in.read(reinterpret_cast<char*>(out.rgba.data()),
            static_cast<std::streamsize>(out.rgba.size()));
    if (!in)
        return false;
    out.width = kWidth;
    out.height = kHeight;
    return true;
}

BakedImage bake(const Folder& folder) {
    BakedImage out;
    out.width = kWidth;
    out.height = kHeight;
    out.rgba.assign(static_cast<std::size_t>(kWidth) * kHeight * 4u, 0);

    const auto titles = memberTitleIds(folder);
    const nxui::Color accent = colorForIndex(folder.colorIndex);

    // Match drawPeekGrid margins/gaps on a fixed 256×384 content canvas.
    constexpr int kCols = 2;
    constexpr int kRows = 3;
    const float margin = std::max(6.f, std::min(static_cast<float>(kWidth),
                                                static_cast<float>(kHeight)) * 0.08f);
    const float gap = std::max(3.f, margin * 0.45f);
    const float innerW = std::max(8.f, static_cast<float>(kWidth) - margin * 2.f);
    const float innerH = std::max(8.f, static_cast<float>(kHeight) - margin * 2.f);
    const float cellF = std::min((innerW - gap * (kCols - 1)) / static_cast<float>(kCols),
                                 (innerH - gap * (kRows - 1)) / static_cast<float>(kRows));
    const int cell = std::max(8, static_cast<int>(cellF + 0.5f));
    const int gapI = std::max(2, static_cast<int>(gap + 0.5f));
    const int gridW = cell * kCols + gapI * (kCols - 1);
    const int gridH = cell * kRows + gapI * (kRows - 1);
    const int originX = (kWidth - gridW) / 2;
    const int originY = (kHeight - gridH) / 2;

    const auto accentR = static_cast<std::uint8_t>(std::clamp(accent.r * 255.f, 0.f, 255.f));
    const auto accentG = static_cast<std::uint8_t>(std::clamp(accent.g * 255.f, 0.f, 255.f));
    const auto accentB = static_cast<std::uint8_t>(std::clamp(accent.b * 255.f, 0.f, 255.f));

    int peekCount = 0;
    for (int i = 0; i < kFolderPeekMax; ++i) {
        if (titles[static_cast<std::size_t>(i)] != 0)
            peekCount = i + 1;
    }

    // Match drawPeekGrid: rad = max(4, cell*0.16), soft drop shadow, white rim.
    const float rad = std::max(4.f, static_cast<float>(cell) * 0.16f);
    const int shadowX = 1;
    const int shadowY = 2;

    for (int i = 0; i < kCols * kRows; ++i) {
        const int col = i % kCols;
        const int row = i / kCols;
        const int dx = originX + col * (cell + gapI);
        const int dy = originY + row * (cell + gapI);

        fillRounded(out.rgba.data(), kWidth, kHeight,
                    dx + shadowX, dy + shadowY, cell, cell, rad,
                    5, 10, 15, 90);

        if (i < peekCount) {
            const std::uint64_t titleId = titles[static_cast<std::size_t>(i)];
            const auto data = AppListLoader::loadIconData(titleId, false);
            int w = 0, h = 0, ch = 0;
            std::uint8_t* raw = nullptr;
            if (!data.empty()) {
                raw = stbi_load_from_memory(data.data(), static_cast<int>(data.size()),
                                            &w, &h, &ch, 4);
            }
            if (raw && w > 0 && h > 0) {
                blitScaledRounded(raw, w, h, out.rgba.data(), kWidth, kHeight,
                                  dx, dy, cell, cell, rad);
                stbi_image_free(raw);
            } else {
                if (raw)
                    stbi_image_free(raw);
                fillRounded(out.rgba.data(), kWidth, kHeight, dx, dy, cell, cell, rad,
                            accentR, accentG, accentB, 128);
            }
        } else {
            fillRounded(out.rgba.data(), kWidth, kHeight, dx, dy, cell, cell, rad,
                        accentR, accentG, accentB, 46);
        }
        strokeRounded(out.rgba.data(), kWidth, kHeight, dx, dy, cell, cell, rad,
                      255, 255, 255, 56, 1.2f);
    }
    return out;
}

} // namespace switchu::folders::peek_composite
