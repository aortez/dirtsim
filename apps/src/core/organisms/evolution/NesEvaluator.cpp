#include "NesEvaluator.h"
#include "FitnessCalculator.h"
#include "core/Assert.h"

namespace DirtSim {

double NesEvaluator::evaluate(const FitnessContext& context)
{
    DIRTSIM_ASSERT(
        context.organismType == OrganismType::NES_FLAPPY_BIRD, "NesEvaluator: non-NES context");
    return evaluateFromRewardTotal(context.result.nesRewardTotal);
}

double NesEvaluator::evaluateFromRewardTotal(double rewardTotal)
{
    return rewardTotal;
}

} // namespace DirtSim
