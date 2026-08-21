#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Animation.hpp>
#include "core/GridModel.hpp"
#include <string>

namespace nxui { class Font; }



class GlossyIcon : public nxui::GlassWidget {
public:
    GlossyIcon();

    void setTitle(const std::string& t) { m_title = t; }
    const std::string& title() const    { return m_title; }

    void setTexture(nxui::Texture* tex) { m_tex = tex; }
    nxui::Texture* texture() const      { return m_tex; }

    void setGameCardTexture(nxui::Texture* tex) { m_gameCardTex = tex; }
    nxui::Texture* gameCardTexture() const      { return m_gameCardTex; }

    void setTitleId(uint64_t id)  { m_titleId = id; }
    uint64_t titleId() const      { return m_titleId; }

    void setSuspended(bool s)     { m_suspended = s; }
    bool isSuspended() const      { return m_suspended; }

    void setIsGameCard(bool gc)     { m_isGameCard = gc; }
    bool isGameCard() const         { return m_isGameCard; }

    void setNotLaunchable(bool nl)  { m_notLaunchable = nl; }
    bool isNotLaunchable() const    { return m_notLaunchable; }
    void setLoadingColor(const nxui::Color& color) { m_loadingColor = color; }
    void setEntryKind(GridEntryKind kind) { m_entryKind = kind; }
    GridEntryKind entryKind() const { return m_entryKind; }
    void setFolderPreviewCount(int count) { m_folderPreviewCount = count; }
    void setFolderVisualSeed(std::uint32_t seed) { m_folderVisualSeed = seed; }
    void setFolderColorIndex(int index) { m_folderColorIndex = index; }
    void setFont(nxui::Font* font) { m_font = font; }  // folder tiles carry their name

    void startAppear(float delay);
    void forceVisible();

    void setFocusable(bool f) { m_focusable = f; }
    bool isFocusable() const override { return m_focusable; }
    void onFocusGained() override;
    void onFocusLost() override;

protected:
    void onRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;

private:
    std::string m_title;
    nxui::Texture*    m_tex = nullptr;
    nxui::Texture*    m_gameCardTex = nullptr;
    uint64_t    m_titleId = 0;
    bool        m_focused = false;
    bool        m_focusable = true;
    bool        m_suspended = false;
    bool        m_isGameCard = false;
    bool        m_notLaunchable = false;
    nxui::Color m_loadingColor = nxui::Color::white();
    float       m_suspendPulse = 0.f;

    nxui::AnimatedFloat m_animScale;
    nxui::AnimatedFloat m_appearOpacity;
    nxui::AnimatedFloat m_focusScale;
    nxui::AnimatedFloat m_focusGlow;
    float         m_appearDelay = 0.f;
    float         m_appearTimer = 0.f;
    bool          m_appearing   = false;
    GridEntryKind m_entryKind = GridEntryKind::Application;
    int           m_folderPreviewCount = 0;
    std::uint32_t m_folderVisualSeed = 0;
    int           m_folderColorIndex = 0;
    nxui::Font*   m_font = nullptr;
};
