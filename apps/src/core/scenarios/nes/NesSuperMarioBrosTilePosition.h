#pragma once

#include "core/scenarios/nes/NesSuperMarioBrosEvaluator.h"

#include <cstdint>

namespace DirtSim {

int16_t makeNesSuperMarioBrosPlayerTileScreenX(
    const NesSuperMarioBrosState& state, uint16_t tileFrameScrollX);
int16_t makeNesSuperMarioBrosPlayerTileScreenY(const NesSuperMarioBrosState& state);

} // namespace DirtSim
