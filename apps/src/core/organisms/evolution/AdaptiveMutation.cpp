#include "AdaptiveMutation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace DirtSim {
namespace {

constexpr double kExplorePerturbationsScale = 1.5;
constexpr int kExplorePerturbationsMinIncrease = 50;
constexpr double kExploreResetsScale = 2.0;
constexpr int kExploreResetsMinIncrease = 1;
constexpr double kExploreSigmaScale = 1.25;
constexpr double kExploreSigmaMinIncrease = 0.01;

constexpr double kRescuePerturbationsScale = 2.5;
constexpr int kRescuePerturbationsMinIncrease = 150;
constexpr double kRescueResetsScale = 4.0;
constexpr int kRescueResetsMinIncrease = 2;
constexpr double kRescueSigmaScale = 1.6;
constexpr double kRescueSigmaMinIncrease = 0.02;

int clampToInt(int64_t value)
{
    if (value <= 0) {
        return 0;
    }
    if (value >= std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(value);
}

int scaledBudgetResolve(int baseline, double scale, int minIncrease)
{
    const int64_t scaled =
        static_cast<int64_t>(std::llround(static_cast<double>(baseline) * scale));
    const int64_t increased = static_cast<int64_t>(baseline) + static_cast<int64_t>(minIncrease);
    return clampToInt(std::max({ static_cast<int64_t>(baseline), scaled, increased }));
}

double scaledSigmaResolve(double baseline, double scale, double minIncrease)
{
    return std::max({ baseline, baseline * scale, baseline + minIncrease });
}

int recoveryBudgetResolve(
    int baseline, int previous, int recoveryLevel, int recoveryWindowGenerations)
{
    if (recoveryLevel <= 0 || recoveryWindowGenerations <= 0 || previous <= baseline) {
        return baseline;
    }

    const int remainingSteps = std::max(recoveryLevel - 1, 0);
    const double factor =
        static_cast<double>(remainingSteps) / static_cast<double>(recoveryWindowGenerations);
    const double resolved = static_cast<double>(baseline)
        + static_cast<double>(previous - baseline) * std::clamp(factor, 0.0, 1.0);
    return clampToInt(static_cast<int64_t>(std::llround(resolved)));
}

double recoverySigmaResolve(
    double baseline, double previous, int recoveryLevel, int recoveryWindowGenerations)
{
    if (recoveryLevel <= 0 || recoveryWindowGenerations <= 0 || previous <= baseline) {
        return baseline;
    }

    const int remainingSteps = std::max(recoveryLevel - 1, 0);
    const double factor =
        static_cast<double>(remainingSteps) / static_cast<double>(recoveryWindowGenerations);
    return baseline + (previous - baseline) * std::clamp(factor, 0.0, 1.0);
}

} // namespace

EffectiveAdaptiveMutation adaptiveMutationResolve(
    const MutationConfig& baselineConfig,
    const TrainingPhaseStatus& trainingPhaseStatus,
    const EffectiveAdaptiveMutation& previousEffective,
    const EvolutionConfig& evolutionConfig)
{
    EffectiveAdaptiveMutation effective{
        .mode = AdaptiveMutationMode::Baseline,
        .mutationConfig = baselineConfig,
    };

    if (!baselineConfig.useBudget) {
        return effective;
    }

    switch (trainingPhaseStatus.phase) {
        case TrainingPhase::Normal:
            return effective;
        case TrainingPhase::Plateau:
            effective.mode = AdaptiveMutationMode::Explore;
            effective.mutationConfig.perturbationsPerOffspring = scaledBudgetResolve(
                baselineConfig.perturbationsPerOffspring,
                kExplorePerturbationsScale,
                kExplorePerturbationsMinIncrease);
            effective.mutationConfig.resetsPerOffspring = scaledBudgetResolve(
                baselineConfig.resetsPerOffspring, kExploreResetsScale, kExploreResetsMinIncrease);
            effective.mutationConfig.sigma = scaledSigmaResolve(
                baselineConfig.sigma, kExploreSigmaScale, kExploreSigmaMinIncrease);
            return effective;
        case TrainingPhase::Stuck:
            effective.mode = AdaptiveMutationMode::Rescue;
            effective.mutationConfig.perturbationsPerOffspring = scaledBudgetResolve(
                baselineConfig.perturbationsPerOffspring,
                kRescuePerturbationsScale,
                kRescuePerturbationsMinIncrease);
            effective.mutationConfig.resetsPerOffspring = scaledBudgetResolve(
                baselineConfig.resetsPerOffspring, kRescueResetsScale, kRescueResetsMinIncrease);
            effective.mutationConfig.sigma = scaledSigmaResolve(
                baselineConfig.sigma, kRescueSigmaScale, kRescueSigmaMinIncrease);
            return effective;
        case TrainingPhase::Recovery:
            effective.mode = AdaptiveMutationMode::Recover;
            effective.mutationConfig.perturbationsPerOffspring = recoveryBudgetResolve(
                baselineConfig.perturbationsPerOffspring,
                previousEffective.mutationConfig.perturbationsPerOffspring,
                trainingPhaseStatus.recoveryLevel,
                evolutionConfig.recoveryWindowGenerations);
            effective.mutationConfig.resetsPerOffspring = recoveryBudgetResolve(
                baselineConfig.resetsPerOffspring,
                previousEffective.mutationConfig.resetsPerOffspring,
                trainingPhaseStatus.recoveryLevel,
                evolutionConfig.recoveryWindowGenerations);
            effective.mutationConfig.sigma = recoverySigmaResolve(
                baselineConfig.sigma,
                previousEffective.mutationConfig.sigma,
                trainingPhaseStatus.recoveryLevel,
                evolutionConfig.recoveryWindowGenerations);
            return effective;
    }

    return effective;
}

} // namespace DirtSim
