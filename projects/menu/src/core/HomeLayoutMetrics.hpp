#pragma once

#include "core/HomeOrientation.hpp"
#include <nxui/core/Types.hpp>
#include <algorithm>
#include <cmath>

// Logical HOME layout for Flip-Grip portrait. FB stays 1280x720; when portrait,
// widgets live in 720x1280 logical space and Renderer projects into the FB
// (nwindow stays identity for 90/270 — projection owns the rotate).
struct HomeLayoutMetrics {
    static constexpr float kFbW = 1280.f;
    static constexpr float kFbH = 720.f;
    static constexpr float kPortraitW = 720.f;
    static constexpr float kPortraitH = 1280.f;

    static constexpr float kTopHudH = 90.f;
    static constexpr float kPageIndicatorH = 28.f;
    static constexpr float kPageIndicatorBottomPad = 7.f;
    static constexpr float kSidebarBtn = 70.f;
    static constexpr float kSidebarGap = 16.f;
    static constexpr float kSidebarMargin = 14.f;
    static constexpr float kGridSafeSideLandscape = 220.f;
    static constexpr float kGridSafeSidePortrait = 100.f;
    static constexpr float kGridSafeTopBottom = 20.f;

    HomeOrientation orientation = HomeOrientation::Landscape0;

    bool isPortrait() const {
        return orientation == HomeOrientation::Rot90
            || orientation == HomeOrientation::Rot270;
    }

    float logicalW() const { return isPortrait() ? kPortraitW : kFbW; }
    float logicalH() const { return isPortrait() ? kPortraitH : kFbH; }

    // Portrait draws in logical 720x1280 (true 3x5). Landscape keeps saved dims.
    int gridColumns(int savedCols) const {
        return isPortrait() ? 3 : std::clamp(savedCols, 1, 8);
    }
    int gridRows(int savedRows) const {
        return isPortrait() ? 5 : std::clamp(savedRows, 1, 5);
    }

    nxui::Rect topHud() const {
        return {0.f, 0.f, logicalW(), kTopHudH};
    }

    nxui::Rect pageIndicator() const {
        const float y = logicalH() - kPageIndicatorH - kPageIndicatorBottomPad;
        return {0.f, y, logicalW(), kPageIndicatorH};
    }

    // Vertically center on the canvas mid (same as sidebars), clipped to the
    // HUD / page-indicator safe area so the grid mid matches the side buttons.
    nxui::Rect gridBand(bool inFolder = false) const {
        const float hardTop = inFolder ? 148.f : kTopHudH;
        const float hardBottom = pageIndicator().y;
        const float mid = logicalH() * 0.5f;
        const float half = std::min(mid - hardTop, hardBottom - mid);
        const float top = mid - half;
        return {0.f, top, logicalW(), std::max(1.f, half * 2.f)};
    }

    // Full logical canvas — wallpaper / wara-wara best-fit area.
    nxui::Rect contentCanvas() const {
        return {0.f, 0.f, logicalW(), logicalH()};
    }

    float gridSafeSideMargin() const {
        return isPortrait() ? kGridSafeSidePortrait : kGridSafeSideLandscape;
    }

    float sidebarLeftX() const { return kSidebarMargin; }
    float sidebarRightX() const {
        return logicalW() - kSidebarMargin - kSidebarBtn;
    }
    float sidebarStartY() const {
        const float totalH = 3.f * kSidebarBtn + 2.f * kSidebarGap;
        return logicalH() * 0.5f - totalH * 0.5f;
    }

    // Logical → FB. Content is laid CCW into the buffer so nwindow ROT_90/270
    // presents an upright (non-mirrored) tall HOME. Rot90: (lx,ly)→(ly, FbH-lx).
    nxui::Vec2 toFramebufferPoint(float lx, float ly) const {
        switch (orientation) {
            case HomeOrientation::Rot90:
                return {ly, kFbH - lx};
            case HomeOrientation::Rot270:
                return {kFbW - ly, lx};
            case HomeOrientation::Rot180:
                return {kFbW - lx, kFbH - ly};
            case HomeOrientation::Landscape0:
            default:
                return {lx, ly};
        }
    }

    nxui::Vec2 toLogicalPoint(float fx, float fy) const {
        switch (orientation) {
            case HomeOrientation::Rot90:
                return {kFbH - fy, fx};
            case HomeOrientation::Rot270:
                return {fy, kFbW - fx};
            case HomeOrientation::Rot180:
                return {kFbW - fx, kFbH - fy};
            case HomeOrientation::Landscape0:
            default:
                return {fx, fy};
        }
    }

    // Axis-aligned bounding box of the mapped corners (for wallpaper FB fit).
    nxui::Rect toFramebufferRect(const nxui::Rect& logical) const {
        if (!isPortrait() && orientation != HomeOrientation::Rot180)
            return logical;
        const nxui::Vec2 c0 = toFramebufferPoint(logical.x, logical.y);
        const nxui::Vec2 c1 = toFramebufferPoint(logical.x + logical.width, logical.y);
        const nxui::Vec2 c2 = toFramebufferPoint(logical.x, logical.y + logical.height);
        const nxui::Vec2 c3 = toFramebufferPoint(logical.x + logical.width,
                                                 logical.y + logical.height);
        const float minX = std::min(std::min(c0.x, c1.x), std::min(c2.x, c3.x));
        const float maxX = std::max(std::max(c0.x, c1.x), std::max(c2.x, c3.x));
        const float minY = std::min(std::min(c0.y, c1.y), std::min(c2.y, c3.y));
        const float maxY = std::max(std::max(c0.y, c1.y), std::max(c2.y, c3.y));
        return {minX, minY, maxX - minX, maxY - minY};
    }
};
