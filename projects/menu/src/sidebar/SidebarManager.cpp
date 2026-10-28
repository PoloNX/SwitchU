#include "SidebarManager.hpp"
#include "core/DebugLog.hpp"
#include "icons/IconPackManager.hpp"
#include "widgets/AdaptiveIconLoad.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Animation.hpp>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <array>

namespace {

constexpr int kSidebarIconCount = 6;
constexpr float kSidebarButtonSize = 70.f;
constexpr float kSidebarColumnGap = 16.f;
constexpr float kSidebarMarginX = 14.f;
constexpr float kDynamicStripButtonSize = 56.f;
constexpr float kDynamicStripGap = 10.f;
constexpr float kDynamicStripY = 17.f;
constexpr float kDynamicLeftGroupX = 190.f;
constexpr float kDynamicRightGroupX = 902.f;
// Mirrors IconGrid::kLayoutMorphDuration so chrome and tiles land together.
constexpr float kSidebarGlideDuration = 0.40f;
// Tiny cascade across the six buttons: reads as one gesture, not six snaps.
constexpr float kSidebarGlideStagger = 0.022f;

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
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
    const float btnSize = kSidebarButtonSize;
    const float gap = kSidebarColumnGap;
    const float leftX = kSidebarMarginX;
    const float rightX = 1280.f - kSidebarMarginX - btnSize;
    const float totalH = 3 * btnSize + 2.f * gap;
    float startY = 360.f - totalH * 0.5f;

    m_leftButtons.clear();
    m_rightButtons.clear();
    m_settingsButton = nullptr;
    m_themeShopButton = nullptr;
    m_albumButton    = nullptr;
    m_buttonGlides.clear();
    m_layoutArranged = false;
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

void SidebarManager::glideButtonTo(AppletButton* button, const nxui::Rect& target,
                                   float delay) {
    if (!button)
        return;
    const nxui::Rect current = button->rect();
    const auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    if (near(current.x, target.x) && near(current.y, target.y) &&
        near(current.width, target.width) && near(current.height, target.height)) {
        button->setRect(target);
        for (auto& glide : m_buttonGlides)
            if (glide.button == button)
                glide.active = false;
        return;
    }

    ButtonGlide* slot = nullptr;
    for (auto& glide : m_buttonGlides)
        if (glide.button == button)
            slot = &glide;
    if (!slot) {
        m_buttonGlides.push_back(ButtonGlide{});
        slot = &m_buttonGlides.back();
    }
    // Retarget from the in-flight rect so a flip interrupted mid-glide keeps
    // travelling instead of jumping back to the previous layout.
    slot->button  = button;
    slot->from    = current;
    slot->to      = target;
    slot->elapsed = 0.f;
    slot->delay   = delay;
    slot->active  = true;
}

void SidebarManager::updateButtonGlides(float dt) {
    for (auto& glide : m_buttonGlides) {
        if (!glide.active || !glide.button)
            continue;
        glide.elapsed += dt;
        const float t = (glide.elapsed - glide.delay) / kSidebarGlideDuration;
        if (t <= 0.f)
            continue;
        if (t >= 1.f) {
            glide.button->setRect(glide.to);
            glide.active = false;
            continue;
        }
        glide.button->setRect(nxui::Rect::lerp(glide.from, glide.to,
                                               nxui::Easing::inOutCubic(t)));
    }
}

void SidebarManager::setDynamicLineLayout(bool enabled) {
    std::vector<AppletButton*> ordered;
    ordered.reserve(m_leftButtons.size() + m_rightButtons.size());
    for (auto& button : m_leftButtons) ordered.push_back(button.get());
    for (auto& button : m_rightButtons) ordered.push_back(button.get());
    if (ordered.empty()) return;

    for (auto* button : ordered) {
        button->setCustomNavigation(nxui::FocusDirection::LEFT, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::RIGHT, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::UP, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
    }

    // Boot snaps to the configured layout; later flips glide into place.
    const bool animate = m_layoutArranged;
    m_layoutArranged = true;
    const auto place = [&](AppletButton* button, const nxui::Rect& target, int order) {
        if (!animate) {
            button->setRect(target);
            return;
        }
        glideButtonTo(button, target,
                      static_cast<float>(order) * kSidebarGlideStagger);
    };

    if (enabled) {
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            auto* button = ordered[i];
            const bool rightGroup = i >= m_leftButtons.size();
            const std::size_t groupIndex = rightGroup ? i - m_leftButtons.size() : i;
            const float groupX = rightGroup ? kDynamicRightGroupX : kDynamicLeftGroupX;
            place(button,
                  {groupX + static_cast<float>(groupIndex) *
                                (kDynamicStripButtonSize + kDynamicStripGap),
                   kDynamicStripY,
                   kDynamicStripButtonSize,
                   kDynamicStripButtonSize},
                  static_cast<int>(groupIndex));
            button->setCustomNavigation(nxui::FocusDirection::LEFT,
                                        i > 0 ? ordered[i - 1] : button);
            button->setCustomNavigation(nxui::FocusDirection::RIGHT,
                                        i + 1 < ordered.size() ? ordered[i + 1] : button);
        }
        return;
    }

    const float totalH = 3.f * kSidebarButtonSize + 2.f * kSidebarColumnGap;
    const float startY = 360.f - totalH * 0.5f;
    const float rightX = 1280.f - kSidebarMarginX - kSidebarButtonSize;
    for (std::size_t i = 0; i < m_leftButtons.size(); ++i) {
        place(m_leftButtons[i].get(),
              {kSidebarMarginX,
               startY + static_cast<float>(i) * (kSidebarButtonSize + kSidebarColumnGap),
               kSidebarButtonSize, kSidebarButtonSize},
              static_cast<int>(i));
    }
    for (std::size_t i = 0; i < m_rightButtons.size(); ++i) {
        place(m_rightButtons[i].get(),
              {rightX,
               startY + static_cast<float>(i) * (kSidebarButtonSize + kSidebarColumnGap),
               kSidebarButtonSize, kSidebarButtonSize},
              static_cast<int>(i));
    }
    if (!m_leftButtons.empty()) {
        m_leftButtons.front()->setCustomNavigation(nxui::FocusDirection::UP,
                                                   m_leftButtons.front().get());
        m_leftButtons.back()->setCustomNavigation(nxui::FocusDirection::DOWN,
                                                  m_leftButtons.back().get());
    }
    if (!m_rightButtons.empty()) {
        m_rightButtons.front()->setCustomNavigation(nxui::FocusDirection::UP,
                                                    m_rightButtons.front().get());
        m_rightButtons.back()->setCustomNavigation(nxui::FocusDirection::DOWN,
                                                   m_rightButtons.back().get());
    }
}

void SidebarManager::setDynamicLineDownAction(std::function<void()> action) {
    auto configure = [&action](std::shared_ptr<AppletButton>& button) {
        button->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
        button->removeAction(static_cast<uint64_t>(nxui::Button::DDown));
        button->removeAction(static_cast<uint64_t>(nxui::Button::LStickD));
        button->removeAction(static_cast<uint64_t>(nxui::Button::RStickD));
        if (action)
            button->addDirectionAction(nxui::FocusDirection::DOWN, action);
    };
    for (auto& button : m_leftButtons) configure(button);
    for (auto& button : m_rightButtons) configure(button);
}

void SidebarManager::setDynamicLineUpTarget(nxui::Widget* target) {
    for (auto& button : m_leftButtons)
        button->setCustomNavigation(nxui::FocusDirection::UP, target);
    for (auto& button : m_rightButtons)
        button->setCustomNavigation(nxui::FocusDirection::UP, target);
}

void SidebarManager::setDynamicLineProfileTarget(nxui::Widget* profile) {
    if (m_leftButtons.empty())
        return;
    // The HOME profile tile sits left of the dynamic-line applet strip.
    m_leftButtons.front()->setCustomNavigation(nxui::FocusDirection::LEFT, profile);
}

void SidebarManager::reloadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const std::string& assetsBase,
                                  const std::string& customIconsBase,
                                  const std::string& iconPackId) {
    if (m_leftButtons.empty() || m_rightButtons.empty())
        return;

    const std::string packId = iconPackId.empty()
        ? switchu::icons::kDefaultPackId : iconPackId;

    if (m_assetsLoaded
        && m_loadedAssetsBase == assetsBase
        && m_loadedCustomIconsBase == customIconsBase
        && m_loadedIconPackId == packId) {
        DebugLog::log("[sidebar-anim] reload skipped: assets unchanged (custom=%s pack=%s)",
                      customIconsBase.empty() ? "<empty>" : customIconsBase.c_str(),
                      packId.c_str());
        return;
    }

    loadAssets(gpu, ren, assetsBase, customIconsBase, packId);
}

void SidebarManager::invalidateAssetsCache() {
    m_loadedAssetsBase.clear();
    m_loadedCustomIconsBase.clear();
    m_loadedIconPackId.clear();
    m_assetsLoaded = false;
}

void SidebarManager::loadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::string& assetsBase,
                                const std::string& customIconsBase,
                                const std::string& iconPackId) {
    gpu.waitIdle();

    // Pack glyphs live under icons/<pack>/ with per-file fallback to icons/default.
    // Theme customIconsBase still wins when present.
    const std::string packId = iconPackId.empty()
        ? switchu::icons::kDefaultPackId : iconPackId;
    const bool useCustomStaticIcons = !customIconsBase.empty();
    auto packAssetPath = [&](const char* fileName) {
        return switchu::icons::resolveGlyphPath(assetsBase, packId, fileName);
    };
    auto resolveAsset = [&](const char* fileName) {
        if (!customIconsBase.empty()) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath))
                return customPath;
        }
        return packAssetPath(fileName);
    };
    // Parallel to iconFiles / button order: true when the loaded glyph is mono.
    std::array<bool, kSidebarIconCount> iconAdaptive{};
    auto loadIconTexture = [&](int iconIdx, const char* fileName) {
        auto tryPath = [&](const std::string& path) -> bool {
            bool adaptive = false;
            float meanLuma = 0.5f;
            if (!switchu::icons::loadMaybeAdaptiveIcon(
                    m_icons[iconIdx], gpu, ren, path, &adaptive, 128,
                    &meanLuma, &m_iconContrastMasks[iconIdx],
                    m_hslAdjust.enabled ? &m_hslAdjust : nullptr))
                return false;
            iconAdaptive[static_cast<std::size_t>(iconIdx)] = adaptive;
            m_iconMeanLumas[static_cast<std::size_t>(iconIdx)] = meanLuma;
            return true;
        };

        if (useCustomStaticIcons) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath)) {
                if (tryPath(customPath))
                    return;
                DebugLog::log("[sidebar-assets] custom icon load failed, falling back: %s",
                              customPath.c_str());
            }
        }

        const std::string fallbackPath = packAssetPath(fileName);
        if (!fallbackPath.empty() && tryPath(fallbackPath))
            return;
        iconAdaptive[static_cast<std::size_t>(iconIdx)] = false;
        m_iconMeanLumas[static_cast<std::size_t>(iconIdx)] = 0.5f;
        DebugLog::log("[sidebar-assets] pack icon load failed: pack=%s file=%s path=%s",
                      packId.c_str(),
                      fileName,
                      fallbackPath.empty() ? "<empty>" : fallbackPath.c_str());
    };

    static const char* iconFiles[] = {
        "album.png", "mii_editor.png", "controller.png", "power.png", "themes.png", "settings.png",
    };
    if ((int)m_icons.size() != kSidebarIconCount)
        m_icons.resize(kSidebarIconCount);
    m_iconContrastMasks.clear();
    m_iconContrastMasks.resize(kSidebarIconCount);
    m_iconMeanLumas.assign(kSidebarIconCount, 0.5f);
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
        const std::size_t idx = static_cast<std::size_t>(def.iconIdx);
        btn->setIconAdaptive(iconAdaptive[idx]);
        btn->setIconMeanLuma(m_iconMeanLumas[idx]);
        btn->setIconContrastMask(
            (!iconAdaptive[idx] && m_iconContrastMasks[idx].valid())
                ? &m_iconContrastMasks[idx] : nullptr);
        btn->setColourContrastAdapt(m_colourContrastAdapt);
        btn->setIconCustomTintEnabled(m_hslAdjust.enabled);
        btn->setIconUsesColorArtwork(false);
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
    m_loadedIconPackId = packId;
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
    updateButtonGlides(dt);
    for (auto& e : m_anims) {
        if (!e.button) continue;
        bool focused = (focusedWidget == e.button);
        e.anim.update(dt, focused);
        if (focused && e.anim.hasFrames()) {
            e.button->setIcon(e.anim.currentFrame());
            // Animated WebP frames keep their own colour — no chrome tint.
            e.button->setIconUsesColorArtwork(true);
        } else {
            // staticTex == nullptr: use frame 0 of the animation as idle
            nxui::Texture* idle = e.staticTex ? e.staticTex
                                              : (e.anim.hasFrames() ? e.anim.currentFrame() : nullptr);
            e.button->setIcon(idle);
            e.button->setIconUsesColorArtwork(e.staticTex == nullptr && e.anim.hasFrames());
        }
    }
}


void SidebarManager::applyTheme(const nxui::Theme& theme) {
    // Mono glyphs follow textPrimary (white on dark chrome / black on light,
    // including hero-aware flips). Colour replacements keep identity tint
    // unless colour-contrast adapt forces a black/white silhouette.
    const nxui::Color iconTint = theme.textPrimary.withAlpha(0.92f);
    auto apply = [&](std::shared_ptr<AppletButton>& btn) {
        btn->setBaseColor(theme.iconDefault);
        btn->setBorderColor(theme.panelBorder);
        btn->setHighlightColor(theme.panelHighlight);
        btn->setIconChromeTint(iconTint);
        btn->setColourContrastAdapt(m_colourContrastAdapt);
        btn->setIconCustomTintEnabled(m_hslAdjust.enabled);
        btn->setLiquidGlassEnabled(true);
        btn->setForceLiquidGlass(true);
        btn->setBlurEnabled(false);
        btn->setBorderWidth(2.2f);
    };
    for (auto& btn : m_leftButtons)  apply(btn);
    for (auto& btn : m_rightButtons) apply(btn);
}

void SidebarManager::setColourIconContrast(bool enabled) {
    m_colourContrastAdapt = enabled;
    auto apply = [&](std::shared_ptr<AppletButton>& btn) {
        if (!btn) return;
        btn->setColourContrastAdapt(enabled);
        btn->setIconCustomTintEnabled(m_hslAdjust.enabled);
    };
    for (auto& btn : m_leftButtons)  apply(btn);
    for (auto& btn : m_rightButtons) apply(btn);
}

void SidebarManager::setIconHslAdjust(const switchu::icons::HslAdjust& adj) {
    m_hslAdjust = adj;
}
