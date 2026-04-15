#pragma once

#include "core/scenarios/nes/NesPlayerRelativeTileFrame.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace DirtSim {

struct NesTileSensoryData {
    static constexpr size_t SpecialSenseCount = 32u;

    NesPlayerRelativeTileFrame tileFrame;
    float facingX = 0.0f;
    float selfViewX = 0.5f;
    float selfViewY = 0.5f;
    float previousControlX = 0.0f;
    float previousControlY = 0.0f;
    bool previousA = false;
    bool previousB = false;
    std::array<double, SpecialSenseCount> specialSenses{};
    float energy = 1.0f;
    float health = 1.0f;
    double deltaTimeSeconds = 0.0;
};

void setNesTilePreviousControlFromControllerMask(
    NesTileSensoryData& sensory, uint8_t controllerMask);

} // namespace DirtSim
