#pragma once

namespace DirtSim {

struct FitnessContext;

struct DuckFitnessBreakdown {
    double survivalScore = 0.0;
    double movementScore = 0.0;
    double displacementScore = 0.0;
    double efficiencyScore = 0.0;
    double coverageScore = 0.0;
    double coverageColumnScore = 0.0;
    double coverageCellScore = 0.0;
    double totalFitness = 0.0;
};

class DuckEvaluator {
public:
    static double evaluate(const FitnessContext& context);
    static DuckFitnessBreakdown evaluateWithBreakdown(const FitnessContext& context);
};

} // namespace DirtSim
