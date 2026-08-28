#include "GridNavigation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

int intervalGap(int firstStart, int firstEnd, int secondStart, int secondEnd) {
    if (firstEnd <= secondStart) return secondStart - firstEnd;
    if (secondEnd <= firstStart) return firstStart - secondEnd;
    return 0;
}

} // namespace

int findGridNavigationTarget(const std::vector<GridNavigationItem>& items,
                             int sourceIndex,
                             GridNavigationDirection direction) {
    const auto sourceIt = std::find_if(items.begin(), items.end(),
        [sourceIndex](const GridNavigationItem& item) {
            return item.index == sourceIndex;
        });
    if (sourceIt == items.end()) return -1;

    const GridNavigationItem& source = *sourceIt;
    const int sourceRight = source.column + std::max(1, source.columns);
    const int sourceBottom = source.row + std::max(1, source.rows);
    const float sourceCenterX = (source.column + sourceRight) * 0.5f;
    const float sourceCenterY = (source.row + sourceBottom) * 0.5f;

    int bestIndex = -1;
    int bestMainGap = std::numeric_limits<int>::max();
    int bestCrossGap = std::numeric_limits<int>::max();
    float bestCenterDistance = std::numeric_limits<float>::max();

    for (const auto& candidate : items) {
        if (candidate.index == source.index) continue;
        const int candidateRight = candidate.column + std::max(1, candidate.columns);
        const int candidateBottom = candidate.row + std::max(1, candidate.rows);

        int mainGap = 0;
        int crossGap = 0;
        float centerDistance = 0.f;
        switch (direction) {
            case GridNavigationDirection::Up:
                if (candidateBottom > source.row) continue;
                mainGap = source.row - candidateBottom;
                crossGap = intervalGap(source.column, sourceRight,
                                       candidate.column, candidateRight);
                centerDistance = std::abs(
                    (candidate.column + candidateRight) * 0.5f - sourceCenterX);
                break;
            case GridNavigationDirection::Down:
                if (candidate.row < sourceBottom) continue;
                mainGap = candidate.row - sourceBottom;
                crossGap = intervalGap(source.column, sourceRight,
                                       candidate.column, candidateRight);
                centerDistance = std::abs(
                    (candidate.column + candidateRight) * 0.5f - sourceCenterX);
                break;
            case GridNavigationDirection::Left:
                if (candidateRight > source.column) continue;
                mainGap = source.column - candidateRight;
                crossGap = intervalGap(source.row, sourceBottom,
                                       candidate.row, candidateBottom);
                centerDistance = std::abs(
                    (candidate.row + candidateBottom) * 0.5f - sourceCenterY);
                break;
            case GridNavigationDirection::Right:
                if (candidate.column < sourceRight) continue;
                mainGap = candidate.column - sourceRight;
                crossGap = intervalGap(source.row, sourceBottom,
                                       candidate.row, candidateBottom);
                centerDistance = std::abs(
                    (candidate.row + candidateBottom) * 0.5f - sourceCenterY);
                break;
        }

        if (mainGap < bestMainGap ||
            (mainGap == bestMainGap && crossGap < bestCrossGap) ||
            (mainGap == bestMainGap && crossGap == bestCrossGap &&
             centerDistance < bestCenterDistance)) {
            bestIndex = candidate.index;
            bestMainGap = mainGap;
            bestCrossGap = crossGap;
            bestCenterDistance = centerDistance;
        }
    }
    return bestIndex;
}
