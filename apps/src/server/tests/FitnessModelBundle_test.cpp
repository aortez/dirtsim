#include "core/organisms/evolution/NesEvaluator.h"
#include "server/evolution/FitnessEvaluation.h"
#include "server/evolution/FitnessModelBundle.h"
#include <array>
#include <gtest/gtest.h>
#include <string>

using namespace DirtSim;
using namespace DirtSim::Server::EvolutionSupport;

namespace {

FitnessEvaluation makeDuckEvaluation(double totalFitness, double wingUpSeconds, double exitDoorRaw)
{
    return FitnessEvaluation{
        .totalFitness = totalFitness,
        .details =
            DuckFitnessBreakdown{
                .wingUpSeconds = wingUpSeconds,
                .exitedThroughDoor = exitDoorRaw >= 0.5,
                .exitDoorRaw = exitDoorRaw,
                .exitDoorBonus = exitDoorRaw >= 0.5 ? 0.5 : 0.0,
                .totalFitness = totalFitness,
            },
    };
}

} // namespace

TEST(FitnessModelBundleTest, TreeBundleFormatsSummariesAndKeepsIdentityMerge)
{
    const FitnessModelBundle bundle =
        fitnessModelResolve(OrganismType::TREE, Scenario::EnumType::TreeGermination);

    ASSERT_NE(bundle.evaluate, nullptr);
    ASSERT_NE(bundle.mergePasses, nullptr);
    ASSERT_NE(bundle.formatLogSummary, nullptr);
    ASSERT_NE(bundle.generatePresentation, nullptr);

    const FitnessEvaluation evaluation{
        .totalFitness = 4.5,
        .details =
            TreeFitnessBreakdown{
                .survivalScore = 0.8,
                .energyScore = 0.7,
                .resourceScore = 0.6,
                .partialStructureBonus = 0.1,
                .stageBonus = 0.2,
                .structureBonus = 0.3,
                .milestoneBonus = 0.4,
                .commandScore = 0.5,
                .totalFitness = 4.5,
            },
    };
    const std::array<FitnessEvaluation, 1> evaluations{ evaluation };

    const FitnessEvaluation merged = bundle.mergePasses(evaluations);
    EXPECT_DOUBLE_EQ(merged.totalFitness, evaluation.totalFitness);

    const std::string summary = bundle.formatLogSummary(evaluation);
    EXPECT_NE(summary.find("surv=0.800"), std::string::npos);
    EXPECT_NE(summary.find("energy=0.700"), std::string::npos);
    EXPECT_NE(summary.find("cmd=0.500"), std::string::npos);

    const Api::FitnessPresentation presentation = bundle.generatePresentation(evaluation);
    EXPECT_EQ(presentation.modelId, "tree");
    ASSERT_FALSE(presentation.sections.empty());
}

TEST(FitnessModelBundleTest, DuckClockBundleMergesSelectedSideDetails)
{
    const FitnessModelBundle bundle =
        fitnessModelResolve(OrganismType::DUCK, Scenario::EnumType::Clock);

    ASSERT_NE(bundle.mergePasses, nullptr);

    const std::array<FitnessEvaluation, 4> evaluations{
        makeDuckEvaluation(1.0, 2.0, 0.0),
        makeDuckEvaluation(5.0, 10.0, 1.0),
        makeDuckEvaluation(1.4, 4.0, 0.0),
        makeDuckEvaluation(4.6, 12.0, 1.0),
    };

    const FitnessEvaluation merged = bundle.mergePasses(evaluations);
    const DuckFitnessBreakdown* breakdown = fitnessEvaluationDuckBreakdownGet(merged);
    ASSERT_NE(breakdown, nullptr);
    EXPECT_DOUBLE_EQ(merged.totalFitness, 1.2);
    EXPECT_DOUBLE_EQ(breakdown->wingUpSeconds, 3.0);
    EXPECT_DOUBLE_EQ(breakdown->exitDoorRaw, 0.0);
    EXPECT_DOUBLE_EQ(breakdown->exitDoorTime, 0.0);
    EXPECT_FALSE(breakdown->exitedThroughDoor);
    EXPECT_DOUBLE_EQ(breakdown->totalFitness, 1.2);

    const Api::FitnessPresentation presentation = bundle.generatePresentation(merged);
    EXPECT_EQ(presentation.modelId, "duck");
    ASSERT_FALSE(presentation.sections.empty());
}

TEST(FitnessModelBundleTest, NesDuckBundleEvaluatesRewardTotals)
{
    const FitnessModelBundle bundle =
        fitnessModelResolve(OrganismType::NES_DUCK, Scenario::EnumType::NesFlappyParatroopa);

    ASSERT_NE(bundle.evaluate, nullptr);

    const FitnessResult result{
        .nesRewardTotal = 12.75,
    };
    const EvolutionConfig evolutionConfig{};
    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::NES_DUCK,
        .worldWidth = 0,
        .worldHeight = 0,
        .evolutionConfig = evolutionConfig,
    };

    const FitnessEvaluation evaluation = bundle.evaluate(context);
    EXPECT_DOUBLE_EQ(
        evaluation.totalFitness, NesEvaluator::evaluateFromRewardTotal(result.nesRewardTotal));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(evaluation.details));
}

TEST(FitnessModelBundleTest, NesSuperMarioBrosBundleKeepsNativeBreakdown)
{
    const FitnessModelBundle bundle =
        fitnessModelResolve(OrganismType::NES_DUCK, Scenario::EnumType::NesSuperMarioBros);

    ASSERT_NE(bundle.evaluate, nullptr);
    ASSERT_NE(bundle.generatePresentation, nullptr);
    ASSERT_NE(bundle.formatLogSummary, nullptr);

    const FitnessResult result{
        .nesRewardTotal = 144.0,
    };
    const EvolutionConfig evolutionConfig{};
    const NesSuperMarioBrosFitnessSnapshot snapshot{
        .totalReward = 144.0,
        .distanceRewardTotal = 44.0,
        .levelClearRewardTotal = 100.0,
        .gameplayFrames = 1200,
        .framesSinceProgress = 300,
        .noProgressTimeoutFrames = 1800,
        .bestStageIndex = 2,
        .bestWorld = 0,
        .bestLevel = 2,
        .bestAbsoluteX = 388,
        .currentWorld = 0,
        .currentLevel = 2,
        .currentAbsoluteX = 388,
        .currentLives = 2,
        .endReason = SmbEpisodeEndReason::None,
        .done = false,
    };
    const NesFitnessDetails details = snapshot;
    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::NES_DUCK,
        .worldWidth = 0,
        .worldHeight = 0,
        .evolutionConfig = evolutionConfig,
        .nesFitnessDetails = &details,
    };

    const FitnessEvaluation evaluation = bundle.evaluate(context);
    EXPECT_DOUBLE_EQ(evaluation.totalFitness, 144.0);
    const auto* nativeBreakdown = fitnessEvaluationNesSuperMarioBrosBreakdownGet(evaluation);
    ASSERT_NE(nativeBreakdown, nullptr);
    EXPECT_EQ(nativeBreakdown->bestStageIndex, 2u);
    EXPECT_EQ(nativeBreakdown->bestAbsoluteX, 388u);

    const std::string summary = bundle.formatLogSummary(evaluation);
    EXPECT_NE(summary.find("reward=144.000"), std::string::npos);
    EXPECT_NE(summary.find("stage=2"), std::string::npos);

    const Api::FitnessPresentation presentation = bundle.generatePresentation(evaluation);
    EXPECT_EQ(presentation.modelId, "nes_smb");
    ASSERT_FALSE(presentation.sections.empty());
}
