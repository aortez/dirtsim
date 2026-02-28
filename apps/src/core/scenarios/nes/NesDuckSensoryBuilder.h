#pragma once

#include "core/organisms/DuckSensoryData.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"
#include "core/scenarios/nes/NesPaletteFrame.h"

namespace DirtSim {

DuckSensoryData makeNesDuckSensoryDataFromPaletteFrame(
    const NesPaletteClusterer& clusterer, const NesPaletteFrame& frame, double deltaTimeSeconds);

} // namespace DirtSim
