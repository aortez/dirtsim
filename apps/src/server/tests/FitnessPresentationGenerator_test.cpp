#include "server/evolution/FitnessPresentationGenerator.h"
#include <cmath>
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
    const double obstacleCompetenceScore = 1.0 - std::exp(-1.0);
    const FitnessEvaluation evaluation{
        .totalFitness = 831.6060279414279,
        .details =
            DuckFitnessBreakdown{
                .survivalRaw = 50.0,
                .survivalReference = 100.0,
                .survivalScore = 0.5,
                .energyAverage = 0.75,
                .energyConsumedTotal = 12.0,
                .energyLimitedSeconds = 1.5,
                .wingUpSeconds = 3.0,
                .wingDownSeconds = 4.0,
                .collisionDamageTotal = 1.25,
                .damageTotal = 2.5,
                .fullTraversals = 2.0,
                .traversalProgress = 2.5,
                .traversalRatePer100Seconds = 2.5,
                .traversalPoints = 1250.0,
                .hurdleClears = 2.0,
                .hurdleOpportunities = 2.0,
                .pitClears = 1.0,
                .pitOpportunities = 1.0,
                .obstacleClears = 3.0,
                .obstacleOpportunities = 3.0,
                .obstacleClearRatePer100Seconds = 3.0,
                .obstacleClearRatePoints = 300.0,
                .obstacleCompetenceScore = obstacleCompetenceScore,
                .obstacleCompetencePoints = 100.0 * obstacleCompetenceScore,
                .coursePoints = 1550.0 + (100.0 * obstacleCompetenceScore),
                .exitDoorDistanceObserved = true,
                .exitedThroughDoor = false,
                .bestExitDoorDistanceCells = 5.0,
                .exitDoorProximityScore = 0.5,
                .exitDoorProximityPoints = 50.0,
                .exitDoorTime = 23.0,
                .healthAverage = 0.9,
                .exitDoorCompletionPoints = 0.0,
                .survivalAdjustedPoints = 831.6060279414279,
                .totalFitness = 831.6060279414279,
            },
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationDuckPresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::DUCK);
    EXPECT_EQ(presentation.modelId, "duck");
    EXPECT_DOUBLE_EQ(presentation.totalFitness, 831.6060279414279);
    EXPECT_EQ(
        presentation.summary,
        "Survival 0.5000 | Traversal 2.5000/100s | Clears 3.0000/100s | Exit no");
    ASSERT_EQ(presentation.sections.size(), 4u);
    EXPECT_EQ(presentation.sections[0].key, "survival");
    EXPECT_EQ(presentation.sections[1].key, "clock_course");
    EXPECT_EQ(presentation.sections[2].key, "clock_exit");
    EXPECT_EQ(presentation.sections[3].key, "condition");

    const Api::FitnessPresentationMetric* lifespan =
        fitnessMetricFind(presentation.sections[0], "lifespan");
    ASSERT_NE(lifespan, nullptr);
    EXPECT_DOUBLE_EQ(lifespan->value, 50.0);
    ASSERT_TRUE(lifespan->reference.has_value());
    EXPECT_DOUBLE_EQ(lifespan->reference.value(), 100.0);
    ASSERT_TRUE(lifespan->normalized.has_value());
    EXPECT_DOUBLE_EQ(lifespan->normalized.value(), 0.5);

    const Api::FitnessPresentationMetric* traversalProgress =
        fitnessMetricFind(presentation.sections[1], "traversal_progress");
    ASSERT_NE(traversalProgress, nullptr);
    EXPECT_DOUBLE_EQ(traversalProgress->value, 2.5);

    const Api::FitnessPresentationMetric* obstacleCompetence =
        fitnessMetricFind(presentation.sections[1], "obstacle_competence_score");
    ASSERT_NE(obstacleCompetence, nullptr);
    EXPECT_NEAR(obstacleCompetence->value, obstacleCompetenceScore, 1e-9);
    ASSERT_TRUE(obstacleCompetence->normalized.has_value());
    EXPECT_NEAR(obstacleCompetence->normalized.value(), obstacleCompetenceScore, 1e-9);

    const Api::FitnessPresentationMetric* exitDistance =
        fitnessMetricFind(presentation.sections[2], "best_exit_door_distance_cells");
    ASSERT_NE(exitDistance, nullptr);
    EXPECT_DOUBLE_EQ(exitDistance->value, 5.0);

    const Api::FitnessPresentationMetric* collisionDamage =
        fitnessMetricFind(presentation.sections[3], "collision_damage_total");
    ASSERT_NE(collisionDamage, nullptr);
    EXPECT_DOUBLE_EQ(collisionDamage->value, 1.25);
}

TEST(FitnessPresentationGeneratorTest, BuildsDuckClockPresentationFromNativeBreakdown)
{
    const double obstacleCompetenceScore = 1.0 - std::exp(-2.0 / 3.0);
    const FitnessEvaluation evaluation{
        .totalFitness = 1161.4937163594232,
        .details =
            DuckFitnessBreakdown{
                .survivalRaw = 75.0,
                .survivalReference = 100.0,
                .survivalScore = 0.75,
                .energyAverage = 0.65,
                .energyConsumedTotal = 14.0,
                .energyLimitedSeconds = 2.5,
                .wingUpSeconds = 4.0,
                .wingDownSeconds = 6.0,
                .collisionDamageTotal = 0.75,
                .damageTotal = 1.5,
                .fullTraversals = 2.0,
                .traversalProgress = 2.0,
                .traversalRatePer100Seconds = 2.0,
                .traversalPoints = 1000.0,
                .hurdleClears = 1.0,
                .hurdleOpportunities = 1.0,
                .leftWallTouches = 2.0,
                .pitClears = 1.0,
                .pitOpportunities = 1.0,
                .obstacleClears = 2.0,
                .obstacleOpportunities = 2.0,
                .obstacleClearRatePer100Seconds = 2.0,
                .obstacleClearRatePoints = 200.0,
                .obstacleCompetenceScore = obstacleCompetenceScore,
                .obstacleCompetencePoints = 100.0 * obstacleCompetenceScore,
                .rightWallTouches = 3.0,
                .coursePoints = 1200.0 + (100.0 * obstacleCompetenceScore),
                .exitDoorDistanceObserved = true,
                .exitedThroughDoor = true,
                .bestExitDoorDistanceCells = 0.0,
                .exitDoorProximityScore = 1.0,
                .exitDoorProximityPoints = 100.0,
                .exitDoorTime = 27.5,
                .healthAverage = 0.8,
                .exitDoorCompletionPoints = 150.0,
                .survivalAdjustedPoints = 1011.4937163594232,
                .totalFitness = 1161.4937163594232,
            },
    };

    const Api::FitnessPresentation presentation =
        fitnessEvaluationDuckClockPresentationGenerate(evaluation);
    EXPECT_EQ(presentation.organismType, OrganismType::DUCK);
    EXPECT_EQ(presentation.modelId, "duck");
    EXPECT_EQ(
        presentation.summary,
        "Survival 0.7500 | Traversal 2.0000/100s | Clears 2.0000/100s | Exit yes");
    ASSERT_EQ(presentation.sections.size(), 4u);

    const Api::FitnessPresentationSection* clockCourse =
        fitnessSectionFind(presentation, "clock_course");
    ASSERT_NE(clockCourse, nullptr);
    ASSERT_TRUE(clockCourse->score.has_value());
    EXPECT_NEAR(clockCourse->score.value(), 1200.0 + (100.0 * obstacleCompetenceScore), 1e-9);

    const Api::FitnessPresentationMetric* fullTraversals =
        fitnessMetricFind(*clockCourse, "full_traversals");
    ASSERT_NE(fullTraversals, nullptr);
    EXPECT_DOUBLE_EQ(fullTraversals->value, 2.0);

    const Api::FitnessPresentationMetric* obstacleRatePoints =
        fitnessMetricFind(*clockCourse, "obstacle_clear_rate_points");
    ASSERT_NE(obstacleRatePoints, nullptr);
    EXPECT_DOUBLE_EQ(obstacleRatePoints->value, 200.0);

    const Api::FitnessPresentationSection* clockExit =
        fitnessSectionFind(presentation, "clock_exit");
    ASSERT_NE(clockExit, nullptr);
    ASSERT_TRUE(clockExit->score.has_value());
    EXPECT_DOUBLE_EQ(clockExit->score.value(), 250.0);

    const Api::FitnessPresentationMetric* exitDoorTime =
        fitnessMetricFind(*clockExit, "exit_door_time");
    ASSERT_NE(exitDoorTime, nullptr);
    EXPECT_DOUBLE_EQ(exitDoorTime->value, 27.5);

    const Api::FitnessPresentationMetric* exitDoorCompletion =
        fitnessMetricFind(*clockExit, "exit_door_completion_points");
    ASSERT_NE(exitDoorCompletion, nullptr);
    EXPECT_DOUBLE_EQ(exitDoorCompletion->value, 150.0);

    const Api::FitnessPresentationMetric* exitDoorDistance =
        fitnessMetricFind(*clockExit, "best_exit_door_distance_cells");
    ASSERT_NE(exitDoorDistance, nullptr);
    EXPECT_DOUBLE_EQ(exitDoorDistance->value, 0.0);
    ASSERT_TRUE(exitDoorDistance->reference.has_value());
    EXPECT_DOUBLE_EQ(exitDoorDistance->reference.value(), 10.0);
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
