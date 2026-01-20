#include "FitnessCalculator.h"
#include "core/Assert.h"
#include "core/organisms/Tree.h"

#include <algorithm>
#include <cmath>

namespace DirtSim {

namespace {
constexpr double kTreeEnergyMaxWeight = 0.7;
constexpr double kTreeEnergyFinalWeight = 0.3;
constexpr double kTreeResourceEnergyWeight = 0.6;
constexpr double kTreeResourceWaterWeight = 0.4;

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

double saturatingScore(double value, double reference)
{
    if (reference <= 0.0) {
        return 0.0;
    }
    return 1.0 - std::exp(-std::max(0.0, value) / reference);
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

double computeMaxEnergyScore(const FitnessContext& context)
{
    return clamp01(normalize(context.result.maxEnergy, context.evolutionConfig.energyReference));
}

double computeFinalEnergyScore(const FitnessContext& context)
{
    if (!context.finalOrganism || context.finalOrganism->getType() != OrganismType::TREE) {
        return 0.0;
    }
    const auto* tree = static_cast<const Tree*>(context.finalOrganism);
    return clamp01(normalize(tree->getEnergy(), context.evolutionConfig.energyReference));
}

const TreeResourceTotals* resolveTreeResources(const FitnessContext& context)
{
    if (context.finalOrganism && context.finalOrganism->getType() == OrganismType::TREE) {
        const auto* tree = static_cast<const Tree*>(context.finalOrganism);
        return &tree->getResourceTotals();
    }
    return context.treeResources;
}

double computeTreeResourceScore(const FitnessContext& context)
{
    const TreeResourceTotals* resources = resolveTreeResources(context);
    if (!resources) {
        return 0.0;
    }

    const double energyScore =
        saturatingScore(resources->energyProduced, context.evolutionConfig.energyReference);
    const double waterScore =
        saturatingScore(resources->waterAbsorbed, context.evolutionConfig.waterReference);

    return (kTreeResourceEnergyWeight * energyScore) + (kTreeResourceWaterWeight * waterScore);
}

double computeTreeEnergyScore(const FitnessContext& context)
{
    const double maxEnergyScore = computeMaxEnergyScore(context);
    if (!context.finalOrganism || context.finalOrganism->getType() != OrganismType::TREE) {
        return maxEnergyScore;
    }
    const double finalEnergyScore = computeFinalEnergyScore(context);
    return (kTreeEnergyMaxWeight * maxEnergyScore) + (kTreeEnergyFinalWeight * finalEnergyScore);
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
            const double energyScore = computeTreeEnergyScore(context);
            const double resourceScore = computeTreeResourceScore(context);
            return survivalScore * (1.0 + energyScore) * (1.0 + resourceScore);
        }
    }

    DIRTSIM_ASSERT(false, "FitnessCalculator: Unknown OrganismType");
    return 0.0;
}

} // namespace DirtSim
