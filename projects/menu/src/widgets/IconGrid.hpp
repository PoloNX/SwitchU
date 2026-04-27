#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <nxui/core/Types.hpp>
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

    int focusedGlobalIndex() const;
    bool focusGlobalIndex(int idx);
    bool swapSlots(int a, int b);

    void startAppearAnimation();

    void startWaveTransition(int targetPage);
    bool isTransitioning() const { return m_waveActive; }

    void onPageSwitched(std::function<void()> cb) { m_onPageSwitched = std::move(cb); }

    void render(nxui::Renderer& ren) override;

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    void layoutPage();

    std::vector<std::shared_ptr<GlossyIcon>> m_allIcons;
    nxui::FocusManager m_focus;

    int m_cols = 5, m_rows = 3;
    int m_page = 0, m_totalPages = 1;
    float m_cellW = 200, m_cellH = 200;
    float m_padX  = 20,  m_padY  = 20;
    float m_originX = 0, m_originY = 0;

    enum class WavePhase { Idle, Capture, Animating };
    WavePhase m_wavePhase    = WavePhase::Idle;
    bool  m_waveActive       = false;
    int   m_waveTargetPage   = 0;
    float m_waveTime         = 0.f;
    float m_waveDuration     = 0.35f;

    std::function<void()> m_onPageSwitched;
};

