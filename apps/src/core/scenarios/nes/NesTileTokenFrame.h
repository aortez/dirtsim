#pragma once

#include "core/Result.h"
#include "core/scenarios/nes/NesTileFrame.h"
#include "core/scenarios/nes/NesTileTokenizer.h"

#include <array>
#include <cstdint>
#include <string>

namespace DirtSim {

struct NesTileTokenFrame {
    static constexpr uint16_t VisibleTileColumns = NesTileFrame::VisibleTileColumns;
    static constexpr uint16_t VisibleTileRows = NesTileFrame::VisibleTileRows;

    uint64_t frameId = 0;
    uint16_t scrollX = 0;
    uint16_t scrollY = 0;
    std::array<NesTileTokenizer::TileToken, VisibleTileColumns * VisibleTileRows> tokens{};
};

Result<NesTileTokenFrame, std::string> makeNesTileTokenFrame(
    const NesTileFrame& tileFrame, NesTileTokenizer& tokenizer);

} // namespace DirtSim
