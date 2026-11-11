#pragma once

#include <cstdint>

namespace nxui {

// Matches menu HomeOrientation. FB stays landscape; presentation transform +
// Input remaps keep hit-testing / focus aligned with what the user sees.
enum class DisplayOrientation : std::uint8_t {
    Landscape0 = 0,
    Rot90 = 1,
    Rot180 = 2,
    Rot270 = 3,
};

} // namespace nxui
