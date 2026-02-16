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
constexpr double kTreeRootBelowSeedBonus = 1.0;
constexpr double kTreeWoodAboveSeedBonus = 1.5;
constexpr double kTreeSaplingStageBonus = 0.5;
constexpr double kTreeMatureStageBonus = 1;
constexpr double kTreeMatureAgeSeconds = 1000.0;
constexpr int kTreeMatureLeafCount = 10;
constexpr int kTreeMatureRootCount = 10;
constexpr int kTreeMatureWoodCount = 10;
constexpr int kTreeCommandAcceptedSaturationCount = 5;
constexpr double kTreeCommandAcceptedSaturationReward = 0.5;
constexpr double kTreeCommandRejectedPenalty = 0.00001;
constexpr double kTreeIdleCancelPenalty = 0.0000001;

struct TreeStructureMetrics {
    bool hasLeaf = false;
    bool hasRoot = false;
    bool hasRootBelowSeed = false;
    bool hasSeed = false;
    bool hasWoodAboveSeed = false;
    int leafCount = 0;
    int rootCount = 0;
    int woodCount = 0;
};

TreeStructureMetrics computeTreeStructureMetrics(const Tree& tree);
double computeMinimalStructureBonus(const TreeStructureMetrics& metrics);

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
    if (context.treeResources) {
        return context.treeResources;
    }
    if (context.finalOrganism && context.finalOrganism->getType() == OrganismType::TREE) {
        const auto* tree = static_cast<const Tree*>(context.finalOrganism);
        return &tree->getResourceTotals();
    }
    return context.treeResources;
}

double computeTreeResourceScore(const FitnessContext& context)
{
    if (!context.finalOrganism || context.finalOrganism->getType() != OrganismType::TREE) {
        return 0.0;
    }

    const auto* tree = static_cast<const Tree*>(context.finalOrganism);
    const TreeStructureMetrics metrics = computeTreeStructureMetrics(*tree);
    if (computeMinimalStructureBonus(metrics) <= 0.0) {
        return 0.0;
    }

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
    if (!context.finalOrganism || context.finalOrganism->getType() != OrganismType::TREE) {
        return 0.0;
    }

    const auto* tree = static_cast<const Tree*>(context.finalOrganism);
    const TreeStructureMetrics metrics = computeTreeStructureMetrics(*tree);
    if (computeMinimalStructureBonus(metrics) <= 0.0) {
        return 0.0;
    }

    const double maxEnergyScore = computeMaxEnergyScore(context);
    const double finalEnergyScore = computeFinalEnergyScore(context);
    return (kTreeEnergyMaxWeight * maxEnergyScore) + (kTreeEnergyFinalWeight * finalEnergyScore);
}

double computeCommandOutcomeScore(const FitnessContext& context)
{
    const int accepted = std::max(0, context.result.commandsAccepted);
    const int rejected = std::max(0, context.result.commandsRejected);
    const int idleCancels = std::max(0, context.result.idleCancels);

    const int cappedAccepted = std::min(accepted, kTreeCommandAcceptedSaturationCount);
    const double acceptedRatio = static_cast<double>(cappedAccepted)
        / static_cast<double>(kTreeCommandAcceptedSaturationCount);
    const double acceptedScore = acceptedRatio * kTreeCommandAcceptedSaturationReward;

    return acceptedScore - (rejected * kTreeCommandRejectedPenalty)
        - (idleCancels * kTreeIdleCancelPenalty);
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
                if (cell.localPos.y > 0) {
                    metrics.hasRootBelowSeed = true;
                }
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

double computeMilestoneBonus(const TreeStructureMetrics& metrics)
{
    double bonus = 0.0;
    if (metrics.hasRootBelowSeed) {
        bonus += kTreeRootBelowSeedBonus;
    }
    if (metrics.hasWoodAboveSeed) {
        bonus += kTreeWoodAboveSeedBonus;
    }
    return bonus;
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
    idleCancelCount_ = 0;
    resourceTotals_.reset();
}

void TreeEvaluator::start()
{
    maxEnergy_ = 0.0;
    commandAcceptedCount_ = 0;
    commandRejectedCount_ = 0;
    idleCancelCount_ = 0;
    resourceTotals_ = TreeResourceTotals{};
}

void TreeEvaluator::update(const Tree& tree)
{
    maxEnergy_ = std::max(maxEnergy_, tree.getEnergy());
    commandAcceptedCount_ = tree.getCommandAcceptedCount();
    commandRejectedCount_ = tree.getCommandRejectedCount();
    idleCancelCount_ = tree.getIdleCancelCount();
    resourceTotals_ = tree.getResourceTotals();
}

double TreeEvaluator::evaluate(const FitnessContext& context)
{
    TreeFitnessBreakdown breakdown = evaluateWithBreakdown(context);
    return breakdown.totalFitness;
}

TreeFitnessBreakdown TreeEvaluator::evaluateWithBreakdown(const FitnessContext& context)
{
    DIRTSIM_ASSERT(
        context.organismType == OrganismType::TREE, "TreeEvaluator: Non-tree fitness context");

    TreeFitnessBreakdown breakdown{};
    breakdown.survivalScore = computeSurvivalScore(context);
    if (breakdown.survivalScore <= 0.0) {
        return breakdown;
    }

    breakdown.energyScore = computeTreeEnergyScore(context);
    breakdown.resourceScore = computeTreeResourceScore(context);
    breakdown.commandScore = computeCommandOutcomeScore(context);

    if (context.finalOrganism && context.finalOrganism->getType() == OrganismType::TREE) {
        const auto* tree = static_cast<const Tree*>(context.finalOrganism);
        const TreeStructureMetrics metrics = computeTreeStructureMetrics(*tree);
        breakdown.stageBonus = computeStageBonus(*tree, metrics);
        breakdown.structureBonus = computeMinimalStructureBonus(metrics);
        breakdown.milestoneBonus = computeMilestoneBonus(metrics);
    }

    breakdown.totalFitness =
        breakdown.survivalScore * (1.0 + breakdown.energyScore) * (1.0 + breakdown.resourceScore)
        + breakdown.stageBonus + breakdown.structureBonus + breakdown.milestoneBonus
        + breakdown.commandScore;
    if (context.finalOrganism && context.finalOrganism->getType() == OrganismType::TREE) {
        const auto* tree = static_cast<const Tree*>(context.finalOrganism);
        tree->setLastFitness(breakdown.totalFitness);
    }

    return breakdown;
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

int TreeEvaluator::getIdleCancelCount() const
{
    return idleCancelCount_;
}

} // namespace DirtSim
