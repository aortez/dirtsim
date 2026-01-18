#include "FitnessCalculator.h"
#include "core/Assert.h"

namespace DirtSim {

double computeFitnessForOrganism(
    const FitnessResult& result,
    OrganismType organismType,
    int worldWidth,
    int worldHeight,
    const EvolutionConfig& evolutionConfig)
{
    FitnessResult adjusted = result;
    bool includeEnergy = false;

    switch (organismType) {
        case OrganismType::DUCK:
        case OrganismType::GOOSE:
            includeEnergy = false;
            break;
        case OrganismType::TREE:
            adjusted.distanceTraveled = 0.0;
            includeEnergy = true;
            break;
        default:
            DIRTSIM_ASSERT(false, "FitnessCalculator: Unknown OrganismType");
            break;
    }

    return adjusted.computeFitness(
        evolutionConfig.maxSimulationTime,
        worldWidth,
        worldHeight,
        evolutionConfig.energyReference,
        includeEnergy);
}

} // namespace DirtSim
