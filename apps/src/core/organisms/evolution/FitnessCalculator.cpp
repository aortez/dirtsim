#include "FitnessCalculator.h"
#include "core/Assert.h"
#include "core/organisms/evolution/TreeEvaluator.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {

namespace {
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

double computeSurvivalScore(const FitnessContext& context)
{
    return clamp01(normalize(context.result.lifespan, context.evolutionConfig.maxSimulationTime));
}

double computeDistanceScore(const FitnessContext& context)
{
    const double maxDistance = std::max(
        1.0,
        std::hypot(
            static_cast<double>(context.worldWidth), static_cast<double>(context.worldHeight)));
    return clamp01(normalize(context.result.distanceTraveled, maxDistance));
}

} // namespace

double computeFitnessForOrganism(const FitnessContext& context)
{
    const double survivalScore = computeSurvivalScore(context);
    if (survivalScore <= 0.0) {
        return 0.0;
    }

    switch (context.organismType) {
        case OrganismType::DUCK:
        case OrganismType::GOOSE: {
            const double distanceScore = computeDistanceScore(context);
            return survivalScore * (1.0 + distanceScore);
        }
        case OrganismType::TREE: {
            return TreeEvaluator::evaluate(context);
        }
    }

    DIRTSIM_ASSERT(false, "FitnessCalculator: Unknown OrganismType");
    return 0.0;
}

} // namespace DirtSim
