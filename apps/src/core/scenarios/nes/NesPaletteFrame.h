#pragma once

#include <cstdint>
#include <vector>

namespace DirtSim {

struct NesPaletteFrame {
    uint16_t width = 0;
    uint16_t height = 0;
    uint64_t frameId = 0;
    std::vector<uint8_t> indices;
};

} // namespace DirtSim
