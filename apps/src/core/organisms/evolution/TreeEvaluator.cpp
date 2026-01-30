#include "core/organisms/evolution/TreeEvaluator.h"

#include "core/Assert.h"
#include "core/organisms/Tree.h"
#include "core/organisms/evolution/FitnessCalculator.h"
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

void TreeEvaluator::reset()
{
    maxEnergy_ = 0.0;
    resourceTotals_.reset();
}

void TreeEvaluator::start()
{
    maxEnergy_ = 0.0;
    resourceTotals_ = TreeResourceTotals{};
}

void TreeEvaluator::update(const Tree& tree)
{
    maxEnergy_ = std::max(maxEnergy_, tree.getEnergy());
    resourceTotals_ = tree.getResourceTotals();
}

double TreeEvaluator::evaluate(const FitnessContext& context)
{
    DIRTSIM_ASSERT(
        context.organismType == OrganismType::TREE, "TreeEvaluator: Non-tree fitness context");

    const double survivalScore = computeSurvivalScore(context);
    if (survivalScore <= 0.0) {
        return 0.0;
    }

    const double energyScore = computeTreeEnergyScore(context);
    const double resourceScore = computeTreeResourceScore(context);
    return survivalScore * (1.0 + energyScore) * (1.0 + resourceScore);
}

double TreeEvaluator::getMaxEnergy() const
{
    return maxEnergy_;
}

const std::optional<TreeResourceTotals>& TreeEvaluator::getResourceTotals() const
{
    return resourceTotals_;
}

} // namespace DirtSim
