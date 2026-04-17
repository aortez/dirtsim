#pragma once

#include "core/Result.h"
#include "core/scenarios/nes/NesTileFrame.h"
#include "core/scenarios/nes/NesTileSensoryData.h"
#include "core/scenarios/nes/NesTileTokenizer.h"

#include <array>
#include <cstdint>
#include <string>

namespace DirtSim {

struct NesTileSensoryBuilderInput {
    int16_t playerScreenX = 0;
    int16_t playerScreenY = 0;
    float facingX = 0.0f;
    float selfViewX = 0.5f;
    float selfViewY = 0.5f;
    uint8_t controllerMask = 0u;
    std::array<double, NesTileSensoryData::SpecialSenseCount> specialSenses{};
    float energy = 1.0f;
    float health = 1.0f;
    double deltaTimeSeconds = 0.0;
};

Result<NesTileSensoryData, std::string> makeNesTileSensoryDataFromPpuSnapshot(
    const NesPpuSnapshot& ppuSnapshot,
    NesTileTokenizer& tokenizer,
    const NesTileSensoryBuilderInput& input);

Result<NesTileSensoryData, std::string> makeNesTileSensoryDataFromTileFrame(
    const NesTileFrame& tileFrame,
    NesTileTokenizer& tokenizer,
    const NesTileSensoryBuilderInput& input);

} // namespace DirtSim
