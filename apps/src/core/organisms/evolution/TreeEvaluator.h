#pragma once

#include "core/organisms/TreeResourceTotals.h"
#include <optional>

namespace DirtSim {

class Tree;
struct FitnessContext;

struct TreeFitnessBreakdown {
    double survivalRaw = 0.0;
    double survivalReference = 0.0;
    double survivalScore = 0.0;
    double maxEnergyRaw = 0.0;
    double maxEnergyNormalized = 0.0;
    double finalEnergyRaw = 0.0;
    double finalEnergyNormalized = 0.0;
    double energyReference = 0.0;
    double energyScore = 0.0;
    double producedEnergyRaw = 0.0;
    double producedEnergyNormalized = 0.0;
    double absorbedWaterRaw = 0.0;
    double absorbedWaterNormalized = 0.0;
    double waterReference = 0.0;
    double resourceScore = 0.0;
    double partialStructureBonus = 0.0;
    double stageBonus = 0.0;
    double structureBonus = 0.0;
    double milestoneBonus = 0.0;
    double commandScore = 0.0;
    double seedScore = 0.0;
    double totalFitness = 0.0;
    double coreFitness = 0.0;
    double bonusFitness = 0.0;
    double energyMaxWeightedComponent = 0.0;
    double energyFinalWeightedComponent = 0.0;
    double resourceEnergyWeightedComponent = 0.0;
    double resourceWaterWeightedComponent = 0.0;
    double rootBelowSeedBonus = 0.0;
    double woodAboveSeedBonus = 0.0;
    int commandsAccepted = 0;
    int commandsRejected = 0;
    int idleCancels = 0;
    int leafCount = 0;
    int rootCount = 0;
    int woodCount = 0;
    int partialStructurePartCount = 0;
    double seedCountBonus = 0.0;
    double seedDistanceBonus = 0.0;
    double seedDistanceReference = 0.0;
    int seedsProduced = 0;
    int landedSeedCount = 0;
    double averageLandedSeedDistance = 0.0;
    double maxLandedSeedDistance = 0.0;
};

class TreeEvaluator {
public:
    void reset();
    void start();
    void update(const Tree& tree);

    static double evaluate(const FitnessContext& context);
    static TreeFitnessBreakdown evaluateWithBreakdown(const FitnessContext& context);

    double getMaxEnergy() const;
    const std::optional<TreeResourceTotals>& getResourceTotals() const;
    int getCommandAcceptedCount() const;
    int getCommandRejectedCount() const;
    int getIdleCancelCount() const;

private:
    double maxEnergy_ = 0.0;
    int commandAcceptedCount_ = 0;
    int commandRejectedCount_ = 0;
    int idleCancelCount_ = 0;
    std::optional<TreeResourceTotals> resourceTotals_;
};

} // namespace DirtSim
