#include "ThemeShopPreviewPipeline.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace themeshop::preview {
namespace {

std::string communityDiskLeaf(const std::string& url) {
    // Stable, filesystem-safe fingerprint of the URL.
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : url) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx.png", static_cast<unsigned long long>(h));
    return buf;
}

} // namespace

std::string communityDiskPath(const std::string& url) {
    return std::string(kCommunityDiskDir) + "/" + communityDiskLeaf(url);
}

bool loadCommunityDiskCache(const std::string& url, std::vector<std::uint8_t>& outBytes) {
    outBytes.clear();
    if (url.empty())
        return false;
    const std::string path = communityDiskPath(url);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff n = in.tellg();
    if (n <= 0 || n > 12 * 1024 * 1024)
        return false;
    in.seekg(0, std::ios::beg);
    outBytes.resize(static_cast<std::size_t>(n));
    if (!in.read(reinterpret_cast<char*>(outBytes.data()), n)) {
        outBytes.clear();
        return false;
    }
    return true;
}

void saveCommunityDiskCache(const std::string& url, const std::vector<std::uint8_t>& bytes) {
    if (url.empty() || bytes.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(kCommunityDiskDir, ec);
    const std::string path = communityDiskPath(url);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out)
            return;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
}

} // namespace themeshop::preview
