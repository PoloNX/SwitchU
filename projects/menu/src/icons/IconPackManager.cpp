#include "IconPackManager.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

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
    // At least album.png marks a pack; missing glyphs fall back to default.
    return fs::is_regular_file(dir / "album.png");
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
    const std::string packPath = join(packDirectory(assetsBase, packId), fileName);
    if (pathExists(packPath))
        return packPath;
    if (packId != kDefaultPackId) {
        const std::string fallback = join(packDirectory(assetsBase, kDefaultPackId), fileName);
        if (pathExists(fallback))
            return fallback;
    }
    // Legacy flat layout (pre-pack migration).
    const std::string legacy = join(iconsRoot(assetsBase), fileName);
    if (pathExists(legacy))
        return legacy;
    return {};
}

std::string displayNameForPack(const std::string& packId) {
    if (packId.empty() || packId == kDefaultPackId)
        return "Default";
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
        const fs::path preview = dir / "preview.png";
        if (fs::is_regular_file(preview, ec))
            info.previewPath = preview.generic_string();
        packs.push_back(std::move(info));
    };

    // Default first when present.
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

    // Flat legacy: synthesize a default entry from root glyphs.
    if (packs.empty() && pathExists(join(root.generic_string(), "album.png"))) {
        IconPackInfo info;
        info.id = kDefaultPackId;
        info.name = "Default";
        info.author = "Built-in";
        info.directory = root.generic_string();
        info.isDefault = true;
        packs.push_back(std::move(info));
    }
    return packs;
}

std::string ensurePackPreview(const std::string& packDir) {
    if (packDir.empty())
        return {};
    const fs::path outPath = fs::path(packDir) / "preview.png";
    std::error_code ec;
    if (fs::is_regular_file(outPath, ec))
        return outPath.generic_string();
    // Bundled packs ship a pre-baked preview.png (3×2 mosaic). User packs on
    // sdmc can drop one beside their glyphs; runtime bake can come later.
    return {};
}

} // namespace switchu::icons
