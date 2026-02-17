#pragma once

namespace DirtSim {

struct FitnessContext;

struct DuckFitnessBreakdown {
    double survivalScore = 0.0;
    double distanceScore = 0.0;
    double totalFitness = 0.0;
};

class DuckEvaluator {
public:
    static double evaluate(const FitnessContext& context);
    static DuckFitnessBreakdown evaluateWithBreakdown(const FitnessContext& context);
};

} // namespace DirtSim
