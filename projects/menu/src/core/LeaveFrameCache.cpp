#include "LeaveFrameCache.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace LeaveFrameCache {
namespace {

constexpr std::uint32_t kMagic = 0x4C555753u; // 'SWUL' LE
constexpr std::uint32_t kVersion = 3; // + layoutFingerprint (rearrange / sort)
constexpr int kMaxSide = 1280;
constexpr std::size_t kMaxPixels = static_cast<std::size_t>(kMaxSide) * static_cast<std::size_t>(kMaxSide);

#pragma pack(push, 1)
struct Header {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t page = 0;
    std::uint32_t openFolderId = 0;
    std::uint64_t focusTitleId = 0;
    std::uint32_t appLayoutMode = 0;
    std::uint32_t homeOrientation = 0;
    std::uint64_t layoutFingerprint = 0;
};
#pragma pack(pop)

static_assert(sizeof(Header) == 48, "LeaveFrameCache header size");

bool readHeader(std::ifstream& in, Header& header) {
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || in.gcount() != static_cast<std::streamsize>(sizeof(header)))
        return false;
    if (header.magic != kMagic || header.version != kVersion)
        return false;
    if (header.width == 0 || header.height == 0)
        return false;
    if (header.width > static_cast<std::uint32_t>(kMaxSide)
        || header.height > static_cast<std::uint32_t>(kMaxSide))
        return false;
    const std::size_t pixels = static_cast<std::size_t>(header.width)
                             * static_cast<std::size_t>(header.height);
    if (pixels > kMaxPixels)
        return false;
    return true;
}

void boxBlurPass(const std::uint8_t* src, std::uint8_t* dst,
                 int width, int height, int radius, bool horizontal) {
    const int r = std::max(1, radius);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int sum[4] = {0, 0, 0, 0};
            int count = 0;
            if (horizontal) {
                for (int k = -r; k <= r; ++k) {
                    const int sx = std::clamp(x + k, 0, width - 1);
                    const std::uint8_t* p = src + (static_cast<std::size_t>(y) * width + sx) * 4u;
                    sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3];
                    ++count;
                }
            } else {
                for (int k = -r; k <= r; ++k) {
                    const int sy = std::clamp(y + k, 0, height - 1);
                    const std::uint8_t* p = src + (static_cast<std::size_t>(sy) * width + x) * 4u;
                    sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3];
                    ++count;
                }
            }
            std::uint8_t* out = dst + (static_cast<std::size_t>(y) * width + x) * 4u;
            out[0] = static_cast<std::uint8_t>(sum[0] / count);
            out[1] = static_cast<std::uint8_t>(sum[1] / count);
            out[2] = static_cast<std::uint8_t>(sum[2] / count);
            out[3] = static_cast<std::uint8_t>(sum[3] / count);
        }
    }
}

} // namespace

void frostRgbaInPlace(std::uint8_t* rgba, int width, int height, int radius) {
    if (!rgba || width <= 0 || height <= 0)
        return;
    const int r = std::max(1, radius);
    const std::size_t bytes = static_cast<std::size_t>(width)
                            * static_cast<std::size_t>(height) * 4u;
    std::vector<std::uint8_t> tmp(bytes);
    // Two separable passes ≈ gentle frost without smear.
    boxBlurPass(rgba, tmp.data(), width, height, r, true);
    boxBlurPass(tmp.data(), rgba, width, height, r, false);
    boxBlurPass(rgba, tmp.data(), width, height, r, true);
    boxBlurPass(tmp.data(), rgba, width, height, r, false);

    // Chromatic fringe scales with radius (≈ screen-space at half or full res).
    const int chromaPx = std::max(1, r);
    std::memcpy(tmp.data(), rgba, bytes);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4u;
            const int xl = std::max(0, x - chromaPx);
            const int xr = std::min(width - 1, x + chromaPx);
            const std::size_t il = (static_cast<std::size_t>(y) * width + xl) * 4u;
            const std::size_t ir = (static_cast<std::size_t>(y) * width + xr) * 4u;
            rgba[i]     = tmp[il];
            rgba[i + 1] = tmp[i + 1];
            rgba[i + 2] = tmp[ir + 2];
        }
    }
}

bool saveToPath(const char* path, const LeaveFrameSession& session,
                const std::uint8_t* rgba, int width, int height, bool frost) {
    if (!path || !rgba || width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide)
        return false;

    std::error_code ec;
    std::filesystem::create_directories("sdmc:/config/SwitchU", ec);

    Header header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.page = session.page < 0 ? 0u : static_cast<std::uint32_t>(session.page);
    header.openFolderId = session.openFolderId;
    header.focusTitleId = session.focusTitleId;
    header.appLayoutMode = session.appLayoutMode;
    header.homeOrientation = session.homeOrientation;
    header.layoutFingerprint = session.layoutFingerprint;

    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    std::vector<std::uint8_t> pixels(rgba, rgba + bytes);
    if (frost)
        frostRgbaInPlace(pixels.data(), width, height);
    const std::string tmpPath = std::string(path) + ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(pixels.data()),
                  static_cast<std::streamsize>(bytes));
        if (!out)
            return false;
    }

    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    return true;
}

bool save(const LeaveFrameSession& session,
          const std::uint8_t* rgba, int width, int height) {
    return saveToPath(kPath, session, rgba, width, height);
}

bool loadFromPath(const char* path, LeaveFrameSession& session, LeaveFrameImage& image) {
    session = {};
    image = {};
    if (!path)
        return false;

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    Header header{};
    if (!readHeader(in, header))
        return false;

    const std::size_t bytes = static_cast<std::size_t>(header.width)
                            * static_cast<std::size_t>(header.height) * 4u;
    image.rgba.resize(bytes);
    in.read(reinterpret_cast<char*>(image.rgba.data()), static_cast<std::streamsize>(bytes));
    if (!in || in.gcount() != static_cast<std::streamsize>(bytes)) {
        image = {};
        return false;
    }

    image.width = static_cast<int>(header.width);
    image.height = static_cast<int>(header.height);
    session.valid = true;
    session.page = static_cast<int>(header.page);
    session.openFolderId = header.openFolderId;
    session.focusTitleId = header.focusTitleId;
    session.appLayoutMode = header.appLayoutMode;
    session.homeOrientation = header.homeOrientation;
    session.layoutFingerprint = header.layoutFingerprint;
    return image.valid();
}

bool load(LeaveFrameSession& session, LeaveFrameImage& image) {
    return loadFromPath(kPath, session, image);
}

bool loadSessionOnly(LeaveFrameSession& session) {
    session = {};
    std::ifstream in(kPath, std::ios::binary);
    if (!in)
        return false;

    Header header{};
    if (!readHeader(in, header))
        return false;

    session.valid = true;
    session.page = static_cast<int>(header.page);
    session.openFolderId = header.openFolderId;
    session.focusTitleId = header.focusTitleId;
    session.appLayoutMode = header.appLayoutMode;
    session.homeOrientation = header.homeOrientation;
    session.layoutFingerprint = header.layoutFingerprint;
    return true;
}

} // namespace LeaveFrameCache
