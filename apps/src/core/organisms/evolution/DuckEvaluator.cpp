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
    double columnCoverageWeight = 0.45;
    double effortPenaltyWeight = 0.50;
    double effortReference = 1.0;
    double jumpHeldEffortWeight = 25;
    double rowCoverageReferenceHeightScale = 0.40;
    double rowCoverageWeight = 0.45;
    double cellCoverageReferenceDiagonalScale = 0.75;
};

const DuckMovementScoringConfig kDuckScoringConfig{};

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

    const double coverageColumnReference =
        std::max(1.0, kDuckScoringConfig.columnCoverageReferenceWidthScale * worldWidth);
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
        MovementScoring::saturatingScore(uniqueColumnProgress, coverageColumnReference);
    scores.coverageRowScore =
        MovementScoring::saturatingScore(uniqueRowProgress, coverageRowReference);
    scores.coverageCellScore =
        MovementScoring::saturatingScore(uniqueCellProgress, coverageCellReference);
    scores.coverageScore = (kDuckScoringConfig.columnCoverageWeight * scores.coverageColumnScore)
        + (kDuckScoringConfig.rowCoverageWeight * scores.coverageRowScore)
        + (kDuckScoringConfig.cellCoverageWeight * scores.coverageCellScore);

    const auto* duck = dynamic_cast<const Duck*>(context.finalOrganism);
    if (duck && duck->getEffortSampleCount() > 0) {
        const double sampleCount = static_cast<double>(duck->getEffortSampleCount());
        const double averageAbsMoveInput = duck->getEffortAbsMoveInputTotal() / sampleCount;
        const double jumpHeldRatio = duck->getEffortJumpHeldTotal() / sampleCount;
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

    const double uncoveredFraction = 1.0 - MovementScoring::clamp01(scores.coverageScore);
    scores.effortPenaltyRaw = MovementScoring::clamp01(
        kDuckScoringConfig.effortPenaltyWeight * scores.effortScore * uncoveredFraction);
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
    breakdown.totalFitness = breakdown.survivalScore * (1.0 + breakdown.movementScore);
    return breakdown;
}

} // namespace DirtSim
