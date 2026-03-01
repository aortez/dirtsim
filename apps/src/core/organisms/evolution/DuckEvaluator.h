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
    double movementRaw = 0.0;
    double movementScore = 0.0;
    double displacementScore = 0.0;
    double efficiencyScore = 0.0;
    double effortRaw = 0.0;
    double effortReference = 0.0;
    double effortScore = 0.0;
    double effortPenaltyRaw = 0.0;
    double effortPenaltyScore = 0.0;
    double coverageColumnRaw = 0.0;
    double coverageColumnReference = 0.0;
    double coverageScore = 0.0;
    double coverageColumnScore = 0.0;
    double coverageRowRaw = 0.0;
    double coverageRowReference = 0.0;
    double coverageRowScore = 0.0;
    double coverageCellRaw = 0.0;
    double coverageCellReference = 0.0;
    double coverageCellScore = 0.0;
    double totalFitness = 0.0;
};

class DuckEvaluator {
public:
    static double evaluate(const FitnessContext& context);
    static DuckFitnessBreakdown evaluateWithBreakdown(const FitnessContext& context);
};

} // namespace DirtSim
