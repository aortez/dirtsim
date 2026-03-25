#include "core/organisms/evolution/AdaptiveMutation.h"

#include <gtest/gtest.h>

using namespace DirtSim;

namespace {

MutationConfig makeBudgetedBaseline()
{
    return MutationConfig{
        .perturbationsPerOffspring = 200,
        .resetsPerOffspring = 1,
        .sigma = 0.05,
    };
}

} // namespace

TEST(AdaptiveMutationTest, PlateauIncreasesBudgetedMutationSettings)
{
    const MutationConfig baseline = makeBudgetedBaseline();
    const TrainingPhaseStatus phaseStatus{
        .phase = TrainingPhase::Plateau,
    };
    const EffectiveAdaptiveMutation previous{
        .mode = AdaptiveMutationMode::Baseline,
        .mutationConfig = baseline,
    };

    const EffectiveAdaptiveMutation effective =
        adaptiveMutationResolve(baseline, phaseStatus, previous, EvolutionConfig{});

    EXPECT_EQ(effective.mode, AdaptiveMutationMode::Explore);
    EXPECT_GT(
        effective.mutationConfig.perturbationsPerOffspring, baseline.perturbationsPerOffspring);
    EXPECT_GT(effective.mutationConfig.resetsPerOffspring, baseline.resetsPerOffspring);
    EXPECT_GT(effective.mutationConfig.sigma, baseline.sigma);
}

TEST(AdaptiveMutationTest, StuckIsMoreAggressiveThanPlateau)
{
    const MutationConfig baseline = makeBudgetedBaseline();
    const EffectiveAdaptiveMutation previous{
        .mode = AdaptiveMutationMode::Baseline,
        .mutationConfig = baseline,
    };

    const EffectiveAdaptiveMutation explore = adaptiveMutationResolve(
        baseline,
        TrainingPhaseStatus{ .phase = TrainingPhase::Plateau },
        previous,
        EvolutionConfig{});
    const EffectiveAdaptiveMutation rescue = adaptiveMutationResolve(
        baseline,
        TrainingPhaseStatus{ .phase = TrainingPhase::Stuck },
        previous,
        EvolutionConfig{});

    EXPECT_EQ(rescue.mode, AdaptiveMutationMode::Rescue);
    EXPECT_GT(
        rescue.mutationConfig.perturbationsPerOffspring,
        explore.mutationConfig.perturbationsPerOffspring);
    EXPECT_GT(rescue.mutationConfig.resetsPerOffspring, explore.mutationConfig.resetsPerOffspring);
    EXPECT_GT(rescue.mutationConfig.sigma, explore.mutationConfig.sigma);
}

TEST(AdaptiveMutationTest, RecoveryDecaysPreviousEffectiveSettingsTowardBaseline)
{
    const MutationConfig baseline = makeBudgetedBaseline();
    EvolutionConfig evolutionConfig;
    evolutionConfig.recoveryWindowGenerations = 3;

    const EffectiveAdaptiveMutation previous{
        .mode = AdaptiveMutationMode::Rescue,
        .mutationConfig =
            MutationConfig{
                .perturbationsPerOffspring = 500,
                .resetsPerOffspring = 4,
                .sigma = 0.08,
            },
    };

    const EffectiveAdaptiveMutation earlyRecovery = adaptiveMutationResolve(
        baseline,
        TrainingPhaseStatus{
            .recoveryLevel = 3,
            .phase = TrainingPhase::Recovery,
        },
        previous,
        evolutionConfig);
    const EffectiveAdaptiveMutation lateRecovery = adaptiveMutationResolve(
        baseline,
        TrainingPhaseStatus{
            .recoveryLevel = 1,
            .phase = TrainingPhase::Recovery,
        },
        earlyRecovery,
        evolutionConfig);

    EXPECT_EQ(earlyRecovery.mode, AdaptiveMutationMode::Recover);
    EXPECT_GT(
        earlyRecovery.mutationConfig.perturbationsPerOffspring, baseline.perturbationsPerOffspring);
    EXPECT_LT(
        earlyRecovery.mutationConfig.perturbationsPerOffspring,
        previous.mutationConfig.perturbationsPerOffspring);
    EXPECT_GT(earlyRecovery.mutationConfig.sigma, baseline.sigma);
    EXPECT_LT(earlyRecovery.mutationConfig.sigma, previous.mutationConfig.sigma);

    EXPECT_EQ(lateRecovery.mode, AdaptiveMutationMode::Recover);
    EXPECT_EQ(
        lateRecovery.mutationConfig.perturbationsPerOffspring, baseline.perturbationsPerOffspring);
    EXPECT_EQ(lateRecovery.mutationConfig.resetsPerOffspring, baseline.resetsPerOffspring);
    EXPECT_DOUBLE_EQ(lateRecovery.mutationConfig.sigma, baseline.sigma);
}

TEST(AdaptiveMutationTest, NormalPhaseKeepsBaselineSettings)
{
    const MutationConfig baseline = makeBudgetedBaseline();
    const EffectiveAdaptiveMutation previous{
        .mode = AdaptiveMutationMode::Rescue,
        .mutationConfig =
            MutationConfig{
                .perturbationsPerOffspring = 500,
                .resetsPerOffspring = 4,
                .sigma = 0.08,
            },
    };

    const EffectiveAdaptiveMutation effective = adaptiveMutationResolve(
        baseline,
        TrainingPhaseStatus{
            .phase = TrainingPhase::Normal,
        },
        previous,
        EvolutionConfig{});

    EXPECT_EQ(effective.mode, AdaptiveMutationMode::Baseline);
    EXPECT_EQ(
        effective.mutationConfig.perturbationsPerOffspring, baseline.perturbationsPerOffspring);
    EXPECT_EQ(effective.mutationConfig.resetsPerOffspring, baseline.resetsPerOffspring);
    EXPECT_DOUBLE_EQ(effective.mutationConfig.sigma, baseline.sigma);
}

TEST(AdaptiveMutationTest, ForcedBaselineIgnoresStuckPhase)
{
    const MutationConfig baseline = makeBudgetedBaseline();
    const EffectiveAdaptiveMutation previous{
        .mode = AdaptiveMutationMode::Rescue,
        .mutationConfig =
            MutationConfig{
                .perturbationsPerOffspring = 500,
                .resetsPerOffspring = 4,
                .sigma = 0.08,
            },
    };

    const EffectiveAdaptiveMutation effective = adaptiveMutationResolve(
        baseline,
        TrainingPhaseStatus{ .phase = TrainingPhase::Stuck },
        previous,
        EvolutionConfig{},
        AdaptiveMutationControlMode::Baseline);

    EXPECT_EQ(effective.mode, AdaptiveMutationMode::Baseline);
    EXPECT_EQ(
        effective.mutationConfig.perturbationsPerOffspring, baseline.perturbationsPerOffspring);
    EXPECT_EQ(effective.mutationConfig.resetsPerOffspring, baseline.resetsPerOffspring);
    EXPECT_DOUBLE_EQ(effective.mutationConfig.sigma, baseline.sigma);
}

TEST(AdaptiveMutationTest, ForcedRescueOverridesNormalPhase)
{
    const MutationConfig baseline = makeBudgetedBaseline();
    const EffectiveAdaptiveMutation previous{
        .mode = AdaptiveMutationMode::Baseline,
        .mutationConfig = baseline,
    };

    const EffectiveAdaptiveMutation effective = adaptiveMutationResolve(
        baseline,
        TrainingPhaseStatus{ .phase = TrainingPhase::Normal },
        previous,
        EvolutionConfig{},
        AdaptiveMutationControlMode::Rescue);

    EXPECT_EQ(effective.mode, AdaptiveMutationMode::Rescue);
    EXPECT_GT(
        effective.mutationConfig.perturbationsPerOffspring, baseline.perturbationsPerOffspring);
    EXPECT_GT(effective.mutationConfig.resetsPerOffspring, baseline.resetsPerOffspring);
    EXPECT_GT(effective.mutationConfig.sigma, baseline.sigma);
}
