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
constexpr double kTreePartialStructurePartBonus = 0.25;
constexpr double kTreeSaplingStageBonus = 0.5;
constexpr double kTreeSeedDistanceReference = 10.0;

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
int computePartialStructurePartCount(const TreeStructureMetrics& metrics);
double computePartialStructureBonus(const TreeStructureMetrics& metrics);

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

const TreeResourceTotals* resolveTreeResources(const FitnessContext& context, const Tree& tree)
{
    if (context.treeResources) {
        return context.treeResources;
    }
    return &tree.getResourceTotals();
}

double computeCommandOutcomeScore(const FitnessContext& context)
{
    (void)context;
    return 0.0;
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

double computeStageBonus(const Tree& tree, const TreeStructureMetrics& metrics)
{
    (void)tree;

    if (!metrics.hasSeed) {
        return 0.0;
    }

    if (metrics.hasLeaf && metrics.hasRoot && metrics.hasWoodAboveSeed) {
        return kTreeSaplingStageBonus;
    }

    return 0.0;
}

double computeSeedScore(const FitnessContext& context, const Tree& tree)
{
    const TreeResourceTotals* resources = resolveTreeResources(context, tree);
    if (!resources || resources->landedSeeds.empty()) {
        return 0.0;
    }
    double score = 0.0;
    for (const auto& seed : resources->landedSeeds) {
        score += 1.0 + saturatingScore(seed.distanceFromParent, kTreeSeedDistanceReference);
    }
    return score;
}

double computeMinimalStructureBonus(const TreeStructureMetrics& metrics)
{
    if (metrics.hasSeed && metrics.hasLeaf && metrics.hasRoot && metrics.hasWoodAboveSeed) {
        return kTreeMinimalStructureBonus;
    }

    return 0.0;
}

int computePartialStructurePartCount(const TreeStructureMetrics& metrics)
{
    if (!metrics.hasSeed) {
        return 0;
    }

    int parts = 0;
    if (metrics.hasLeaf) {
        parts++;
    }
    if (metrics.hasRoot) {
        parts++;
    }
    if (metrics.hasWoodAboveSeed) {
        parts++;
    }

    return parts;
}

double computePartialStructureBonus(const TreeStructureMetrics& metrics)
{
    return static_cast<double>(computePartialStructurePartCount(metrics))
        * kTreePartialStructurePartBonus;
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
    breakdown.commandsAccepted = context.result.commandsAccepted;
    breakdown.commandsRejected = context.result.commandsRejected;
    breakdown.idleCancels = context.result.idleCancels;
    breakdown.energyReference = context.evolutionConfig.energyReference;
    breakdown.seedDistanceReference = kTreeSeedDistanceReference;
    breakdown.survivalRaw = std::max(0.0, context.result.lifespan);
    breakdown.survivalReference = context.evolutionConfig.maxSimulationTime;
    breakdown.waterReference = context.evolutionConfig.waterReference;
    breakdown.survivalScore = computeSurvivalScore(context);
    if (breakdown.survivalScore <= 0.0) {
        return breakdown;
    }

    breakdown.commandScore = computeCommandOutcomeScore(context);

    const Tree* tree = nullptr;
    TreeStructureMetrics metrics;
    if (context.finalOrganism && context.finalOrganism->getType() == OrganismType::TREE) {
        tree = static_cast<const Tree*>(context.finalOrganism);
        metrics = computeTreeStructureMetrics(*tree);
        breakdown.leafCount = metrics.leafCount;
        breakdown.rootCount = metrics.rootCount;
        breakdown.woodCount = metrics.woodCount;
        breakdown.partialStructurePartCount = computePartialStructurePartCount(metrics);
        breakdown.structureBonus = computeMinimalStructureBonus(metrics);

        breakdown.maxEnergyRaw = std::max(0.0, context.result.maxEnergy);
        breakdown.maxEnergyNormalized = computeMaxEnergyScore(context);
        breakdown.finalEnergyRaw = std::max(0.0, tree->getEnergy());
        breakdown.finalEnergyNormalized =
            clamp01(normalize(breakdown.finalEnergyRaw, context.evolutionConfig.energyReference));
        const TreeResourceTotals* resources = resolveTreeResources(context, *tree);
        if (resources) {
            breakdown.producedEnergyRaw = std::max(0.0, resources->energyProduced);
            breakdown.producedEnergyNormalized = saturatingScore(
                breakdown.producedEnergyRaw, context.evolutionConfig.energyReference);
            breakdown.absorbedWaterRaw = std::max(0.0, resources->waterAbsorbed);
            breakdown.absorbedWaterNormalized =
                saturatingScore(breakdown.absorbedWaterRaw, context.evolutionConfig.waterReference);
            breakdown.seedsProduced = resources->seedsProduced;
            breakdown.landedSeedCount = static_cast<int>(resources->landedSeeds.size());
            if (!resources->landedSeeds.empty()) {
                double totalDistance = 0.0;
                for (const auto& seed : resources->landedSeeds) {
                    totalDistance += seed.distanceFromParent;
                    breakdown.maxLandedSeedDistance =
                        std::max(breakdown.maxLandedSeedDistance, seed.distanceFromParent);
                }
                breakdown.averageLandedSeedDistance =
                    totalDistance / static_cast<double>(resources->landedSeeds.size());
            }
        }

        breakdown.partialStructureBonus = computePartialStructureBonus(metrics);
        breakdown.stageBonus = computeStageBonus(*tree, metrics);
        breakdown.rootBelowSeedBonus = metrics.hasRootBelowSeed ? kTreeRootBelowSeedBonus : 0.0;
        breakdown.woodAboveSeedBonus = metrics.hasWoodAboveSeed ? kTreeWoodAboveSeedBonus : 0.0;
        breakdown.milestoneBonus = breakdown.rootBelowSeedBonus + breakdown.woodAboveSeedBonus;
        breakdown.seedScore = computeSeedScore(context, *tree);
        breakdown.seedCountBonus = static_cast<double>(breakdown.landedSeedCount);
        breakdown.seedDistanceBonus = breakdown.seedScore - breakdown.seedCountBonus;

        if (breakdown.structureBonus > 0.0) {
            breakdown.energyMaxWeightedComponent =
                kTreeEnergyMaxWeight * breakdown.maxEnergyNormalized;
            breakdown.energyFinalWeightedComponent =
                kTreeEnergyFinalWeight * breakdown.finalEnergyNormalized;
            breakdown.resourceEnergyWeightedComponent =
                kTreeResourceEnergyWeight * breakdown.producedEnergyNormalized;
            breakdown.resourceWaterWeightedComponent =
                kTreeResourceWaterWeight * breakdown.absorbedWaterNormalized;
            breakdown.energyScore =
                breakdown.energyMaxWeightedComponent + breakdown.energyFinalWeightedComponent;
            breakdown.resourceScore = breakdown.resourceEnergyWeightedComponent
                + breakdown.resourceWaterWeightedComponent;
        }
    }

    breakdown.coreFitness =
        breakdown.survivalScore * (1.0 + breakdown.energyScore) * (1.0 + breakdown.resourceScore);
    breakdown.bonusFitness = breakdown.partialStructureBonus + breakdown.stageBonus
        + breakdown.structureBonus + breakdown.milestoneBonus + breakdown.commandScore
        + breakdown.seedScore;
    breakdown.totalFitness = breakdown.coreFitness + breakdown.bonusFitness;
    if (tree) {
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
