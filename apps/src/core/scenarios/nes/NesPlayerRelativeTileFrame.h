#pragma once

#include "core/scenarios/nes/NesTileTokenFrame.h"

#include <array>
#include <cstdint>

namespace DirtSim {

struct NesPlayerRelativeTileFrame {
    static constexpr uint16_t ScreenTileColumns = NesTileTokenFrame::VisibleTileColumns;
    static constexpr uint16_t ScreenTileRows = NesTileTokenFrame::VisibleTileRows;
    static constexpr uint16_t AnchorTileColumn = ScreenTileColumns - 1u;
    static constexpr uint16_t AnchorTileRow = ScreenTileRows - 1u;
    static constexpr uint16_t RelativeTileColumns = ScreenTileColumns * 2u - 1u;
    static constexpr uint16_t RelativeTileRows = ScreenTileRows * 2u - 1u;
    static constexpr uint16_t TileSizePixels = 8u;

    uint64_t frameId = 0;
    uint16_t scrollX = 0;
    uint16_t scrollY = 0;
    int16_t playerScreenX = 0;
    int16_t playerScreenY = 0;
    int16_t playerTileColumn = 0;
    int16_t playerTileRow = 0;
    std::array<NesTileTokenizer::TileToken, RelativeTileColumns * RelativeTileRows> tokens{};
};

NesPlayerRelativeTileFrame makeNesPlayerRelativeTileFrame(
    const NesTileTokenFrame& tokenFrame, int16_t playerScreenX, int16_t playerScreenY);

} // namespace DirtSim
