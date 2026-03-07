#pragma once

#include "EvolutionConfig.h"
#include "FitnessResult.h"
#include "core/organisms/OrganismType.h"

#include <cstdint>

namespace DirtSim {

namespace Organism {
class Body;
}

struct OrganismTrackingHistory;
struct TreeResourceTotals;

// Snapshot of duck stats taken before the duck is removed from the world.
struct DuckStatsSnapshot {
    double collisionDamageTotal = 0.0;
    double damageTotal = 0.0;
    double effortAbsMoveInputTotal = 0.0;
    double effortJumpHeldTotal = 0.0;
    double energyAverage = 0.0;
    double energyConsumedTotal = 0.0;
    double energyLimitedSeconds = 0.0;
    double healthAverage = 0.0;
    double wingDownSeconds = 0.0;
    double wingUpSeconds = 0.0;
    uint64_t effortSampleCount = 0;
};

struct FitnessContext {
    const FitnessResult& result;
    OrganismType organismType;
    int worldWidth;
    int worldHeight;
    const EvolutionConfig& evolutionConfig;
    const Organism::Body* finalOrganism = nullptr;
    const DuckStatsSnapshot* duckStatsSnapshot = nullptr;
    const OrganismTrackingHistory* organismTrackingHistory = nullptr;
    const TreeResourceTotals* treeResources = nullptr;
    bool exitedThroughDoor = false;
    double exitDoorTime = 0.0;
};

double computeFitnessForOrganism(const FitnessContext& context);

} // namespace DirtSim
