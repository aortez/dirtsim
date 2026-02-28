#pragma once

#include "core/Vector2.h"
#include <cstdint>
#include <vector>

namespace DirtSim {

struct FitnessContext;

namespace MovementScoring {

struct Scores {
    double movementScore = 0.0;
    double movementRaw = 0.0;
    double displacementScore = 0.0;
    double efficiencyScore = 0.0;
    double effortRaw = 0.0;
    double effortReference = 0.0;
    double effortScore = 0.0;
    double effortPenaltyRaw = 0.0;
    double effortPenaltyScore = 0.0;
    double coverageScore = 0.0;
    double coverageColumnRaw = 0.0;
    double coverageColumnReference = 0.0;
    double coverageColumnScore = 0.0;
    double coverageRowRaw = 0.0;
    double coverageRowReference = 0.0;
    double coverageRowScore = 0.0;
    double coverageCellRaw = 0.0;
    double coverageCellReference = 0.0;
    double coverageCellScore = 0.0;
};

double clamp01(double value);
double normalize(double value, double reference);
double saturatingScore(double value, double reference);

void markVisitedColumnCellCoverage(
    const Vector2d& position,
    int worldWidth,
    int worldHeight,
    std::vector<uint8_t>& visitedColumns,
    std::vector<uint8_t>& visitedCells);

void markVisitedColumnRowCellCoverage(
    const Vector2d& position,
    int worldWidth,
    int worldHeight,
    std::vector<uint8_t>& visitedColumns,
    std::vector<uint8_t>& visitedRows,
    std::vector<uint8_t>& visitedCells);

Scores computeLegacyScores(const FitnessContext& context);

} // namespace MovementScoring

} // namespace DirtSim
