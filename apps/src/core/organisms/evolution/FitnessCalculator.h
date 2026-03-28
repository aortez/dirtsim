#pragma once

#include "EvolutionConfig.h"
#include "FitnessResult.h"
#include "core/organisms/OrganismType.h"
#include "core/scenarios/nes/NesFitnessDetails.h"

#include <cstdint>
#include <optional>

namespace DirtSim {

namespace Organism {
class Body;
}

struct OrganismTrackingHistory;
struct TreeResourceTotals;

struct DuckClockEvaluationArtifacts {
    int fullTraversals = 0;
    int hurdleClears = 0;
    int hurdleOpportunities = 0;
    int leftWallTouches = 0;
    int pitClears = 0;
    int pitOpportunities = 0;
    int rightWallTouches = 0;
    double traversalProgress = 0.0;
    bool exitDoorDistanceObserved = false;
    bool exitedThroughDoor = false;
    double bestExitDoorDistanceCells = 0.0;
    double exitDoorTime = 0.0;
};

struct DuckEvaluationArtifacts {
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
    std::optional<DuckClockEvaluationArtifacts> clock = std::nullopt;
};

struct FitnessContext {
    const FitnessResult& result;
    OrganismType organismType;
    int worldWidth;
    int worldHeight;
    const EvolutionConfig& evolutionConfig;
    const Organism::Body* finalOrganism = nullptr;
    std::optional<DuckEvaluationArtifacts> duckArtifacts = std::nullopt;
    const NesFitnessDetails* nesFitnessDetails = nullptr;
    const OrganismTrackingHistory* organismTrackingHistory = nullptr;
    const TreeResourceTotals* treeResources = nullptr;
};

double computeFitnessForOrganism(const FitnessContext& context);

} // namespace DirtSim
