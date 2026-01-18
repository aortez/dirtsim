#pragma once

#include "EvolutionConfig.h"
#include "FitnessResult.h"
#include "core/organisms/OrganismType.h"

namespace DirtSim {

double computeFitnessForOrganism(
    const FitnessResult& result,
    OrganismType organismType,
    int worldWidth,
    int worldHeight,
    const EvolutionConfig& evolutionConfig);

} // namespace DirtSim
