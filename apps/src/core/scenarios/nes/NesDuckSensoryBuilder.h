#pragma once

#include "core/organisms/DuckSensoryData.h"
#include "core/scenarios/nes/NesPaletteClusterer.h"
#include "core/scenarios/nes/NesPaletteFrame.h"

namespace DirtSim {

DuckSensoryData makeNesDuckSensoryDataFromPaletteFrame(
    const NesPaletteClusterer& clusterer, const NesPaletteFrame& frame, double deltaTimeSeconds);

DuckSensoryData makeNesDuckSensoryData(
    const NesPaletteClusterer& clusterer,
    const NesPaletteFrame* frame,
    double deltaTimeSeconds,
    const std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT>& specialSenses);

} // namespace DirtSim
