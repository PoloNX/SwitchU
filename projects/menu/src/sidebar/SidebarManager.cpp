#include "SidebarManager.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <sys/stat.h>

namespace {

constexpr int kSidebarIconCount = 6;

bool pathExists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty())
        return name;
    if (base.back() == '/')
        return base + name;
    return base + "/" + name;
}

} // namespace


void SidebarManager::build(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                           const std::string& assetsBase,
                           const Actions& actions) {
    constexpr float btnSize = 70.f;
    constexpr float gap     = 16.f;
    constexpr float marginX = 14.f;

    float leftX  = marginX;
    float rightX = 1280.f - marginX - btnSize;
    float totalH = 3 * btnSize + 2.f * gap;
    float startY = 360.f - totalH * 0.5f;

    m_leftButtons.clear();
    m_rightButtons.clear();
    m_settingsButton = nullptr;
    m_themeShopButton = nullptr;
    m_albumButton    = nullptr;
    m_anims.clear();
    m_icons.clear();
    m_icons.resize(kSidebarIconCount);
    invalidateAssetsCache();

    auto makeBtn = [](nxui::Texture* tex, const std::string& labelKey,
                      const std::string& fallback, std::function<void()> action) {
        auto btn = std::make_shared<AppletButton>();
        btn->setIcon(tex);
        btn->setLabelKey(labelKey, fallback);
        btn->setAccessibilityLabel(fallback);
        btn->setAccessibilityRole(nxui::I18n::instance().tr("accessibility.roles.button", "button"));
        btn->setAccessibilityHint(nxui::I18n::instance().tr("accessibility.hints.open", "A to open."));
        btn->setOnActivate(std::move(action));
        btn->setFocusable(true);
        return btn;
    };

    {
        auto album = makeBtn(&m_icons[0], "sidebar.album", "Album", actions.onAlbum);
        m_albumButton = album.get();
        album->setRect({leftX, startY + 0.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(album));

        auto miiEditor = makeBtn(&m_icons[1], "sidebar.mii_editor", "Mii Editor", actions.onMiiEditor);
        miiEditor->setRect({leftX, startY + 1.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(miiEditor));

        auto settings = makeBtn(&m_icons[5], "sidebar.settings", "Settings", actions.onSettings);
        m_settingsButton = settings.get();
        settings->setRect({leftX, startY + 2.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(settings));
    }

    {
        auto ctrl = makeBtn(&m_icons[2], "sidebar.controllers", "Controllers", actions.onControllers);
        ctrl->setRect({rightX, startY + 0.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(ctrl));

        auto sleep = makeBtn(&m_icons[3], "sidebar.sleep", "Sleep", actions.onSleep);
        sleep->setRect({rightX, startY + 1.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(sleep));

        auto themeShop = makeBtn(&m_icons[4], "sidebar.theme_shop", "Theme Shop", actions.onMiiverse);
        m_themeShopButton = themeShop.get();
        themeShop->setRect({rightX, startY + 2.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(themeShop));
    }

    if (!m_leftButtons.empty()) {
        m_leftButtons.front()->setCustomNavigation(nxui::FocusDirection::UP, m_leftButtons.front().get());
        m_leftButtons.back()->setCustomNavigation(nxui::FocusDirection::DOWN, m_leftButtons.back().get());
    }
    if (!m_rightButtons.empty()) {
        m_rightButtons.front()->setCustomNavigation(nxui::FocusDirection::UP, m_rightButtons.front().get());
        m_rightButtons.back()->setCustomNavigation(nxui::FocusDirection::DOWN, m_rightButtons.back().get());
    }

    (void)gpu;
    (void)ren;
    (void)assetsBase;
}

void SidebarManager::reloadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const std::string& assetsBase,
                                  const std::string& customIconsBase) {
    if (m_leftButtons.empty() || m_rightButtons.empty())
        return;

    if (m_assetsLoaded
        && m_loadedAssetsBase == assetsBase
        && m_loadedCustomIconsBase == customIconsBase) {
        DebugLog::log("[sidebar-anim] reload skipped: assets unchanged (custom=%s)",
                      customIconsBase.empty() ? "<empty>" : customIconsBase.c_str());
        return;
    }

    loadAssets(gpu, ren, assetsBase, customIconsBase);
}

void SidebarManager::invalidateAssetsCache() {
    m_loadedAssetsBase.clear();
    m_loadedCustomIconsBase.clear();
    m_assetsLoaded = false;
}

void SidebarManager::loadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::string& assetsBase,
                                const std::string& customIconsBase) {
    gpu.waitIdle();

    std::string defaultIconsBase = joinPath(assetsBase, "icons");
    const bool useCustomStaticIcons = !customIconsBase.empty();
    auto defaultAssetPath = [&](const char* fileName) {
        return joinPath(defaultIconsBase, fileName);
    };
    auto resolveAsset = [&](const char* fileName) {
        if (!customIconsBase.empty()) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath))
                return customPath;
        }
        return defaultAssetPath(fileName);
    };
    auto loadIconTexture = [&](int iconIdx, const char* fileName) {
        if (useCustomStaticIcons) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath)) {
                if (m_icons[iconIdx].loadFromFile(gpu, ren, customPath))
                    return;
                DebugLog::log("[sidebar-assets] custom icon load failed, falling back: %s",
                              customPath.c_str());
            }
        }

        const std::string fallbackPath = defaultAssetPath(fileName);
        if (!m_icons[iconIdx].loadFromFile(gpu, ren, fallbackPath)) {
            DebugLog::log("[sidebar-assets] fallback icon load failed: %s",
                          fallbackPath.c_str());
        }
    };

    static const char* iconFiles[] = {
        "album.png", "mii_editor.png", "controller.png", "power.png", "themes.png", "settings.png",
    };
    if ((int)m_icons.size() != kSidebarIconCount)
        m_icons.resize(kSidebarIconCount);
    for (int i = 0; i < kSidebarIconCount; ++i)
        loadIconTexture(i, iconFiles[i]);

    static const struct { int iconIdx; const char* webpFile; bool useFirstFrame; } animDefs[] = {
        { 0, "album.webp",      false },
        { 1, "mii_editor.webp", false },
        { 2, "controller.webp", true  },
        { 3, "power.webp",      false },
        { 4, "themes.webp",     false },
        { 5, "settings.webp",   false },
    };

    m_anims.clear();
    if (useCustomStaticIcons) {
        DebugLog::log("[sidebar-anim] custom theme icons use PNG only; skipping WebP animations (%s)",
                      customIconsBase.c_str());
    }

    for (const auto& def : animDefs) {
        AppletButton* btn = nullptr;
        if (def.iconIdx == 0) btn = m_leftButtons[0].get();
        else if (def.iconIdx == 1) btn = m_leftButtons[1].get();
        else if (def.iconIdx == 2) btn = m_rightButtons[0].get();
        else if (def.iconIdx == 3) btn = m_rightButtons[1].get();
        else if (def.iconIdx == 4) btn = m_rightButtons[2].get();
        else if (def.iconIdx == 5) btn = m_leftButtons[2].get();
        if (!btn)
            continue;

        nxui::Texture* staticTex = &m_icons[def.iconIdx];
        if (useCustomStaticIcons) {
            btn->setIcon(staticTex);
            continue;
        }

        nxui::Texture* idleTex = def.useFirstFrame ? nullptr : staticTex;
        tryLoadAnimation(gpu, ren, resolveAsset(def.webpFile), btn, idleTex);
        btn->setIcon(idleTex ? idleTex : staticTex);
    }

    m_loadedAssetsBase = assetsBase;
    m_loadedCustomIconsBase = customIconsBase;
    m_assetsLoaded = true;
}

void SidebarManager::tryLoadAnimation(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                      const std::string& webpPath,
                                      AppletButton* button,
                                      nxui::Texture* staticIcon) {
    AnimEntry entry;
    entry.button    = button;
    entry.staticTex = staticIcon;
    if (entry.anim.load(gpu, ren, webpPath)) {
        m_anims.push_back(std::move(entry));
    }
}

void SidebarManager::update(float dt, nxui::Widget* focusedWidget) {
    for (auto& e : m_anims) {
        if (!e.button) continue;
        bool focused = (focusedWidget == e.button);
        e.anim.update(dt, focused);
        if (focused && e.anim.hasFrames()) {
            e.button->setIcon(e.anim.currentFrame());
        } else {
            // staticTex == nullptr: use frame 0 of the animation as idle
            nxui::Texture* idle = e.staticTex ? e.staticTex
                                              : (e.anim.hasFrames() ? e.anim.currentFrame() : nullptr);
            e.button->setIcon(idle);
        }
    }
}


void SidebarManager::applyTheme(const nxui::Theme& theme) {
    auto apply = [&](std::shared_ptr<AppletButton>& btn) {
        btn->setBaseColor(theme.iconDefault);
        btn->setBorderColor(theme.panelBorder);
        btn->setHighlightColor(theme.panelHighlight);
        btn->setLiquidGlassEnabled(true);
        btn->setForceLiquidGlass(true);
        btn->setBlurEnabled(false);
        btn->setBorderWidth(2.2f);
    };
    for (auto& btn : m_leftButtons)  apply(btn);
    for (auto& btn : m_rightButtons) apply(btn);
}
