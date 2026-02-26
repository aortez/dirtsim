#pragma once

namespace DirtSim {

struct FitnessContext;

class NesEvaluator {
public:
    static double evaluate(const FitnessContext& context);
    static double evaluateFromRewardTotal(double rewardTotal);
};

} // namespace DirtSim
