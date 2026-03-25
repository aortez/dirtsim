#include "DuckEvaluator.h"
#include "FitnessCalculator.h"
#include "MovementScoring.h"
#include "OrganismTracker.h"
#include "core/Assert.h"
#include "core/organisms/Duck.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace DirtSim {

namespace {
struct DuckMovementScoringConfig {
    double cellCoverageWeight = 0.10;
    double columnCoverageReferenceWidthScale = 0.40;
    double columnCoverageWeight = 0.65;
    double effortPenaltyWeight = 0.15;
    double effortReference = 1.0;
    double jumpHeldEffortWeight = 10;
    double rowCoverageReferenceHeightScale = 0.40;
    double rowCoverageWeight = 0.25;
    double cellCoverageReferenceDiagonalScale = 0.75;
};

const DuckMovementScoringConfig kDuckScoringConfig{};

struct DuckClockScoringConfig {
    double exitDoorProximityRadiusCells = 10.0;
    double exitDoorCompletionBonus = 0.5;
    double exitDoorProximityWeight = 0.25;
    double obstacleClearCountReference = 3.0;
    double obstacleClearCountWeight = 0.30;
    double obstacleClearRateWeight = 0.70;
    double hurdleScoreWeight = 0.40;
    double obstacleWeight = 0.30;
    double pitScoreWeight = 0.60;
    double traversalReference = 2.0;
    double traversalWeight = 0.45;
};

const DuckClockScoringConfig kDuckClockScoringConfig{};

const DuckEvaluationArtifacts* duckArtifactsGet(const FitnessContext& context)
{
    return context.duckArtifacts.has_value() ? &context.duckArtifacts.value() : nullptr;
}

MovementScoring::Scores computeDuckMovementScores(const FitnessContext& context)
{
    MovementScoring::Scores scores;
    const OrganismTrackingHistory* history = context.organismTrackingHistory;
    if (!history || history->samples.empty()) {
        return scores;
    }

    const int worldWidth = std::max(1, context.worldWidth);
    const int worldHeight = std::max(1, context.worldHeight);
    const double worldDiagonal = std::hypot(static_cast<double>(worldWidth), worldHeight);
    std::vector<uint8_t> visitedColumns(static_cast<size_t>(worldWidth), 0);
    std::vector<uint8_t> visitedRows(static_cast<size_t>(worldHeight), 0);
    std::vector<uint8_t> visitedCells(static_cast<size_t>(worldWidth * worldHeight), 0);

    for (const auto& sample : history->samples) {
        MovementScoring::markVisitedColumnRowCellCoverage(
            sample.position, worldWidth, worldHeight, visitedColumns, visitedRows, visitedCells);
    }

    const size_t uniqueColumnCount = static_cast<size_t>(
        std::count(visitedColumns.begin(), visitedColumns.end(), static_cast<uint8_t>(1)));
    const size_t uniqueRowCount = static_cast<size_t>(
        std::count(visitedRows.begin(), visitedRows.end(), static_cast<uint8_t>(1)));
    const size_t uniqueCellCount = static_cast<size_t>(
        std::count(visitedCells.begin(), visitedCells.end(), static_cast<uint8_t>(1)));
    const double uniqueColumnProgress = std::max(0.0, static_cast<double>(uniqueColumnCount) - 1.0);
    const double uniqueRowProgress = std::max(0.0, static_cast<double>(uniqueRowCount) - 1.0);
    const double uniqueCellProgress = std::max(0.0, static_cast<double>(uniqueCellCount) - 1.0);

    const double coverageColumnReference = std::max(1.0, static_cast<double>(worldWidth) - 1.0);
    const double coverageRowReference =
        std::max(1.0, kDuckScoringConfig.rowCoverageReferenceHeightScale * worldHeight);
    const double coverageCellReference =
        std::max(1.0, kDuckScoringConfig.cellCoverageReferenceDiagonalScale * worldDiagonal);

    scores.coverageColumnRaw = uniqueColumnProgress;
    scores.coverageColumnReference = coverageColumnReference;
    scores.coverageRowRaw = uniqueRowProgress;
    scores.coverageRowReference = coverageRowReference;
    scores.coverageCellRaw = uniqueCellProgress;
    scores.coverageCellReference = coverageCellReference;
    scores.coverageColumnScore =
        MovementScoring::clamp01(uniqueColumnProgress / coverageColumnReference);
    scores.coverageRowScore =
        MovementScoring::saturatingScore(uniqueRowProgress, coverageRowReference);
    scores.coverageCellScore =
        MovementScoring::saturatingScore(uniqueCellProgress, coverageCellReference);
    scores.coverageScore = (kDuckScoringConfig.columnCoverageWeight * scores.coverageColumnScore)
        + (kDuckScoringConfig.rowCoverageWeight * scores.coverageRowScore)
        + (kDuckScoringConfig.cellCoverageWeight * scores.coverageCellScore);

    const auto* duck = dynamic_cast<const Duck*>(context.finalOrganism);
    const auto* artifacts = duckArtifactsGet(context);
    const uint64_t effortSamples =
        artifacts ? artifacts->effortSampleCount : (duck ? duck->getEffortSampleCount() : 0);
    if (effortSamples > 0) {
        const double sampleCount = static_cast<double>(effortSamples);
        const double absMoveTotal =
            artifacts ? artifacts->effortAbsMoveInputTotal : duck->getEffortAbsMoveInputTotal();
        const double jumpHeldTotal =
            artifacts ? artifacts->effortJumpHeldTotal : duck->getEffortJumpHeldTotal();
        const double averageAbsMoveInput = absMoveTotal / sampleCount;
        const double jumpHeldRatio = jumpHeldTotal / sampleCount;
        const double combinedEffort = std::max(0.0, averageAbsMoveInput)
            + (kDuckScoringConfig.jumpHeldEffortWeight * std::max(0.0, jumpHeldRatio));
        scores.effortRaw = combinedEffort;
        scores.effortReference = kDuckScoringConfig.effortReference;
        scores.effortScore =
            MovementScoring::saturatingScore(combinedEffort, kDuckScoringConfig.effortReference);
    }
    else {
        scores.effortReference = kDuckScoringConfig.effortReference;
    }

    scores.effortPenaltyRaw =
        MovementScoring::clamp01(kDuckScoringConfig.effortPenaltyWeight * scores.effortScore);
    scores.effortPenaltyScore = scores.effortPenaltyRaw;
    scores.movementRaw = scores.coverageScore - scores.effortPenaltyScore;
    scores.movementScore = std::max(0.0, scores.movementRaw);
    return scores;
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

double computeOpportunityRate(double clears, double opportunities)
{
    if (opportunities <= 0.0) {
        return 0.0;
    }
    return MovementScoring::clamp01(clears / opportunities);
}

double computeObstacleClearScore(double clears, double opportunities)
{
    if (opportunities <= 0.0) {
        return 0.0;
    }

    const double creditedClears = std::min(clears, opportunities);
    const double rateScore = computeOpportunityRate(creditedClears, opportunities);
    const double countScore = MovementScoring::saturatingScore(
        creditedClears, kDuckClockScoringConfig.obstacleClearCountReference);
    return (kDuckClockScoringConfig.obstacleClearRateWeight * rateScore)
        + (kDuckClockScoringConfig.obstacleClearCountWeight * countScore);
}

} // namespace

double DuckEvaluator::evaluate(const FitnessContext& context)
{
    return evaluateWithBreakdown(context).totalFitness;
}

DuckFitnessBreakdown DuckEvaluator::evaluateWithBreakdown(const FitnessContext& context)
{
    DIRTSIM_ASSERT(context.organismType == OrganismType::DUCK, "DuckEvaluator: non-duck context");

    DuckFitnessBreakdown breakdown;
    breakdown.survivalRaw = std::max(0.0, context.result.lifespan);
    breakdown.survivalReference = context.evolutionConfig.maxSimulationTime;
    breakdown.survivalScore = computeSurvivalScore(context);
    if (breakdown.survivalScore <= 0.0) {
        return breakdown;
    }

    const auto* duck = dynamic_cast<const Duck*>(context.finalOrganism);
    const auto* artifacts = duckArtifactsGet(context);
    if (artifacts) {
        breakdown.energyAverage = artifacts->energyAverage;
        breakdown.energyConsumedTotal = artifacts->energyConsumedTotal;
        breakdown.energyLimitedSeconds = artifacts->energyLimitedSeconds;
        breakdown.healthAverage = artifacts->healthAverage;
        breakdown.collisionDamageTotal = artifacts->collisionDamageTotal;
        breakdown.damageTotal = artifacts->damageTotal;
        breakdown.wingUpSeconds = artifacts->wingUpSeconds;
        breakdown.wingDownSeconds = artifacts->wingDownSeconds;
    }
    else if (duck) {
        breakdown.energyAverage = duck->getEnergyAverage();
        breakdown.energyConsumedTotal = duck->getEnergyConsumedTotal();
        breakdown.energyLimitedSeconds = duck->getEnergyLimitedSeconds();
        breakdown.healthAverage = duck->getHealthAverage();
        breakdown.collisionDamageTotal = duck->getCollisionDamageTotal();
        breakdown.damageTotal = duck->getDamageTotal();
        breakdown.wingUpSeconds = duck->getWingUpSeconds();
        breakdown.wingDownSeconds = duck->getWingDownSeconds();
    }

    const MovementScoring::Scores movement = computeDuckMovementScores(context);
    breakdown.coverageCellScore = movement.coverageCellScore;
    breakdown.coverageCellRaw = movement.coverageCellRaw;
    breakdown.coverageCellReference = movement.coverageCellReference;
    breakdown.coverageColumnScore = movement.coverageColumnScore;
    breakdown.coverageColumnRaw = movement.coverageColumnRaw;
    breakdown.coverageColumnReference = movement.coverageColumnReference;
    breakdown.coverageRowScore = movement.coverageRowScore;
    breakdown.coverageRowRaw = movement.coverageRowRaw;
    breakdown.coverageRowReference = movement.coverageRowReference;
    breakdown.coverageScore = movement.coverageScore;
    breakdown.displacementScore = movement.displacementScore;
    breakdown.effortPenaltyRaw = movement.effortPenaltyRaw;
    breakdown.effortPenaltyScore = movement.effortPenaltyScore;
    breakdown.effortRaw = movement.effortRaw;
    breakdown.effortReference = movement.effortReference;
    breakdown.effortScore = movement.effortScore;
    breakdown.efficiencyScore = movement.efficiencyScore;
    breakdown.movementRaw = movement.movementRaw;
    breakdown.movementScore = movement.movementScore;

    if (artifacts && artifacts->clock.has_value()) {
        const DuckClockEvaluationArtifacts& clock = artifacts->clock.value();
        breakdown.fullTraversals = static_cast<double>(clock.fullTraversals);
        breakdown.hurdleOpportunities = static_cast<double>(clock.hurdleOpportunities);
        breakdown.hurdleClears = static_cast<double>(clock.hurdleClears);
        breakdown.leftWallTouches = static_cast<double>(clock.leftWallTouches);
        breakdown.pitClears = static_cast<double>(clock.pitClears);
        breakdown.pitOpportunities = static_cast<double>(clock.pitOpportunities);
        breakdown.rightWallTouches = static_cast<double>(clock.rightWallTouches);
        const double traversalProgress =
            std::max(breakdown.fullTraversals, clock.traversalProgress);
        breakdown.traversalScore = MovementScoring::saturatingScore(
            traversalProgress, kDuckClockScoringConfig.traversalReference);
        breakdown.traversalBonus =
            kDuckClockScoringConfig.traversalWeight * breakdown.traversalScore;
        breakdown.pitClearScore =
            computeObstacleClearScore(breakdown.pitClears, breakdown.pitOpportunities);
        breakdown.hurdleClearScore =
            computeObstacleClearScore(breakdown.hurdleClears, breakdown.hurdleOpportunities);
        breakdown.obstacleScore = (kDuckClockScoringConfig.pitScoreWeight * breakdown.pitClearScore)
            + (kDuckClockScoringConfig.hurdleScoreWeight * breakdown.hurdleClearScore);
        breakdown.pitClearBonus = kDuckClockScoringConfig.obstacleWeight
            * (kDuckClockScoringConfig.pitScoreWeight * breakdown.pitClearScore);
        breakdown.hurdleClearBonus = kDuckClockScoringConfig.obstacleWeight
            * (kDuckClockScoringConfig.hurdleScoreWeight * breakdown.hurdleClearScore);
        breakdown.obstacleBonus = kDuckClockScoringConfig.obstacleWeight * breakdown.obstacleScore;
        breakdown.exitDoorDistanceObserved = clock.exitDoorDistanceObserved;
        breakdown.exitedThroughDoor = clock.exitedThroughDoor;
        breakdown.bestExitDoorDistanceCells = clock.bestExitDoorDistanceCells;
        breakdown.exitDoorTime = std::max(0.0, clock.exitDoorTime);

        if (breakdown.exitedThroughDoor) {
            breakdown.exitDoorProximityScore = 1.0;
        }
        else if (breakdown.exitDoorDistanceObserved) {
            breakdown.exitDoorProximityScore =
                computeExitDoorProximityScore(clock.bestExitDoorDistanceCells);
        }

        breakdown.exitDoorRaw = breakdown.exitDoorProximityScore;
        breakdown.exitDoorProximityBonus =
            kDuckClockScoringConfig.exitDoorProximityWeight * breakdown.exitDoorProximityScore;
        breakdown.exitDoorBonus =
            breakdown.exitedThroughDoor ? kDuckClockScoringConfig.exitDoorCompletionBonus : 0.0;
        breakdown.clockBonus =
            breakdown.traversalBonus + breakdown.obstacleBonus + breakdown.exitDoorProximityBonus;
    }

    breakdown.totalFitness =
        breakdown.survivalScore * (1.0 + breakdown.movementScore + breakdown.clockBonus)
        + breakdown.exitDoorBonus;
    return breakdown;
}

} // namespace DirtSim
