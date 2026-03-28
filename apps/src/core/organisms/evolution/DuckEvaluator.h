#pragma once

namespace DirtSim {

struct FitnessContext;

struct DuckFitnessBreakdown {
    double survivalRaw = 0.0;
    double survivalReference = 0.0;
    double survivalScore = 0.0;
    double energyAverage = 0.0;
    double energyConsumedTotal = 0.0;
    double energyLimitedSeconds = 0.0;
    double wingUpSeconds = 0.0;
    double wingDownSeconds = 0.0;
    double collisionDamageTotal = 0.0;
    double damageTotal = 0.0;
    double fullTraversals = 0.0;
    double traversalProgress = 0.0;
    double traversalRatePer100Seconds = 0.0;
    double traversalPoints = 0.0;
    double hurdleClears = 0.0;
    double hurdleOpportunities = 0.0;
    double leftWallTouches = 0.0;
    double pitClears = 0.0;
    double pitOpportunities = 0.0;
    double obstacleClears = 0.0;
    double obstacleOpportunities = 0.0;
    double obstacleClearRatePer100Seconds = 0.0;
    double obstacleClearRatePoints = 0.0;
    double obstacleCompetenceScore = 0.0;
    double obstacleCompetencePoints = 0.0;
    double rightWallTouches = 0.0;
    double coursePoints = 0.0;
    bool exitDoorDistanceObserved = false;
    bool exitedThroughDoor = false;
    double bestExitDoorDistanceCells = 0.0;
    double exitDoorProximityScore = 0.0;
    double exitDoorProximityPoints = 0.0;
    double exitDoorTime = 0.0;
    double healthAverage = 0.0;
    double exitDoorCompletionPoints = 0.0;
    double survivalPoints = 0.0;
    double totalFitness = 0.0;
};

class DuckEvaluator {
public:
    static double evaluate(const FitnessContext& context);
    static DuckFitnessBreakdown evaluateWithBreakdown(const FitnessContext& context);
};

} // namespace DirtSim
