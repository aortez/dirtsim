#pragma once

namespace DirtSim {

struct FitnessContext;

class GooseEvaluator {
public:
    static double evaluate(const FitnessContext& context);
};

} // namespace DirtSim
