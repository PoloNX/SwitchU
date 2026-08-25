#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <nxui/core/Types.hpp>
#include <nxui/core/Animation.hpp>
#include "core/AppLayoutMode.hpp"
#include <vector>
#include <memory>
#include <functional>


class GlossyIcon;

class IconGrid : public nxui::Widget {
public:
    IconGrid();

    void setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
               int cols, int rows,
               float cellW, float cellH,
               float padX, float padY);
    void reconfigureLayout(int cols, int rows,
                           float cellW, float cellH,
                           float padX, float padY);

    void setLayoutMode(AppLayoutMode mode);
    AppLayoutMode layoutMode() const { return m_layoutMode; }
    bool isDynamicLine() const { return m_layoutMode == AppLayoutMode::DynamicLine; }
    bool isDynamicLineScrolling() const;
    void setDynamicLineUpTarget(nxui::Widget* target);

    void setPage(int page);
    int  currentPage()  const { return m_page; }
    int  totalPages()   const { return m_totalPages; }
    int  columns()      const { return m_cols; }
    int  rowsPerPage()  const { return m_rows; }
    int  iconsPerPage() const { return m_cols * m_rows; }

    nxui::FocusManager& focusManager() { return m_focus; }
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons() const { return m_allIcons; }

    std::vector<GlossyIcon*> pageIcons() const;

    int hitTest(float screenX, float screenY) const;
    nxui::Rect focusedDisplayRect() const;

    int focusedGlobalIndex() const;
    bool focusGlobalIndex(int idx);
    bool swapSlots(int a, int b);

    void startAppearAnimation();

    void startPageTransition(int targetPage);
    bool isTransitioning() const { return m_sliding; }

    void setSlideTransition(bool enabled) { m_slideTransition = enabled; }
    void setEdgePaging(bool enabled) { m_edgePaging = enabled; }
    void onEdgePage(std::function<void(int dir)> cb) { m_onEdgePage = std::move(cb); }

    void onPageSwitched(std::function<void()> cb) { m_onPageSwitched = std::move(cb); }

    void render(nxui::Renderer& ren) override;

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    void layoutPage();
    void layoutLine();
    void positionPage(int page, float dx);
    void renderPageAt(nxui::Renderer& ren, int page, float dx);
    void renderDynamicLine(nxui::Renderer& ren);
    void bindEdgeActions(int start, int end);
    nxui::Rect dynamicIconRect(int index, float* outScale = nullptr,
                               float* outOpacity = nullptr,
                               float* outDistance = nullptr) const;
    float pageStride() const;

    std::vector<std::shared_ptr<GlossyIcon>> m_allIcons;
    nxui::FocusManager m_focus;

    AppLayoutMode m_layoutMode = AppLayoutMode::Grid;
    nxui::AnimatedFloat m_lineScrollOffset{0.f};
    nxui::AnimatedFloat m_layoutReveal{1.f};
    nxui::Widget* m_lineUpTarget = nullptr;

    int m_cols = 5, m_rows = 3;
    int m_page = 0, m_totalPages = 1;
    float m_cellW = 200, m_cellH = 200;
    float m_padX  = 20,  m_padY  = 20;
    float m_originX = 0, m_originY = 0;
    static constexpr float kLineScrollDuration = 0.34f;

    bool  m_slideTransition = false;
    bool  m_edgePaging      = false;
    bool  m_sliding         = false;
    int   m_slidePrevPage   = 0;
    int   m_slideDir        = 1;
    float m_slideT          = 0.f;
    float m_slideInDx       = 0.f;
    float m_slideOutDx      = 0.f;
    static constexpr float kSlideDuration = 0.30f;

    std::function<void()> m_onPageSwitched;
    std::function<void(int)> m_onEdgePage;
};
