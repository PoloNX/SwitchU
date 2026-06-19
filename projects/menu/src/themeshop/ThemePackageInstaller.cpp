#include "ThemePackageInstaller.hpp"

#include "ThemeHttp.hpp"

#include "core/DebugLog.hpp"

#include <nxui/core/I18n.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>
#include <nlohmann/json.hpp>
#include <switch.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct GitHubRepoSource {
    std::string owner;
    std::string repo;
    std::string ref;
    std::string rawRoot;
};

struct RemoteFile {
    std::string relativePath;
    std::string downloadUrl;
};

std::string trimSlashes(std::string path) {
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    return path;
}

std::string parentDirectory(const std::string& path) {
    std::string clean = trimSlashes(path);
    std::size_t slash = clean.find_last_of('/');
    if (slash == std::string::npos)
        return {};
    return clean.substr(0, slash);
}

std::string basename(const std::string& path) {
    std::string clean = trimSlashes(path);
    std::size_t slash = clean.find_last_of('/');
    if (slash == std::string::npos)
        return clean;
    return clean.substr(slash + 1);
}

std::string dirnameForPreview(const std::string& path) {
    std::string name = basename(path);
    if (name.empty())
        return {};
    return "preview/" + name;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string lowerString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return (char)std::tolower(ch);
    });
    return value;
}

bool hasKnownAssetExtension(const std::string& path) {
    std::string name = lowerString(basename(path));
    std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return false;

    const std::string ext = name.substr(dot + 1);
    static constexpr const char* kAssetExtensions[] = {
        "png", "jpg", "jpeg", "webp", "bmp", "gif",
        "wav", "mp3", "ogg", "flac",
        "ttf", "otf",
        "json"
    };
    return std::any_of(std::begin(kAssetExtensions), std::end(kAssetExtensions), [&](const char* assetExt) {
        return ext == assetExt;
    });
}

bool hasParentTraversal(const std::string& path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part == "..")
            return true;
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return false;
}

std::string urlEncodeComponent(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());

    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back((char)ch);
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[(ch >> 4) & 0x0F]);
            encoded.push_back(kHex[ch & 0x0F]);
        }
    }

    return encoded;
}

std::string urlEncodePath(const std::string& path) {
    std::string encoded;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!encoded.empty())
            encoded.push_back('/');
        encoded += urlEncodeComponent(part);
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return encoded;
}

std::string rawGitHubFileUrl(const GitHubRepoSource& repo, const std::string& repoPath) {
    return repo.rawRoot + "/" + urlEncodePath(trimSlashes(repoPath));
}

void appendUniquePath(std::vector<std::string>& paths, std::string path) {
    path = trimSlashes(std::move(path));
    if (path.empty())
        return;
    if (startsWith(path, "https://") || startsWith(path, "http://"))
        return;
    if (std::find(paths.begin(), paths.end(), path) != paths.end())
        return;
    paths.push_back(std::move(path));
}

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool ensureDirectoryRecursive(const std::string& path) {
    if (path.empty())
        return false;

    std::string clean = path;
    while (!clean.empty() && clean.back() == '/')
        clean.pop_back();
    if (clean.empty())
        return false;

    std::size_t scheme = clean.find(":/");
    std::string current;
    std::size_t start = 0;
    if (scheme != std::string::npos) {
        current = clean.substr(0, scheme + 2);
        start = scheme + 2;
    }

    while (start < clean.size()) {
        std::size_t slash = clean.find('/', start);
        std::string part = clean.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty()) {
            if (!current.empty() && current.back() != '/')
                current.push_back('/');
            current += part;
            std::error_code ec;
            if (!std::filesystem::create_directory(current, ec) && ec)
                return false;
        }
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }

    return true;
}

bool removeDirectoryRecursive(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
}

std::string joinUrl(const std::string& baseUrl, const std::string& relativePath) {
    if (relativePath.empty())
        return baseUrl;
    if (startsWith(relativePath, "https://") || startsWith(relativePath, "http://"))
        return relativePath;

    std::size_t slash = baseUrl.find_last_of('/');
    if (slash == std::string::npos)
        return urlEncodePath(trimSlashes(relativePath));
    return baseUrl.substr(0, slash + 1) + urlEncodePath(trimSlashes(relativePath));
}

std::string joinPath(const std::string& base, const std::string& relative) {
    if (base.empty())
        return relative;
    if (relative.empty())
        return base;
    if (base.back() == '/')
        return base + relative;
    return base + "/" + relative;
}

bool parseRawGitHubUrl(const std::string& url, GitHubRepoSource& out) {
    static constexpr const char* kPrefix = "https://raw.githubusercontent.com/";
    if (!startsWith(url, kPrefix))
        return false;

    std::string rest = url.substr(sizeof(kPrefix) - 1);
    std::size_t slash1 = rest.find('/');
    if (slash1 == std::string::npos)
        return false;
    std::size_t slash2 = rest.find('/', slash1 + 1);
    if (slash2 == std::string::npos)
        return false;
    std::size_t slash3 = rest.find('/', slash2 + 1);
    if (slash3 == std::string::npos)
        return false;

    out.owner = rest.substr(0, slash1);
    out.repo = rest.substr(slash1 + 1, slash2 - slash1 - 1);
    out.ref = rest.substr(slash2 + 1, slash3 - slash2 - 1);
    out.rawRoot = std::string(kPrefix) + out.owner + "/" + out.repo + "/" + out.ref;
    return !(out.owner.empty() || out.repo.empty() || out.ref.empty());
}

std::vector<RemoteFile> listThemeFilesFromTree(const GitHubRepoSource& repo,
                                               const std::string& themePath) {
    std::vector<RemoteFile> files;

    const std::string apiUrl = "https://api.github.com/repos/" + repo.owner + "/" + repo.repo
        + "/git/trees/" + urlEncodeComponent(repo.ref) + "?recursive=1";
    const std::string body = themeshop::http::getText(apiUrl, {
        "Accept: application/vnd.github+json",
        "X-GitHub-Api-Version: 2022-11-28"
    });

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(body);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Invalid GitHub tree JSON: ") + ex.what());
    }

    auto treeIt = root.find("tree");
    if (treeIt == root.end() || !treeIt->is_array())
        throw std::runtime_error("GitHub tree response is missing entries");

    const std::string cleanRoot = trimSlashes(themePath);
    const std::string prefix = cleanRoot.empty() ? std::string() : cleanRoot + "/";
    for (const auto& item : *treeIt) {
        if (!item.is_object())
            continue;

        auto typeIt = item.find("type");
        auto pathIt = item.find("path");
        if (typeIt == item.end() || pathIt == item.end() || !typeIt->is_string() || !pathIt->is_string())
            continue;
        if (typeIt->get<std::string>() != "blob")
            continue;

        std::string repoPath = trimSlashes(pathIt->get<std::string>());
        if (!prefix.empty() && !startsWith(repoPath, prefix))
            continue;

        std::string relativePath = prefix.empty() ? repoPath : repoPath.substr(prefix.size());
        if (basename(relativePath) == ".gitkeep")
            continue;

        files.push_back({relativePath, rawGitHubFileUrl(repo, repoPath)});
    }

    return files;
}

void listThemeFilesFromContentsRecursive(const GitHubRepoSource& repo,
                                         const std::string& rootPath,
                                         const std::string& currentPath,
                                         std::vector<RemoteFile>& files) {
    const std::string apiUrl = "https://api.github.com/repos/" + repo.owner + "/" + repo.repo
        + "/contents/" + urlEncodePath(currentPath) + "?ref=" + urlEncodeComponent(repo.ref);
    const std::string body = themeshop::http::getText(apiUrl, {
        "Accept: application/vnd.github+json",
        "X-GitHub-Api-Version: 2022-11-28"
    });

    nlohmann::json entries;
    try {
        entries = nlohmann::json::parse(body);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("Invalid GitHub contents JSON: ") + ex.what());
    }
    if (!entries.is_array())
        throw std::runtime_error("GitHub contents response is not a directory listing");

    const std::string cleanRoot = trimSlashes(rootPath);
    const std::string prefix = cleanRoot.empty() ? std::string() : cleanRoot + "/";
    for (const auto& item : entries) {
        if (!item.is_object())
            continue;

        auto typeIt = item.find("type");
        auto pathIt = item.find("path");
        if (typeIt == item.end() || pathIt == item.end() || !typeIt->is_string() || !pathIt->is_string())
            continue;

        const std::string type = typeIt->get<std::string>();
        const std::string repoPath = trimSlashes(pathIt->get<std::string>());
        if (type == "dir") {
            listThemeFilesFromContentsRecursive(repo, rootPath, repoPath, files);
            continue;
        }
        if (type != "file")
            continue;
        if (!prefix.empty() && !startsWith(repoPath, prefix))
            continue;

        std::string relativePath = prefix.empty() ? repoPath : repoPath.substr(prefix.size());
        if (basename(relativePath) == ".gitkeep")
            continue;

        auto downloadIt = item.find("download_url");
        std::string downloadUrl = (downloadIt != item.end() && downloadIt->is_string())
            ? downloadIt->get<std::string>()
            : rawGitHubFileUrl(repo, repoPath);
        files.push_back({relativePath, downloadUrl});
    }
}

std::vector<RemoteFile> listThemeFilesFromContents(const GitHubRepoSource& repo,
                                                   const std::string& themePath) {
    std::vector<RemoteFile> files;

    listThemeFilesFromContentsRecursive(repo, trimSlashes(themePath), trimSlashes(themePath), files);
    return files;
}

void collectManifestAssets(const nlohmann::json& root, std::vector<std::string>& paths) {
    auto looksLikeRelativeAssetPath = [](const std::string& value, const std::string& key) {
        std::string path = trimSlashes(value);
        if (path.empty())
            return false;
        if (startsWith(path, "https://") || startsWith(path, "http://")
            || startsWith(path, "data:") || startsWith(path, "#"))
            return false;
        if (hasParentTraversal(path))
            return false;

        const bool hasAssetExtension = hasKnownAssetExtension(path);
        const bool hasDirectory = path.find('/') != std::string::npos;
        if (hasAssetExtension)
            return true;
        if (!hasDirectory)
            return false;

        static constexpr const char* kPathKeys[] = {
            "path", "file", "src", "image", "font", "regular", "normal", "ui",
            "small", "secondary", "caption", "cover", "screenshot", "background"
        };
        return std::any_of(std::begin(kPathKeys), std::end(kPathKeys), [&](const char* pathKey) {
            return key == pathKey;
        });
    };

    std::function<void(const nlohmann::json&, const std::string&)> collectRecursive =
        [&](const nlohmann::json& value, const std::string& key) {
            if (value.is_string()) {
                std::string path = value.get<std::string>();
                if (looksLikeRelativeAssetPath(path, key))
                    appendUniquePath(paths, std::move(path));
                return;
            }

            if (value.is_array()) {
                for (const auto& item : value)
                    collectRecursive(item, key);
                return;
            }

            if (!value.is_object())
                return;

            for (auto it = value.begin(); it != value.end(); ++it)
                collectRecursive(it.value(), it.key());
        };

    collectRecursive(root, {});

    auto addStringMember = [&](const nlohmann::json& object, const char* key) {
        auto it = object.find(key);
        if (it != object.end() && it->is_string())
            appendUniquePath(paths, it->get<std::string>());
    };

    addStringMember(root, "cover");
    addStringMember(root, "screenshot");
    auto screenshotsIt = root.find("screenshots");
    if (screenshotsIt != root.end() && screenshotsIt->is_array()) {
        for (const auto& screenshot : *screenshotsIt) {
            if (screenshot.is_string())
                appendUniquePath(paths, screenshot.get<std::string>());
        }
    }

    auto previewIt = root.find("preview");
    if (previewIt != root.end() && previewIt->is_object()) {
        addStringMember(*previewIt, "cover");
        addStringMember(*previewIt, "screenshot");
        auto previewScreenshotsIt = previewIt->find("screenshots");
        if (previewScreenshotsIt != previewIt->end() && previewScreenshotsIt->is_array()) {
            for (const auto& screenshot : *previewScreenshotsIt) {
                if (screenshot.is_string())
                    appendUniquePath(paths, screenshot.get<std::string>());
            }
        }
    }

    auto themeIt = root.find("theme");
    if (themeIt == root.end() || !themeIt->is_object())
        return;

    auto backgroundIt = themeIt->find("background");
    if (backgroundIt == themeIt->end() || !backgroundIt->is_object())
        return;

    auto imageIt = backgroundIt->find("image");
    if (imageIt == backgroundIt->end())
        return;
    if (imageIt->is_string()) {
        appendUniquePath(paths, imageIt->get<std::string>());
    } else if (imageIt->is_object()) {
        addStringMember(*imageIt, "path");
        addStringMember(*imageIt, "file");
        addStringMember(*imageIt, "src");
    }
}

std::vector<RemoteFile> listThemeFiles(const std::string& catalogUrl,
                                       const std::string& themePath,
                                       const std::string& manifestPath,
                                       const ThemeCatalogClient::Entry& entry) {
    std::vector<RemoteFile> files;

    GitHubRepoSource repo;
    if (parseRawGitHubUrl(catalogUrl, repo)) {
        try {
            files = listThemeFilesFromTree(repo, themePath);
        } catch (const std::exception& ex) {
            DebugLog::log("[themeshop] tree listing failed for %s: %s", themePath.c_str(), ex.what());
        }
        if (files.empty()) {
            try {
                files = listThemeFilesFromContents(repo, themePath);
            } catch (const std::exception& ex) {
                DebugLog::log("[themeshop] contents listing failed for %s: %s", themePath.c_str(), ex.what());
            }
        }
    }

    auto hasRelativePath = [&](const std::string& relativePath) {
        return std::any_of(files.begin(), files.end(), [&](const RemoteFile& file) {
            return file.relativePath == relativePath;
        });
    };
    auto addRelativeFile = [&](const std::string& relativePath, const std::string& downloadUrl) {
        std::string clean = trimSlashes(relativePath);
        if (clean.empty() || hasRelativePath(clean))
            return;
        files.push_back({clean, downloadUrl});
    };

    const bool usingFallbackListing = files.empty();
    std::string manifestText;
    std::string manifestRelativePath;
    std::string manifestUrl;
    if (!manifestPath.empty()) {
        manifestRelativePath = basename(manifestPath);
        if (manifestRelativePath.empty())
            manifestRelativePath = "theme.json";
        manifestUrl = joinUrl(catalogUrl, manifestPath);
        if (usingFallbackListing)
            addRelativeFile(manifestRelativePath, manifestUrl);
    }

    if (usingFallbackListing && !manifestUrl.empty()) {
        try {
            manifestText = themeshop::http::getText(manifestUrl);
            nlohmann::json manifest = nlohmann::json::parse(manifestText);
            std::vector<std::string> manifestAssets;
            collectManifestAssets(manifest, manifestAssets);
            for (const auto& assetPath : manifestAssets) {
                std::string remotePath = assetPath;
                if (!themePath.empty())
                    remotePath = joinPath(themePath, assetPath);
                addRelativeFile(assetPath, joinUrl(catalogUrl, remotePath));
            }
        } catch (const std::exception& ex) {
            DebugLog::log("[themeshop] manifest asset fallback failed for %s: %s",
                          manifestPath.c_str(),
                          ex.what());
        }
    }

    auto addPreviewFile = [&](const std::string& previewPath) {
        if (previewPath.empty())
            return;

        std::string clean = trimSlashes(previewPath);
        std::string relativePath;
        std::string downloadUrl;
        if (startsWith(clean, "https://") || startsWith(clean, "http://")) {
            relativePath = dirnameForPreview(clean);
            downloadUrl = clean;
        } else {
            std::string prefix = themePath.empty() ? std::string() : trimSlashes(themePath) + "/";
            std::string remotePath = clean;
            if (!themePath.empty() && !startsWith(clean, prefix))
                remotePath = joinPath(themePath, clean);
            relativePath = (!prefix.empty() && startsWith(clean, prefix))
                ? clean.substr(prefix.size())
                : clean;
            downloadUrl = joinUrl(catalogUrl, remotePath);
        }

        addRelativeFile(relativePath, downloadUrl);
    };

    addPreviewFile(entry.cover);
    for (const auto& screenshot : entry.screenshots)
        addPreviewFile(screenshot);

    std::sort(files.begin(), files.end(), [](const RemoteFile& lhs, const RemoteFile& rhs) {
        bool lhsManifest = lhs.relativePath == "theme.json";
        bool rhsManifest = rhs.relativePath == "theme.json";
        if (lhsManifest != rhsManifest)
            return lhsManifest;
        return lhs.relativePath < rhs.relativePath;
    });

    return files;
}

void writeFileBinary(const std::string& path, const std::string& data) {
    std::string parent = parentDirectory(path);
    if (!parent.empty() && !ensureDirectoryRecursive(parent))
        throw std::runtime_error("Failed to create directory: " + parent);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Failed to open file for writing: " + path);

    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output)
        throw std::runtime_error("Failed to write file: " + path);
}

} // namespace

std::string ThemePackageInstaller::destinationRootFor(const std::string& themeId, Mode mode) {
    (void)mode;
    return "sdmc:/config/SwitchU/themes/" + themeId;
}

ThemePackageInstaller::Result ThemePackageInstaller::run(const std::string& catalogUrl,
                                                         const ThemeCatalogClient::Entry& entry,
                                                         Mode mode,
                                                         ProgressCallback onProgress) {
    Result result;
    result.themeId = entry.id;
    result.installed = true;
    result.destinationPath = destinationRootFor(entry.id, mode);

    const std::string themePath = trimSlashes(!entry.path.empty() ? entry.path : parentDirectory(entry.manifest));
    const std::string manifestPath = trimSlashes(!entry.manifest.empty() ? entry.manifest : joinPath(themePath, "theme.json"));
    if (manifestPath.empty())
        throw std::runtime_error("Catalog entry is missing a manifest path");

    if (onProgress) {
        auto& i18n = nxui::I18n::instance();
        onProgress(mode == Mode::InstallAndApply
                       ? i18n.tr("themeshop.transfer.prepare_apply", "Preparing download + apply...")
                       : i18n.tr("themeshop.transfer.prepare_install", "Preparing download + install..."),
                   0.f);
    }

    if (pathExists(result.destinationPath))
        removeDirectoryRecursive(result.destinationPath);
    if (!ensureDirectoryRecursive(result.destinationPath))
        throw std::runtime_error("Failed to prepare destination: " + result.destinationPath);

    std::vector<RemoteFile> files = listThemeFiles(catalogUrl, themePath, manifestPath, entry);
    if (files.empty())
        throw std::runtime_error("Theme package contains no downloadable files");

    DebugLog::log("[themeshop] package file list: id=%s count=%zu root=%s",
                  entry.id.c_str(),
                  files.size(),
                  themePath.c_str());

    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        float progress = files.size() > 1 ? (float)i / (float)files.size() : 0.f;
        if (onProgress) {
            auto& i18n = nxui::I18n::instance();
            onProgress(i18n.tr("themeshop.transfer.downloading_file", "Downloading")
                           + " " + file.relativePath + " (" + std::to_string(i + 1)
                           + "/" + std::to_string(files.size()) + ")",
                       progress);
        }

        const std::string body = themeshop::http::getText(file.downloadUrl);
        writeFileBinary(joinPath(result.destinationPath, file.relativePath), body);
    }

    if (onProgress) {
        auto& i18n = nxui::I18n::instance();
        onProgress(mode == Mode::InstallAndApply
                       ? i18n.tr("themeshop.transfer.finalize_apply", "Finalizing install before apply...")
                       : i18n.tr("themeshop.transfer.finalize_install", "Finalizing installation..."),
                   1.f);
    }

    result.success = true;
    auto& i18n = nxui::I18n::instance();
    result.message = mode == Mode::InstallAndApply
        ? i18n.tr("themeshop.transfer.installed_applying", "Theme installed. Applying it now...")
        : i18n.tr("themeshop.transfer.installed", "Theme installed.");
    return result;
}
