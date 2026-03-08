#pragma once

#include "core/Vector2.h"
#include <vector>

namespace DirtSim {

struct LandedSeed {
    Vector2i landingPosition;
    double distanceFromParent = 0.0;
};

struct TreeResourceTotals {
    double waterAbsorbed = 0.0;
    double energyProduced = 0.0;
    int seedsProduced = 0;
    std::vector<LandedSeed> landedSeeds;
};

} // namespace DirtSim
