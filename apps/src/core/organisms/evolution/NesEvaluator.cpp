#include "NesEvaluator.h"
#include "FitnessCalculator.h"
#include "core/Assert.h"

namespace DirtSim {

double NesEvaluator::evaluate(const FitnessContext& context)
{
    DIRTSIM_ASSERT(context.organismType == OrganismType::NES_DUCK, "NesEvaluator: non-NES context");
    if (context.nesFitnessDetails != nullptr
        && std::holds_alternative<NesSuperMarioBrosFitnessSnapshot>(*context.nesFitnessDetails)) {
        return evaluateSuperMarioBrosWithBreakdown(context).totalFitness;
    }
    return evaluateFromRewardTotal(context.result.nesRewardTotal);
}

double NesEvaluator::evaluateFromRewardTotal(double rewardTotal)
{
    return rewardTotal;
}

NesSuperMarioBrosFitnessBreakdown NesEvaluator::evaluateSuperMarioBrosWithBreakdown(
    const FitnessContext& context)
{
    DIRTSIM_ASSERT(context.organismType == OrganismType::NES_DUCK, "NesEvaluator: non-NES context");

    NesSuperMarioBrosFitnessBreakdown breakdown{
        .totalFitness = context.result.nesRewardTotal,
    };
    if (context.nesFitnessDetails == nullptr) {
        return breakdown;
    }

    const auto* snapshot = std::get_if<NesSuperMarioBrosFitnessSnapshot>(context.nesFitnessDetails);
    if (snapshot == nullptr) {
        return breakdown;
    }

    breakdown.totalFitness = snapshot->totalReward;
    breakdown.distanceRewardTotal = snapshot->distanceRewardTotal;
    breakdown.levelClearRewardTotal = snapshot->levelClearRewardTotal;
    breakdown.gameplayFrames = snapshot->gameplayFrames;
    breakdown.framesSinceProgress = snapshot->framesSinceProgress;
    breakdown.noProgressTimeoutFrames = snapshot->noProgressTimeoutFrames;
    breakdown.bestStageIndex = snapshot->bestStageIndex;
    breakdown.bestWorld = snapshot->bestWorld;
    breakdown.bestLevel = snapshot->bestLevel;
    breakdown.bestAbsoluteX = snapshot->bestAbsoluteX;
    breakdown.currentWorld = snapshot->currentWorld;
    breakdown.currentLevel = snapshot->currentLevel;
    breakdown.currentAbsoluteX = snapshot->currentAbsoluteX;
    breakdown.currentLives = snapshot->currentLives;
    breakdown.endReason = snapshot->endReason;
    breakdown.done = snapshot->done;
    return breakdown;
}

} // namespace DirtSim
