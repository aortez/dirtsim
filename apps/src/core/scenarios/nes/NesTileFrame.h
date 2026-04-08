#pragma once

#include "core/scenarios/nes/NesPpuSnapshot.h"

#include <array>
#include <cstdint>

namespace DirtSim {

struct NesTileFrame {
    static constexpr uint16_t VisibleHeightPixels = 224u;
    static constexpr uint16_t VisibleTileColumns = 32u;
    static constexpr uint16_t VisibleTileRows = 28u;
    static constexpr uint16_t VisibleWidthPixels = 256u;

    uint64_t frameId = 0;
    uint16_t scrollX = 0;
    uint16_t scrollY = 0;
    std::array<uint8_t, VisibleWidthPixels * VisibleHeightPixels> patternPixels{};
    std::array<uint64_t, VisibleTileColumns * VisibleTileRows> tilePatternHashes{};
    std::array<uint8_t, VisibleTileColumns * VisibleTileRows> tileIds{};
};

NesTileFrame makeNesTileFrame(const NesPpuSnapshot& snapshot);

} // namespace DirtSim
