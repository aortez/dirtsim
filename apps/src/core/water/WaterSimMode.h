#pragma once

#include <cstdint>

namespace DirtSim {

enum class WaterSimMode : uint8_t {
    LegacyCell = 0,
    MacProjection = 1,
};

} // namespace DirtSim
