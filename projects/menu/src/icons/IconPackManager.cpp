#include "IconPackManager.hpp"
#include "widgets/AdaptiveIconLoad.hpp"

#include <nxui/third_party/stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <nxui/third_party/stb/stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <vector>

namespace switchu::icons {
namespace {

namespace fs = std::filesystem;

bool pathExists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

std::string join(const std::string& base, const std::string& name) {
    if (base.empty())
        return name;
    if (base.back() == '/' || base.back() == '\\')
        return base + name;
    return base + "/" + name;
}

bool packLooksValid(const fs::path& dir) {
    // At least album.png marks a full SD pack; theme packs use iconDirLooksUsable.
    return fs::is_regular_file(dir / "album.png");
}

std::string stripThemeIdPrefix(std::string id) {
    const char* prefixes[] = { "package:", "builtin:", "user:" };
    for (const char* prefix : prefixes) {
        const std::size_t n = std::strlen(prefix);
        if (id.rfind(prefix, 0) == 0)
            return id.substr(n);
    }
    return id;
}

std::string readThemeJsonIconsBase(const fs::path& themeDir) {
    const fs::path manifest = themeDir / "theme.json";
    std::error_code ec;
    if (!fs::is_regular_file(manifest, ec))
        return {};
    std::ifstream in(manifest, std::ios::binary);
    if (!in)
        return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Tiny parse: look for "icons" then a path-like string. Prefer nlohmann if
    // already linked via other TUs — keep this file free of json deps by
    // falling back to assets/icons when unsure.
    auto findPathNear = [&](const char* key) -> std::string {
        const std::size_t keyPos = text.find(key);
        if (keyPos == std::string::npos)
            return {};
        const std::size_t colon = text.find(':', keyPos);
        if (colon == std::string::npos)
            return {};
        const std::size_t q1 = text.find('"', colon);
        if (q1 == std::string::npos)
            return {};
        const std::size_t q2 = text.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 <= q1 + 1)
            return {};
        return text.substr(q1 + 1, q2 - q1 - 1);
    };
    // Prefer nested theme.icons.basePath / path.
    std::string base = findPathNear("\"basePath\"");
    if (base.empty())
        base = findPathNear("\"base_path\"");
    if (base.empty())
        base = findPathNear("\"path\"");
    // Reject obviously non-path captures (colour arrays etc.)
    if (base.find("assets/") == std::string::npos && base.find("icons") == std::string::npos)
        return {};
    return base;
}

} // namespace

std::string iconsRoot(const std::string& assetsBase) {
    return join(assetsBase, "icons");
}

std::string packDirectory(const std::string& assetsBase, const std::string& packId) {
    const std::string id = packId.empty() ? kDefaultPackId : packId;
    return join(iconsRoot(assetsBase), id);
}

std::string resolveGlyphPath(const std::string& assetsBase,
                             const std::string& packId,
                             const std::string& fileName) {
    const std::string dir = resolvePackIconsDirectory(assetsBase, packId);
    if (!dir.empty()) {
        const std::string packPath = join(dir, fileName);
        if (pathExists(packPath))
            return packPath;
    }
    if (packId != kDefaultPackId && !isThemePackId(packId)) {
        const std::string fallback = join(packDirectory(assetsBase, kDefaultPackId), fileName);
        if (pathExists(fallback))
            return fallback;
    }
    if (isThemePackId(packId)) {
        const std::string fallback = join(packDirectory(assetsBase, kDefaultPackId), fileName);
        if (pathExists(fallback))
            return fallback;
    }
    const std::string legacy = join(iconsRoot(assetsBase), fileName);
    if (pathExists(legacy))
        return legacy;
    return {};
}

std::string displayNameForPack(const std::string& packId) {
    if (packId.empty() || packId == kDefaultPackId)
        return "Default";
    if (isThemePackId(packId)) {
        std::string name = themeFolderFromPackId(packId);
        if (!name.empty())
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        return name;
    }
    std::string name = packId;
    if (!name.empty())
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

std::vector<IconPackInfo> discoverPacks(const std::string& assetsBase) {
    std::vector<IconPackInfo> packs;
    const fs::path root = iconsRoot(assetsBase);
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return packs;

    auto addPack = [&](const std::string& id, const fs::path& dir) {
        if (!packLooksValid(dir))
            return;
        IconPackInfo info;
        info.id = id;
        info.name = displayNameForPack(id);
        info.author = (id == kDefaultPackId) ? "Built-in" : "Icon pack";
        info.directory = dir.generic_string();
        info.isDefault = (id == kDefaultPackId);
        info.fromTheme = false;
        info.colourPack = probeIconLooksColour((dir / "album.png").generic_string());
        packs.push_back(std::move(info));
    };

    const fs::path defaultDir = root / kDefaultPackId;
    if (fs::is_directory(defaultDir, ec))
        addPack(kDefaultPackId, defaultDir);

    std::vector<fs::path> others;
    for (fs::directory_iterator it(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_directory(ec))
            continue;
        const std::string id = it->path().filename().string();
        if (id == kDefaultPackId || id == "music")
            continue;
        others.push_back(it->path());
    }
    std::sort(others.begin(), others.end());
    for (const auto& dir : others)
        addPack(dir.filename().string(), dir);

    if (packs.empty() && pathExists(join(root.generic_string(), "album.png"))) {
        IconPackInfo info;
        info.id = kDefaultPackId;
        info.name = "Default";
        info.author = "Built-in";
        info.directory = root.generic_string();
        info.isDefault = true;
        info.colourPack = probeIconLooksColour(join(root.generic_string(), "album.png"));
        packs.push_back(std::move(info));
    }
    return packs;
}

std::vector<IconPackInfo> discoverThemePacks() {
    std::vector<IconPackInfo> packs;
    std::error_code ec;
    if (!fs::is_directory(kInstalledThemesDir, ec))
        return packs;

    std::vector<fs::path> themes;
    for (fs::directory_iterator it(kInstalledThemesDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->is_directory(ec))
            themes.push_back(it->path());
    }
    std::sort(themes.begin(), themes.end());

    for (const auto& themeDir : themes) {
        const std::string folder = themeDir.filename().string();
        if (folder.empty() || folder == "." || folder == "..")
            continue;

        std::string rel = readThemeJsonIconsBase(themeDir);
        if (rel.empty())
            rel = "assets/icons";

        const std::string iconsDir = join(themeDir.generic_string(), rel);
        if (!iconDirLooksUsable(iconsDir))
            continue;

        IconPackInfo info;
        info.id = makeThemePackId(folder);
        info.name = folder;
        // Prefer display name from theme.json "name" if present.
        {
            const fs::path manifest = themeDir / "theme.json";
            std::ifstream in(manifest, std::ios::binary);
            if (in) {
                std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                const std::size_t keyPos = text.find("\"name\"");
                if (keyPos != std::string::npos) {
                    const std::size_t q1 = text.find('"', text.find(':', keyPos));
                    const std::size_t q2 = (q1 == std::string::npos) ? std::string::npos : text.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
                        info.name = text.substr(q1 + 1, q2 - q1 - 1);
                }
                const std::size_t aPos = text.find("\"author\"");
                if (aPos != std::string::npos) {
                    const std::size_t q1 = text.find('"', text.find(':', aPos));
                    const std::size_t q2 = (q1 == std::string::npos) ? std::string::npos : text.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
                        info.author = text.substr(q1 + 1, q2 - q1 - 1);
                }
            }
        }
        if (info.author.empty())
            info.author = "Theme";
        info.directory = iconsDir;
        info.isDefault = false;
        info.fromTheme = true;
        const std::string probe = firstGlyphPathInDir(info.directory);
        info.colourPack = !probe.empty() && probeIconLooksColour(probe);
        packs.push_back(std::move(info));
    }
    return packs;
}


std::string resolvePackIconsDirectory(const std::string& assetsBase, const std::string& packId) {
    const std::string id = packId.empty() ? kDefaultPackId : packId;
    if (isThemePackId(id)) {
        const std::string folder = themeFolderFromPackId(id);
        if (folder.empty())
            return {};
        // Do not use std::filesystem operators on sdmc: roots — they can yield
        // paths that exist() rejects on Switch. String-join like the rest of SwitchU.
        const std::string conventional = join(join(join(kInstalledThemesDir, folder), "assets"), "icons");
        if (iconDirLooksUsable(conventional))
            return conventional;
        for (const auto& pack : discoverThemePacks()) {
            if (pack.id == makeThemePackId(folder) || themeFolderFromPackId(pack.id) == folder)
                return pack.directory;
        }
        return {};
    }
    const std::string dir = packDirectory(assetsBase, id);
    return pathExists(dir) ? dir : std::string{};
}

std::string ensurePackPreview(const std::string& packDir, const std::string& packId) {
    if (packDir.empty())
        return {};

    std::error_code ec;

    // Prefer authored preview.png (transparent glyphs; glass well shows through).
    const fs::path authored = fs::path(packDir) / "preview.png";
    if (fs::is_regular_file(authored, ec))
        return authored.generic_string();

    std::string leaf = packId.empty() ? fs::path(packDir).filename().string() : packId;
    for (char& c : leaf) {
        if (c == ':' || c == '/' || c == '\\' || c == ' ')
            c = '_';
    }
    if (leaf.empty())
        leaf = "pack";

    const fs::path cachePath = fs::path("sdmc:/config/SwitchU/icon_pack_previews") / (leaf + "-mosaic.png");
    if (fs::is_regular_file(cachePath, ec))
        return cachePath.generic_string();

    // Bake once onto a transparent plate (no opaque black fill).
    constexpr int kCols = 3;
    constexpr int kRows = 2;
    constexpr int kCell = 128;
    constexpr int kOutW = kCols * kCell;
    constexpr int kOutH = kRows * kCell;
    constexpr int kPad = 14;

    std::vector<std::uint8_t> canvas(static_cast<size_t>(kOutW * kOutH * 4), 0);

    const std::string sdDefaultAbs = "sdmc:/switch/SwitchU/icons/default";
    std::string sdSiblingDefault;
    {
        fs::path p(packDir);
        if (p.has_parent_path()) {
            const fs::path sibling = p.parent_path() / kDefaultPackId;
            if (fs::is_directory(sibling, ec))
                sdSiblingDefault = sibling.generic_string();
        }
    }

    auto pickGlyph = [&](const char* fileName) -> std::string {
        const fs::path local = fs::path(packDir) / fileName;
        if (fs::is_regular_file(local, ec))
            return local.generic_string();
        for (const std::string& base : { sdSiblingDefault, sdDefaultAbs }) {
            if (base.empty()) continue;
            const fs::path cand = fs::path(base) / fileName;
            if (fs::is_regular_file(cand, ec))
                return cand.generic_string();
        }
        return {};
    };

    std::vector<std::string> glyphPaths;
    for (int i = 0; i < kPackGlyphCount; ++i) {
        std::string p = pickGlyph(kPackGlyphFiles[i]);
        if (!p.empty())
            glyphPaths.push_back(std::move(p));
    }

    int placed = 0;
    for (const std::string& glyphPath : glyphPaths) {
        const int col = placed % kCols;
        const int row = placed / kCols;
        if (row >= kRows)
            break;

        int gw = 0, gh = 0, ch = 0;
        std::uint8_t* pixels = stbi_load(glyphPath.c_str(), &gw, &gh, &ch, 4);
        if (!pixels || gw <= 0 || gh <= 0) {
            if (pixels) stbi_image_free(pixels);
            continue;
        }

        const int cellX = col * kCell;
        const int cellY = row * kCell;
        const int inner = kCell - kPad * 2;
        const float scale = std::min(static_cast<float>(inner) / static_cast<float>(gw),
                                     static_cast<float>(inner) / static_cast<float>(gh));
        const int dw = std::max(1, static_cast<int>(gw * scale + 0.5f));
        const int dh = std::max(1, static_cast<int>(gh * scale + 0.5f));
        const int dx0 = cellX + (kCell - dw) / 2;
        const int dy0 = cellY + (kCell - dh) / 2;

        for (int dy = 0; dy < dh; ++dy) {
            const int sy = std::min(gh - 1, static_cast<int>((dy + 0.5f) * gh / dh));
            for (int dx = 0; dx < dw; ++dx) {
                const int sx = std::min(gw - 1, static_cast<int>((dx + 0.5f) * gw / dw));
                const std::uint8_t* src = pixels + (sy * gw + sx) * 4;
                const float a = src[3] / 255.f;
                if (a <= 0.001f)
                    continue;
                const int x = dx0 + dx;
                const int y = dy0 + dy;
                if (x < 0 || y < 0 || x >= kOutW || y >= kOutH)
                    continue;
                std::uint8_t* dst = canvas.data() + (y * kOutW + x) * 4;
                const float dstA = dst[3] / 255.f;
                const float outA = a + dstA * (1.f - a);
                if (outA <= 0.001f)
                    continue;
                dst[0] = static_cast<std::uint8_t>((src[0] * a + dst[0] * dstA * (1.f - a)) / outA + 0.5f);
                dst[1] = static_cast<std::uint8_t>((src[1] * a + dst[1] * dstA * (1.f - a)) / outA + 0.5f);
                dst[2] = static_cast<std::uint8_t>((src[2] * a + dst[2] * dstA * (1.f - a)) / outA + 0.5f);
                dst[3] = static_cast<std::uint8_t>(outA * 255.f + 0.5f);
            }
        }
        stbi_image_free(pixels);
        ++placed;
    }

    if (placed <= 0)
        return {};

    fs::create_directories(cachePath.parent_path(), ec);
    if (stbi_write_png(cachePath.string().c_str(), kOutW, kOutH, 4, canvas.data(), kOutW * 4) == 0)
        return {};
    return cachePath.generic_string();
}

bool isThemePackId(const std::string& packId) {
    return packId.rfind(kThemePackPrefix, 0) == 0
        && packId.size() > kThemePackPrefixLen;
}

std::string makeThemePackId(const std::string& themeFolderOrPresetId) {
    if (themeFolderOrPresetId.empty())
        return {};
    if (isThemePackId(themeFolderOrPresetId))
        return makeThemePackId(themeFolderFromPackId(themeFolderOrPresetId));
    return std::string(kThemePackPrefix) + stripThemeIdPrefix(themeFolderOrPresetId);
}

std::string themeFolderFromPackId(const std::string& packId) {
    if (!isThemePackId(packId))
        return {};
    return stripThemeIdPrefix(packId.substr(kThemePackPrefixLen));
}

bool iconDirLooksUsable(const std::string& dir) {
    if (dir.empty())
        return false;
    const fs::path root(dir);
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return false;
    for (int i = 0; i < kPackGlyphCount; ++i) {
        if (fs::is_regular_file(root / kPackGlyphFiles[i], ec))
            return true;
    }
    if (fs::is_regular_file(root / kChangeViewFile, ec))
        return true;
    if (fs::is_regular_file(root / "gamecard.png", ec))
        return true;
    return false;
}

std::string firstGlyphPathInDir(const std::string& dir) {
    if (dir.empty())
        return {};
    const fs::path root(dir);
    std::error_code ec;
    for (int i = 0; i < kPackGlyphCount; ++i) {
        const fs::path p = root / kPackGlyphFiles[i];
        if (fs::is_regular_file(p, ec))
            return p.generic_string();
    }
    for (const char* extra : { kChangeViewFile, "gamecard.png" }) {
        const fs::path p = root / extra;
        if (fs::is_regular_file(p, ec))
            return p.generic_string();
    }
    return {};
}

} // namespace switchu::icons
