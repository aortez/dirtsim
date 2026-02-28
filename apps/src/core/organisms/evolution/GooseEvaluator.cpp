#include "GooseEvaluator.h"
#include "FitnessCalculator.h"
#include "MovementScoring.h"
#include "core/Assert.h"

namespace DirtSim {

namespace {
double computeSurvivalScore(const FitnessContext& context)
{
    return MovementScoring::clamp01(
        MovementScoring::normalize(
            context.result.lifespan, context.evolutionConfig.maxSimulationTime));
}
} // namespace

double GooseEvaluator::evaluate(const FitnessContext& context)
{
    DIRTSIM_ASSERT(
        context.organismType == OrganismType::GOOSE, "GooseEvaluator: non-goose context");
    const double survivalScore = computeSurvivalScore(context);
    if (survivalScore <= 0.0) {
        return 0.0;
    }

    const MovementScoring::Scores movement = MovementScoring::computeLegacyScores(context);
    return survivalScore * (1.0 + movement.movementScore);
}

} // namespace DirtSim
