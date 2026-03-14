#include "server/evolution/FitnessPresentationGenerator.h"
#include <gtest/gtest.h>
#include <string_view>

using namespace DirtSim;
using namespace DirtSim::Server::EvolutionSupport;

namespace {

const Api::FitnessPresentationMetric* fitnessMetricFind(
    const Api::FitnessPresentationSection& section, std::string_view key)
{
    for (const auto& metric : section.metrics) {
        if (metric.key == key) {
            return &metric;
        }
    }
    return nullptr;
}

const Api::FitnessPresentationSection* fitnessSectionFind(
    const Api::FitnessPresentation& presentation, std::string_view key)
{
    for (const auto& section : presentation.sections) {
        if (section.key == key) {
            return &section;
        }
    }
    return nullptr;
}

} // namespace

TEST(FitnessPresentationGeneratorTest, BuildsDuckPresentationFromNativeBreakdown)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 2.75,
        .details =
            DuckFitnessBreakdown{
                .survivalRaw = 20.0,
                .survivalReference = 40.0,
                .survivalScore = 0.5,
                .energyAverage = 0.75,
                .energyConsumedTotal = 12.0,
                .energyLimitedSeconds = 1.5,
                .wingUpSeconds = 3.0,
                .wingDownSeconds = 4.0,
                .movementRaw = 0.3,
                .movementScore = 0.3,
                .effortRaw = 0.25,
                .effortReference = 1.0,
                .effortScore = 0.25,
                .effortPenaltyRaw = 0.05,
                .effortPenaltyScore = 0.05,
                .coverageColumnRaw = 8.0,
                .coverageColumnReference = 10.0,
                .coverageScore = 0.6,
                .coverageColumnScore = 0.8,
                .coverageRowRaw = 3.0,
                .coverageRowReference = 5.0,
                .coverageRowScore = 0.6,
                .coverageCellRaw = 12.0,
                .coverageCellReference = 20.0,
                .coverageCellScore = 0.6,
                .collisionDamageTotal = 1.25,
                .damageTotal = 2.5,
                .exitedThroughDoor = true,
                .exitDoorRaw = 1.0,
                .exitDoorTime = 23.0,
                .healthAverage = 0.9,
                .exitDoorBonus = 0.5,
                .totalFitness = 2.75,
            },
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationDuckPresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::DUCK);
    EXPECT_EQ(presentation.modelId, "duck");
    EXPECT_DOUBLE_EQ(presentation.totalFitness, 2.75);
    EXPECT_EQ(presentation.summary, "Survival 0.5000 | Movement 0.3000 | Coverage 0.6000");
    ASSERT_EQ(presentation.sections.size(), 5u);
    EXPECT_EQ(presentation.sections[0].key, "survival");
    EXPECT_EQ(presentation.sections[1].key, "movement");
    EXPECT_EQ(presentation.sections[2].key, "coverage");
    EXPECT_EQ(presentation.sections[3].key, "effort");
    EXPECT_EQ(presentation.sections[4].key, "condition");

    const Api::FitnessPresentationMetric* lifespan =
        fitnessMetricFind(presentation.sections[0], "lifespan");
    ASSERT_NE(lifespan, nullptr);
    EXPECT_DOUBLE_EQ(lifespan->value, 20.0);
    ASSERT_TRUE(lifespan->reference.has_value());
    EXPECT_DOUBLE_EQ(lifespan->reference.value(), 40.0);
    ASSERT_TRUE(lifespan->normalized.has_value());
    EXPECT_DOUBLE_EQ(lifespan->normalized.value(), 0.5);

    const Api::FitnessPresentationMetric* coverageColumns =
        fitnessMetricFind(presentation.sections[2], "coverage_columns");
    ASSERT_NE(coverageColumns, nullptr);
    EXPECT_DOUBLE_EQ(coverageColumns->value, 8.0);
    ASSERT_TRUE(coverageColumns->reference.has_value());
    EXPECT_DOUBLE_EQ(coverageColumns->reference.value(), 10.0);
    ASSERT_TRUE(coverageColumns->normalized.has_value());
    EXPECT_DOUBLE_EQ(coverageColumns->normalized.value(), 0.8);

    const Api::FitnessPresentationMetric* collisionDamage =
        fitnessMetricFind(presentation.sections[4], "collision_damage_total");
    ASSERT_NE(collisionDamage, nullptr);
    EXPECT_DOUBLE_EQ(collisionDamage->value, 1.25);
}

TEST(FitnessPresentationGeneratorTest, BuildsDuckClockPresentationFromNativeBreakdown)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 1.85,
        .details =
            DuckFitnessBreakdown{
                .survivalRaw = 30.0,
                .survivalReference = 40.0,
                .survivalScore = 0.75,
                .energyAverage = 0.65,
                .energyConsumedTotal = 14.0,
                .energyLimitedSeconds = 2.5,
                .wingUpSeconds = 4.0,
                .wingDownSeconds = 6.0,
                .movementRaw = 0.8,
                .movementScore = 0.8,
                .effortRaw = 0.35,
                .effortReference = 1.0,
                .effortScore = 0.35,
                .effortPenaltyRaw = 0.05,
                .effortPenaltyScore = 0.05,
                .coverageColumnRaw = 10.0,
                .coverageColumnReference = 12.0,
                .coverageScore = 0.85,
                .coverageColumnScore = 0.8333,
                .coverageRowRaw = 5.0,
                .coverageRowReference = 8.0,
                .coverageRowScore = 0.625,
                .coverageCellRaw = 16.0,
                .coverageCellReference = 24.0,
                .coverageCellScore = 0.6667,
                .collisionDamageTotal = 0.75,
                .damageTotal = 1.5,
                .exitedThroughDoor = true,
                .exitDoorRaw = 1.0,
                .exitDoorTime = 27.5,
                .healthAverage = 0.8,
                .exitDoorBonus = 0.5,
                .totalFitness = 1.85,
            },
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationDuckClockPresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::DUCK);
    EXPECT_EQ(presentation.modelId, "duck");
    EXPECT_EQ(
        presentation.summary, "Survival 0.7500 | Movement 0.8000 | Coverage 0.8500 | Exit yes");
    ASSERT_EQ(presentation.sections.size(), 6u);

    const Api::FitnessPresentationSection* clockExit =
        fitnessSectionFind(presentation, "clock_exit");
    ASSERT_NE(clockExit, nullptr);
    ASSERT_TRUE(clockExit->score.has_value());
    EXPECT_DOUBLE_EQ(clockExit->score.value(), 0.5);

    const Api::FitnessPresentationMetric* exitDoorTime =
        fitnessMetricFind(*clockExit, "exit_door_time");
    ASSERT_NE(exitDoorTime, nullptr);
    EXPECT_DOUBLE_EQ(exitDoorTime->value, 27.5);
}

TEST(FitnessPresentationGeneratorTest, BuildsNesSuperMarioBrosPresentationFromBreakdown)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 123.5,
        .details =
            NesSuperMarioBrosFitnessBreakdown{
                .totalFitness = 123.5,
                .distanceRewardTotal = 23.5,
                .levelClearRewardTotal = 100.0,
                .gameplayFrames = 800,
                .framesSinceProgress = 120,
                .noProgressTimeoutFrames = 1800,
                .bestStageIndex = 5,
                .bestWorld = 1,
                .bestLevel = 1,
                .bestAbsoluteX = 342,
                .currentWorld = 1,
                .currentLevel = 1,
                .currentAbsoluteX = 330,
                .currentLives = 2,
                .endReason = SmbEpisodeEndReason::NoProgressTimeout,
                .done = true,
            },
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationNesSuperMarioBrosPresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::NES_DUCK);
    EXPECT_EQ(presentation.modelId, "nes_smb");
    EXPECT_EQ(
        presentation.summary,
        "Reward 123.5000 | Best Stage 5 | Best X 342 | Gameplay 800 frames | Since Progress "
        "120 frames | End no_progress_timeout");
    ASSERT_EQ(presentation.sections.size(), 3u);
    EXPECT_EQ(presentation.sections[0].key, "reward");
    EXPECT_EQ(presentation.sections[1].key, "frontier");
    EXPECT_EQ(presentation.sections[2].key, "episode");

    const Api::FitnessPresentationMetric* distanceReward =
        fitnessMetricFind(presentation.sections[0], "distance_reward_total");
    ASSERT_NE(distanceReward, nullptr);
    EXPECT_DOUBLE_EQ(distanceReward->value, 23.5);

    const Api::FitnessPresentationMetric* framesSinceProgress =
        fitnessMetricFind(presentation.sections[2], "frames_since_progress");
    ASSERT_NE(framesSinceProgress, nullptr);
    EXPECT_DOUBLE_EQ(framesSinceProgress->value, 120.0);
    ASSERT_TRUE(framesSinceProgress->reference.has_value());
    EXPECT_DOUBLE_EQ(framesSinceProgress->reference.value(), 1800.0);
}

TEST(FitnessPresentationGeneratorTest, BuildsTreePresentationFromNativeBreakdown)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 6.395,
        .details =
            TreeFitnessBreakdown{
                .survivalRaw = 90.0,
                .survivalReference = 120.0,
                .survivalScore = 0.75,
                .maxEnergyRaw = 80.0,
                .maxEnergyNormalized = 0.8,
                .finalEnergyRaw = 80.0,
                .finalEnergyNormalized = 0.8,
                .energyReference = 100.0,
                .energyScore = 0.8,
                .producedEnergyRaw = 55.0,
                .producedEnergyNormalized = 0.8,
                .absorbedWaterRaw = 42.0,
                .absorbedWaterNormalized = 0.55,
                .waterReference = 80.0,
                .resourceScore = 0.7,
                .partialStructureBonus = 0.1,
                .stageBonus = 0.2,
                .structureBonus = 0.3,
                .milestoneBonus = 0.4,
                .commandScore = 0.5,
                .seedScore = 2.6,
                .totalFitness = 6.395,
                .coreFitness = 2.295,
                .bonusFitness = 4.1,
                .energyMaxWeightedComponent = 0.56,
                .energyFinalWeightedComponent = 0.24,
                .resourceEnergyWeightedComponent = 0.48,
                .resourceWaterWeightedComponent = 0.22,
                .rootBelowSeedBonus = 0.1,
                .woodAboveSeedBonus = 0.3,
                .commandsAccepted = 11,
                .commandsRejected = 3,
                .idleCancels = 2,
                .leafCount = 5,
                .rootCount = 2,
                .woodCount = 4,
                .partialStructurePartCount = 3,
                .seedCountBonus = 2.0,
                .seedDistanceBonus = 0.6,
                .seedDistanceReference = 10.0,
                .seedsProduced = 7,
                .landedSeedCount = 2,
                .averageLandedSeedDistance = 6.5,
                .maxLandedSeedDistance = 9.0,
            },
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationTreePresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::TREE);
    EXPECT_EQ(presentation.modelId, "tree");
    EXPECT_EQ(
        presentation.summary,
        "Core 2.2950 = Survival 0.7500 x (1 + Energy 0.8000) x (1 + Resources 0.7000)\n"
        "Bonus 4.1000 = Structure 1.0000 + Commands/Seed 3.1000");
    ASSERT_EQ(presentation.sections.size(), 5u);
    EXPECT_EQ(presentation.sections[0].key, "survival");
    EXPECT_EQ(presentation.sections[1].key, "energy");
    EXPECT_EQ(presentation.sections[2].key, "resources");
    EXPECT_EQ(presentation.sections[3].key, "structure");
    EXPECT_EQ(presentation.sections[4].key, "commands_seed");
    EXPECT_EQ(presentation.sections[0].label, "Survival Factor");
    EXPECT_EQ(presentation.sections[3].label, "Structure Bonuses");

    const Api::FitnessPresentationMetric* energyMax =
        fitnessMetricFind(presentation.sections[1], "energy_max");
    ASSERT_NE(energyMax, nullptr);
    EXPECT_DOUBLE_EQ(energyMax->value, 80.0);
    ASSERT_TRUE(energyMax->reference.has_value());
    EXPECT_DOUBLE_EQ(energyMax->reference.value(), 100.0);
    ASSERT_TRUE(energyMax->normalized.has_value());
    EXPECT_DOUBLE_EQ(energyMax->normalized.value(), 0.8);

    const Api::FitnessPresentationMetric* energyMaxWeighted =
        fitnessMetricFind(presentation.sections[1], "energy_max_weighted");
    ASSERT_NE(energyMaxWeighted, nullptr);
    EXPECT_DOUBLE_EQ(energyMaxWeighted->value, 0.56);

    const Api::FitnessPresentationMetric* waterAbsorbed =
        fitnessMetricFind(presentation.sections[2], "water_absorbed");
    ASSERT_NE(waterAbsorbed, nullptr);
    EXPECT_DOUBLE_EQ(waterAbsorbed->value, 42.0);
    ASSERT_TRUE(waterAbsorbed->reference.has_value());
    EXPECT_DOUBLE_EQ(waterAbsorbed->reference.value(), 80.0);
    ASSERT_TRUE(waterAbsorbed->normalized.has_value());
    EXPECT_DOUBLE_EQ(waterAbsorbed->normalized.value(), 0.55);

    const Api::FitnessPresentationMetric* partialStructureParts =
        fitnessMetricFind(presentation.sections[3], "partial_structure_parts");
    ASSERT_NE(partialStructureParts, nullptr);
    EXPECT_DOUBLE_EQ(partialStructureParts->value, 3.0);

    const Api::FitnessPresentationMetric* commandsAccepted =
        fitnessMetricFind(presentation.sections[4], "commands_accepted");
    ASSERT_NE(commandsAccepted, nullptr);
    EXPECT_DOUBLE_EQ(commandsAccepted->value, 11.0);

    const Api::FitnessPresentationMetric* seedDistance =
        fitnessMetricFind(presentation.sections[4], "max_seed_distance");
    ASSERT_NE(seedDistance, nullptr);
    EXPECT_DOUBLE_EQ(seedDistance->value, 9.0);
    ASSERT_TRUE(seedDistance->reference.has_value());
    EXPECT_DOUBLE_EQ(seedDistance->reference.value(), 10.0);
}

TEST(FitnessPresentationGeneratorTest, FallsBackWhenTreeDetailsAreMissing)
{
    const FitnessEvaluation evaluation{
        .totalFitness = 1.25,
        .details = std::monostate{},
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationTreePresentationGenerate(evaluation);
    EXPECT_EQ(presentation.modelId, "tree");
    EXPECT_EQ(presentation.totalFitness, 1.25);
    ASSERT_EQ(presentation.sections.size(), 1u);
    EXPECT_EQ(presentation.sections[0].key, "overview");
}
