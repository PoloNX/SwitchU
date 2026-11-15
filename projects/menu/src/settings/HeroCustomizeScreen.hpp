#pragma once

#include "steamgriddb/SteamGridDbManager.hpp"

#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/widgets/GlassWidget.hpp>

#include <cstdint>
#include <functional>
#include <string>

/// Per-title hero crop editor (landscape + portrait slots).
class HeroCustomizeScreen final : public nxui::GlassWidget {
public:
    using Presentation = SteamGridDbManager::HeroPresentation;

    HeroCustomizeScreen(nxui::GpuDevice& gpu, nxui::Renderer& renderer);

    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setCanvasSize(float width, float height);

    void show(std::uint64_t titleId, const std::string& title);
    void hide();
    bool isActive() const { return m_active; }
    std::uint64_t titleId() const { return m_titleId; }

    void onClosed(std::function<void()> cb) { m_closedCb = std::move(cb); }
    void onSaved(std::function<void(std::uint64_t)> cb) { m_savedCb = std::move(cb); }

    /// Touch + adjust-mode analog (left pan, right zoom). Call every frame while active.
    void handleFrame(nxui::Input& input);

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;

private:
    enum class FocusItem { Tabs, Preview, Save, Cancel };

    Presentation& activeSlot();
    const Presentation& activeSlot() const;
    void loadSourceTexture();
    void enterAdjust();
    void endAdjustKeep();
    void activateFocused();
    void moveFocus(int dx, int dy);
    void setPortraitSlot(bool portrait);
    void toggleFit();
    void resetActiveSlot();
    void saveAndClose();
    void cancelAndClose();
    bool isPortraitCanvas() const { return m_canvasH > m_canvasW + 0.5f; }
    nxui::Rect panelRect() const;
    nxui::Rect tabsRect(const nxui::Rect& panel) const;
    nxui::Rect contentRect(const nxui::Rect& panel) const;
    nxui::Rect previewFrame(const nxui::Rect& content) const;
    nxui::Rect cropButtonRect(const nxui::Rect& content, const nxui::Rect& frame) const;
    nxui::Rect saveButtonRect(const nxui::Rect& content) const;
    nxui::Rect cancelButtonRect(const nxui::Rect& content) const;
    nxui::Rect focusRectFor(FocusItem item) const;
    void drawTab(nxui::Renderer& ren, const nxui::Rect& r, const std::string& label,
                 bool selected, bool focused) const;
    void drawActionButton(nxui::Renderer& ren, const nxui::Rect& r, const std::string& label,
                          bool focused, bool primary) const;
    static nxui::Rect mappedRect(const nxui::Texture& texture, const nxui::Rect& area,
                                 bool fill, float focusX, float focusY, float zoom);

    nxui::GpuDevice& m_gpu;
    nxui::Renderer& m_renderer;
    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;

    bool m_active = false;
    bool m_portraitSlot = false;
    bool m_adjusting = false;
    FocusItem m_focus = FocusItem::Preview;
    std::uint64_t m_titleId = 0;
    std::string m_title;
    Presentation m_landscape{};
    Presentation m_portrait{};
    nxui::Texture m_source;
    bool m_sourceReady = false;
    float m_canvasW = 1280.f;
    float m_canvasH = 720.f;
    float m_uiTime = 0.f;

    // Adjust-mode touch: one-finger drag pan, two-finger pinch zoom.
    bool m_touchPanActive = false;
    bool m_pinchActive = false;
    float m_touchLastX = 0.f;
    float m_touchLastY = 0.f;
    float m_pinchLastDist = 0.f;

    std::function<void()> m_closedCb;
    std::function<void(std::uint64_t)> m_savedCb;
};
