#pragma once

#include <cstdint>
#include <string>

// Flip-Grip / TATE presentation for HOME. UI stays 1280x720; NWindow transform
// rotates the buffer onto the panel and Input remaps touch/sticks to match.
enum class HomeOrientation : std::uint8_t {
    Landscape0 = 0,
    Rot90 = 1,
    Rot180 = 2,
    Rot270 = 3,
};

inline constexpr std::uint8_t kHomeOrientationCount = 4;
inline constexpr std::uint8_t kHomeOrientationCycleAll = 0x0F;

inline const char* homeOrientationToString(HomeOrientation o) {
    switch (o) {
        case HomeOrientation::Rot90:  return "rot90";
        case HomeOrientation::Rot180: return "rot180";
        case HomeOrientation::Rot270: return "rot270";
        case HomeOrientation::Landscape0:
        default:                      return "landscape";
    }
}

inline HomeOrientation homeOrientationFromString(const std::string& s) {
    if (s == "rot90" || s == "90" || s == "portrait90")
        return HomeOrientation::Rot90;
    if (s == "rot180" || s == "180")
        return HomeOrientation::Rot180;
    if (s == "rot270" || s == "270" || s == "portrait270")
        return HomeOrientation::Rot270;
    return HomeOrientation::Landscape0;
}

inline std::uint8_t homeOrientationBit(HomeOrientation o) {
    return static_cast<std::uint8_t>(1u << static_cast<unsigned>(o));
}

inline bool homeOrientationCycleIncludes(std::uint8_t mask, HomeOrientation o) {
    return (mask & homeOrientationBit(o)) != 0;
}

// When the HOME button is enabled, keep at least two cycle entries so the
// control can actually toggle. Prefer current + landscape.
inline std::uint8_t sanitizeHomeOrientationCycle(std::uint8_t mask,
                                                 HomeOrientation current,
                                                 bool buttonEnabled) {
    mask &= kHomeOrientationCycleAll;
    if (!buttonEnabled)
        return mask == 0 ? kHomeOrientationCycleAll : mask;

    int count = 0;
    for (std::uint8_t i = 0; i < kHomeOrientationCount; ++i) {
        if (mask & (1u << i))
            ++count;
    }
    if (count >= 2)
        return mask;

    mask |= homeOrientationBit(current);
    mask |= homeOrientationBit(HomeOrientation::Landscape0);
    if (mask == homeOrientationBit(current)) {
        // Current was already landscape — add Rot90 so the button can cycle.
        mask |= homeOrientationBit(HomeOrientation::Rot90);
    }
    return mask;
}

inline HomeOrientation nextHomeOrientation(HomeOrientation current,
                                           std::uint8_t cycleMask) {
    cycleMask = sanitizeHomeOrientationCycle(cycleMask, current, true);
    const unsigned start = static_cast<unsigned>(current);
    for (unsigned step = 1; step <= kHomeOrientationCount; ++step) {
        const unsigned idx = (start + step) % kHomeOrientationCount;
        if (cycleMask & (1u << idx))
            return static_cast<HomeOrientation>(idx);
    }
    return current;
}
