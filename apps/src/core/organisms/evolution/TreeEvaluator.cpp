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
constexpr double kTreeMinimalStructureBonus = 1;
constexpr double kTreeSaplingStageBonus = 0.2;
constexpr double kTreeMatureStageBonus = 1;
constexpr double kTreeMatureAgeSeconds = 1000.0;
constexpr int kTreeMatureLeafCount = 10;
constexpr int kTreeMatureRootCount = 10;
constexpr int kTreeMatureWoodCount = 10;
constexpr double kTreeCommandAcceptedReward = 0.001;
constexpr double kTreeCommandRejectedPenalty = 0.00005;

struct TreeStructureMetrics {
    bool hasLeaf = false;
    bool hasRoot = false;
    bool hasSeed = false;
    bool hasWoodAboveSeed = false;
    int leafCount = 0;
    int rootCount = 0;
    int woodCount = 0;
};

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

double computeCommandOutcomeScore(const FitnessContext& context)
{
    const int accepted = context.result.commandsAccepted;
    const int rejected = context.result.commandsRejected;
    return (accepted * kTreeCommandAcceptedReward) - (rejected * kTreeCommandRejectedPenalty);
}

TreeStructureMetrics computeTreeStructureMetrics(const Tree& tree)
{
    TreeStructureMetrics metrics;

    for (const auto& cell : tree.local_shape) {
        switch (cell.material) {
            case Material::EnumType::Air:
                break;
            case Material::EnumType::Dirt:
                break;
            case Material::EnumType::Leaf:
                metrics.hasLeaf = true;
                metrics.leafCount++;
                break;
            case Material::EnumType::Metal:
                break;
            case Material::EnumType::Root:
                metrics.hasRoot = true;
                metrics.rootCount++;
                break;
            case Material::EnumType::Sand:
                break;
            case Material::EnumType::Seed:
                metrics.hasSeed = true;
                break;
            case Material::EnumType::Wall:
                break;
            case Material::EnumType::Water:
                break;
            case Material::EnumType::Wood:
                metrics.woodCount++;
                if (cell.localPos.y < 0) {
                    metrics.hasWoodAboveSeed = true;
                }
                break;
        }
    }

    return metrics;
}

double computeStageBonus(const Tree& tree, const TreeStructureMetrics& metrics)
{
    if (!metrics.hasSeed) {
        return 0.0;
    }

    if (tree.getAge() >= kTreeMatureAgeSeconds && metrics.leafCount >= kTreeMatureLeafCount
        && metrics.rootCount >= kTreeMatureRootCount && metrics.woodCount >= kTreeMatureWoodCount) {
        return kTreeMatureStageBonus;
    }

    if (metrics.hasLeaf && metrics.hasRoot && metrics.hasWoodAboveSeed) {
        return kTreeSaplingStageBonus;
    }

    return 0.0;
}

double computeMinimalStructureBonus(const TreeStructureMetrics& metrics)
{
    if (metrics.hasSeed && metrics.hasLeaf && metrics.hasRoot && metrics.hasWoodAboveSeed) {
        return kTreeMinimalStructureBonus;
    }

    return 0.0;
}
} // namespace

void TreeEvaluator::reset()
{
    maxEnergy_ = 0.0;
    commandAcceptedCount_ = 0;
    commandRejectedCount_ = 0;
    resourceTotals_.reset();
}

void TreeEvaluator::start()
{
    maxEnergy_ = 0.0;
    commandAcceptedCount_ = 0;
    commandRejectedCount_ = 0;
    resourceTotals_ = TreeResourceTotals{};
}

void TreeEvaluator::update(const Tree& tree)
{
    maxEnergy_ = std::max(maxEnergy_, tree.getEnergy());
    commandAcceptedCount_ = tree.getCommandAcceptedCount();
    commandRejectedCount_ = tree.getCommandRejectedCount();
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
    const double commandScore = computeCommandOutcomeScore(context);

    double stageBonus = 0.0;
    double structureBonus = 0.0;
    if (context.finalOrganism && context.finalOrganism->getType() == OrganismType::TREE) {
        const auto* tree = static_cast<const Tree*>(context.finalOrganism);
        const TreeStructureMetrics metrics = computeTreeStructureMetrics(*tree);
        stageBonus = computeStageBonus(*tree, metrics);
        structureBonus = computeMinimalStructureBonus(metrics);
    }

    const double fitness = survivalScore * (1.0 + energyScore) * (1.0 + resourceScore) + stageBonus
        + structureBonus + commandScore;
    if (context.finalOrganism && context.finalOrganism->getType() == OrganismType::TREE) {
        const auto* tree = static_cast<const Tree*>(context.finalOrganism);
        tree->setLastFitness(fitness);
    }

    return fitness;
}

double TreeEvaluator::getMaxEnergy() const
{
    return maxEnergy_;
}

const std::optional<TreeResourceTotals>& TreeEvaluator::getResourceTotals() const
{
    return resourceTotals_;
}

int TreeEvaluator::getCommandAcceptedCount() const
{
    return commandAcceptedCount_;
}

int TreeEvaluator::getCommandRejectedCount() const
{
    return commandRejectedCount_;
}

} // namespace DirtSim
