#include "MovementScoring.h"
#include "FitnessCalculator.h"
#include "OrganismTracker.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {
namespace MovementScoring {

namespace {
struct LegacyScoringConfig {
    double cellCoverageWeight = 0.15;
    double columnCoverageWeight = 0.85;
    double coverageWeight = 0.15;
    double displacementReferenceWidthScale = 0.35;
    double displacementWeight = 0.55;
    double efficiencyWeight = 0.30;
    double epsilon = 1e-6;
    double pathDeadband = 0.01;
    double pathReferenceWidthScale = 0.60;
    double cellCoverageReferenceDiagonalScale = 0.75;
    double columnCoverageReferenceWidthScale = 0.40;
    double verticalDistanceWeight = 0.20;
};

const LegacyScoringConfig kLegacyScoringConfig{};

double weightedDistance(const Vector2d& from, const Vector2d& to)
{
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    return std::hypot(dx, dy * kLegacyScoringConfig.verticalDistanceWeight);
}

} // namespace

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double normalize(double value, double reference)
{
    if (reference <= 0.0) {
        return 0.0;
    }
    return std::max(0.0, value) / reference;
}

double saturatingScore(double value, double reference)
{
    if (reference <= 0.0) {
        return 0.0;
    }
    return 1.0 - std::exp(-std::max(0.0, value) / reference);
}

void markVisitedColumnCellCoverage(
    const Vector2d& position,
    int worldWidth,
    int worldHeight,
    std::vector<uint8_t>& visitedColumns,
    std::vector<uint8_t>& visitedCells)
{
    if (worldWidth < 1 || worldHeight < 1) {
        return;
    }

    const int clampedX = std::clamp(static_cast<int>(std::floor(position.x)), 0, worldWidth - 1);
    const int clampedY = std::clamp(static_cast<int>(std::floor(position.y)), 0, worldHeight - 1);
    const size_t columnIndex = static_cast<size_t>(clampedX);
    const size_t cellIndex = static_cast<size_t>(clampedY * worldWidth + clampedX);

    visitedColumns[columnIndex] = 1;
    visitedCells[cellIndex] = 1;
}

void markVisitedColumnRowCellCoverage(
    const Vector2d& position,
    int worldWidth,
    int worldHeight,
    std::vector<uint8_t>& visitedColumns,
    std::vector<uint8_t>& visitedRows,
    std::vector<uint8_t>& visitedCells)
{
    if (worldWidth < 1 || worldHeight < 1) {
        return;
    }

    const int clampedX = std::clamp(static_cast<int>(std::floor(position.x)), 0, worldWidth - 1);
    const int clampedY = std::clamp(static_cast<int>(std::floor(position.y)), 0, worldHeight - 1);
    const size_t columnIndex = static_cast<size_t>(clampedX);
    const size_t rowIndex = static_cast<size_t>(clampedY);
    const size_t cellIndex = static_cast<size_t>(clampedY * worldWidth + clampedX);

    visitedColumns[columnIndex] = 1;
    visitedRows[rowIndex] = 1;
    visitedCells[cellIndex] = 1;
}

Scores computeLegacyScores(const FitnessContext& context)
{
    Scores scores;
    const OrganismTrackingHistory* history = context.organismTrackingHistory;
    if (!history || history->samples.empty()) {
        return scores;
    }

    const int worldWidth = std::max(1, context.worldWidth);
    const int worldHeight = std::max(1, context.worldHeight);
    const double worldDiagonal = std::hypot(static_cast<double>(worldWidth), worldHeight);
    std::vector<uint8_t> visitedColumns(static_cast<size_t>(worldWidth), 0);
    std::vector<uint8_t> visitedCells(static_cast<size_t>(worldWidth * worldHeight), 0);

    const Vector2d startPosition = history->samples.front().position;
    double maxDisplacement = 0.0;
    double pathDistance = 0.0;

    markVisitedColumnCellCoverage(
        startPosition, worldWidth, worldHeight, visitedColumns, visitedCells);

    for (size_t i = 1; i < history->samples.size(); ++i) {
        const Vector2d& previousPosition = history->samples[i - 1].position;
        const Vector2d& currentPosition = history->samples[i].position;
        const double stepDistance = weightedDistance(previousPosition, currentPosition);
        pathDistance += std::max(0.0, stepDistance - kLegacyScoringConfig.pathDeadband);
        maxDisplacement =
            std::max(maxDisplacement, weightedDistance(startPosition, currentPosition));
        markVisitedColumnCellCoverage(
            currentPosition, worldWidth, worldHeight, visitedColumns, visitedCells);
    }

    const Vector2d endPosition = history->samples.back().position;
    const double netDisplacement = weightedDistance(startPosition, endPosition);
    const double efficiency =
        clamp01(netDisplacement / std::max(kLegacyScoringConfig.epsilon, pathDistance));

    const size_t uniqueColumnCount = static_cast<size_t>(
        std::count(visitedColumns.begin(), visitedColumns.end(), static_cast<uint8_t>(1)));
    const size_t uniqueCellCount = static_cast<size_t>(
        std::count(visitedCells.begin(), visitedCells.end(), static_cast<uint8_t>(1)));
    const double uniqueColumnProgress = std::max(0.0, static_cast<double>(uniqueColumnCount) - 1.0);
    const double uniqueCellProgress = std::max(0.0, static_cast<double>(uniqueCellCount) - 1.0);

    const double displacementReference =
        std::max(1.0, kLegacyScoringConfig.displacementReferenceWidthScale * worldWidth);
    const double pathReference =
        std::max(1.0, kLegacyScoringConfig.pathReferenceWidthScale * worldWidth);
    const double coverageColumnReference =
        std::max(1.0, kLegacyScoringConfig.columnCoverageReferenceWidthScale * worldWidth);
    const double coverageCellReference =
        std::max(1.0, kLegacyScoringConfig.cellCoverageReferenceDiagonalScale * worldDiagonal);

    const double pathScore = saturatingScore(pathDistance, pathReference);
    scores.displacementScore = saturatingScore(maxDisplacement, displacementReference);
    scores.efficiencyScore = pathScore * efficiency;
    scores.coverageColumnRaw = uniqueColumnProgress;
    scores.coverageColumnReference = coverageColumnReference;
    scores.coverageColumnScore = saturatingScore(uniqueColumnProgress, coverageColumnReference);
    scores.coverageCellRaw = uniqueCellProgress;
    scores.coverageCellReference = coverageCellReference;
    scores.coverageCellScore = saturatingScore(uniqueCellProgress, coverageCellReference);
    scores.coverageScore = (kLegacyScoringConfig.columnCoverageWeight * scores.coverageColumnScore)
        + (kLegacyScoringConfig.cellCoverageWeight * scores.coverageCellScore);
    scores.movementScore = (kLegacyScoringConfig.displacementWeight * scores.displacementScore)
        + (kLegacyScoringConfig.efficiencyWeight * scores.efficiencyScore)
        + (kLegacyScoringConfig.coverageWeight * scores.coverageScore);
    scores.movementRaw = scores.movementScore;
    return scores;
}

} // namespace MovementScoring
} // namespace DirtSim
