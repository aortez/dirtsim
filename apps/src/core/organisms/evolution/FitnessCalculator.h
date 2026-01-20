#pragma once

#include "EvolutionConfig.h"
#include "FitnessResult.h"
#include "core/organisms/OrganismType.h"

namespace DirtSim {

namespace Organism {
class Body;
}

struct TreeResourceTotals;

struct FitnessContext {
    const FitnessResult& result;
    OrganismType organismType;
    int worldWidth;
    int worldHeight;
    const EvolutionConfig& evolutionConfig;
    const Organism::Body* finalOrganism = nullptr;
    const TreeResourceTotals* treeResources = nullptr;
};

double computeFitnessForOrganism(const FitnessContext& context);

} // namespace DirtSim
