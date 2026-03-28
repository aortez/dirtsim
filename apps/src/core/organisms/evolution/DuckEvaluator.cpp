#include "DuckEvaluator.h"
#include "FitnessCalculator.h"
#include "MovementScoring.h"
#include "core/Assert.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {

namespace {
struct DuckClockScoringConfig {
    double deathPenaltyExponent = 2.0;
    double exitDoorCompletionPoints = 150.0;
    double exitDoorProximityRadiusCells = 10.0;
    double exitDoorProximityPoints = 100.0;
    double obstacleClearRatePoints = 100.0;
    double obstacleCompetenceOpportunityReference = 3.0;
    double obstacleCompetencePoints = 100.0;
    double referenceDurationSeconds = 100.0;
    double survivalPoints = 500.0;
    double traversalPoints = 500.0;
};

const DuckClockScoringConfig kDuckClockScoringConfig{};

const DuckEvaluationArtifacts* duckArtifactsGet(const FitnessContext& context)
{
    return context.duckArtifacts.has_value() ? &context.duckArtifacts.value() : nullptr;
}

const DuckClockEvaluationArtifacts* duckClockArtifactsGet(const FitnessContext& context)
{
    const auto* artifacts = duckArtifactsGet(context);
    if (artifacts == nullptr || !artifacts->clock.has_value()) {
        return nullptr;
    }
    return &artifacts->clock.value();
}

double computeSurvivalScore(const FitnessContext& context)
{
    return MovementScoring::clamp01(
        MovementScoring::normalize(
            context.result.lifespan, context.evolutionConfig.maxSimulationTime));
}

double computeExitDoorProximityScore(double bestExitDoorDistanceCells)
{
    return MovementScoring::clamp01(
        (kDuckClockScoringConfig.exitDoorProximityRadiusCells - bestExitDoorDistanceCells)
        / kDuckClockScoringConfig.exitDoorProximityRadiusCells);
}

double computePer100Seconds(double total, double durationSeconds)
{
    if (durationSeconds <= 0.0) {
        return 0.0;
    }
    return kDuckClockScoringConfig.referenceDurationSeconds * std::max(0.0, total)
        / durationSeconds;
}

double computeObstacleCompetenceScore(double clears, double opportunities)
{
    if (opportunities <= 0.0) {
        return 0.0;
    }

    const double creditedClears = std::min(clears, opportunities);
    const double successRate = MovementScoring::clamp01(creditedClears / opportunities);
    const double confidence = MovementScoring::saturatingScore(
        opportunities, kDuckClockScoringConfig.obstacleCompetenceOpportunityReference);
    return successRate * confidence;
}

} // namespace

double DuckEvaluator::evaluate(const FitnessContext& context)
{
    return evaluateWithBreakdown(context).totalFitness;
}

DuckFitnessBreakdown DuckEvaluator::evaluateWithBreakdown(const FitnessContext& context)
{
    DIRTSIM_ASSERT(context.organismType == OrganismType::DUCK, "DuckEvaluator: non-duck context");

    const auto* artifacts = duckArtifactsGet(context);
    const auto* clock = duckClockArtifactsGet(context);
    DIRTSIM_ASSERT(
        artifacts != nullptr && clock != nullptr,
        "DuckEvaluator: duck fitness requires clock evaluation artifacts");

    DuckFitnessBreakdown breakdown;
    breakdown.survivalRaw = std::max(0.0, context.result.lifespan);
    breakdown.survivalReference = context.evolutionConfig.maxSimulationTime;
    breakdown.survivalScore = computeSurvivalScore(context);

    breakdown.energyAverage = artifacts->energyAverage;
    breakdown.energyConsumedTotal = artifacts->energyConsumedTotal;
    breakdown.energyLimitedSeconds = artifacts->energyLimitedSeconds;
    breakdown.healthAverage = artifacts->healthAverage;
    breakdown.collisionDamageTotal = artifacts->collisionDamageTotal;
    breakdown.damageTotal = artifacts->damageTotal;
    breakdown.wingUpSeconds = artifacts->wingUpSeconds;
    breakdown.wingDownSeconds = artifacts->wingDownSeconds;

    breakdown.fullTraversals = static_cast<double>(clock->fullTraversals);
    breakdown.hurdleClears = std::min(
        static_cast<double>(clock->hurdleClears), static_cast<double>(clock->hurdleOpportunities));
    breakdown.hurdleOpportunities = static_cast<double>(clock->hurdleOpportunities);
    breakdown.leftWallTouches = static_cast<double>(clock->leftWallTouches);
    breakdown.pitClears = std::min(
        static_cast<double>(clock->pitClears), static_cast<double>(clock->pitOpportunities));
    breakdown.pitOpportunities = static_cast<double>(clock->pitOpportunities);
    breakdown.rightWallTouches = static_cast<double>(clock->rightWallTouches);
    breakdown.traversalProgress = std::max(breakdown.fullTraversals, clock->traversalProgress);
    breakdown.traversalRatePer100Seconds =
        computePer100Seconds(breakdown.traversalProgress, breakdown.survivalRaw);
    breakdown.traversalPoints =
        kDuckClockScoringConfig.traversalPoints * breakdown.traversalRatePer100Seconds;

    breakdown.obstacleClears = breakdown.pitClears + breakdown.hurdleClears;
    breakdown.obstacleOpportunities = breakdown.pitOpportunities + breakdown.hurdleOpportunities;
    breakdown.obstacleClearRatePer100Seconds =
        computePer100Seconds(breakdown.obstacleClears, breakdown.survivalRaw);
    breakdown.obstacleClearRatePoints =
        kDuckClockScoringConfig.obstacleClearRatePoints * breakdown.obstacleClearRatePer100Seconds;
    breakdown.obstacleCompetenceScore =
        computeObstacleCompetenceScore(breakdown.obstacleClears, breakdown.obstacleOpportunities);
    breakdown.obstacleCompetencePoints =
        kDuckClockScoringConfig.obstacleCompetencePoints * breakdown.obstacleCompetenceScore;
    breakdown.coursePoints = breakdown.traversalPoints + breakdown.obstacleClearRatePoints
        + breakdown.obstacleCompetencePoints;

    breakdown.exitDoorDistanceObserved = clock->exitDoorDistanceObserved;
    breakdown.exitedThroughDoor = clock->exitedThroughDoor;
    breakdown.bestExitDoorDistanceCells = clock->bestExitDoorDistanceCells;
    breakdown.exitDoorTime = std::max(0.0, clock->exitDoorTime);
    if (breakdown.exitedThroughDoor) {
        breakdown.exitDoorProximityScore = 1.0;
    }
    else if (breakdown.exitDoorDistanceObserved) {
        breakdown.exitDoorProximityScore =
            computeExitDoorProximityScore(clock->bestExitDoorDistanceCells);
    }
    breakdown.exitDoorProximityPoints =
        kDuckClockScoringConfig.exitDoorProximityPoints * breakdown.exitDoorProximityScore;
    breakdown.exitDoorCompletionPoints =
        breakdown.exitedThroughDoor ? kDuckClockScoringConfig.exitDoorCompletionPoints : 0.0;
    breakdown.survivalPoints = kDuckClockScoringConfig.survivalPoints * breakdown.survivalScore;

    if (context.result.organismDied && !breakdown.exitedThroughDoor) {
        const double deathPenaltyMultiplier =
            std::pow(breakdown.survivalScore, kDuckClockScoringConfig.deathPenaltyExponent);
        breakdown.coursePoints *= deathPenaltyMultiplier;
        breakdown.exitDoorProximityPoints *= deathPenaltyMultiplier;
        breakdown.exitDoorCompletionPoints *= deathPenaltyMultiplier;
        breakdown.obstacleClearRatePoints *= deathPenaltyMultiplier;
        breakdown.obstacleCompetencePoints *= deathPenaltyMultiplier;
        breakdown.survivalPoints *= deathPenaltyMultiplier;
        breakdown.traversalPoints *= deathPenaltyMultiplier;
    }

    breakdown.totalFitness = breakdown.survivalPoints + breakdown.coursePoints
        + breakdown.exitDoorProximityPoints + breakdown.exitDoorCompletionPoints;
    return breakdown;
}

} // namespace DirtSim
