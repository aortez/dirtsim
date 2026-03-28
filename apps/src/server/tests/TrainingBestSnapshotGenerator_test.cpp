#include "server/evolution/TrainingBestSnapshotGenerator.h"
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Server::EvolutionSupport;

TEST(TrainingBestSnapshotGeneratorTest, BuildsSnapshotFromDomainEvaluation)
{
    WorldData worldData;
    worldData.width = 2;
    worldData.height = 1;
    worldData.cells.resize(2);
    worldData.organism_ids = { OrganismId{ 1 }, OrganismId{ 2 } };

    std::vector<OrganismId> organismIds{ OrganismId{ 3 }, OrganismId{ 4 } };
    ScenarioVideoFrame videoFrame;
    videoFrame.width = 16;
    videoFrame.height = 8;
    videoFrame.frame_id = 42;
    videoFrame.pixels = { std::byte{ 0x12 }, std::byte{ 0x34 } };

    const FitnessEvaluation evaluation{
        .totalFitness = 3.5,
        .details =
            DuckFitnessBreakdown{
                .wingUpSeconds = 2.0,
                .traversalRatePer100Seconds = 1.0,
                .obstacleClearRatePer100Seconds = 0.5,
                .exitedThroughDoor = true,
                .exitDoorProximityScore = 1.0,
                .exitDoorProximityPoints = 100.0,
                .exitDoorCompletionPoints = 150.0,
                .totalFitness = 3.5,
            },
    };
    const FitnessModelBundle fitnessModel =
        fitnessModelResolve(OrganismType::DUCK, Scenario::EnumType::Clock);

    const Api::TrainingBestSnapshot snapshot = trainingBestSnapshotBuild(
        std::move(worldData),
        std::move(organismIds),
        evaluation,
        fitnessModel,
        7,
        11,
        3,
        { { "GrowLeaf", 5 } },
        { { "GrowLeaf -> SUCCESS", 4 } },
        videoFrame);

    EXPECT_DOUBLE_EQ(snapshot.fitness, 3.5);
    EXPECT_EQ(snapshot.generation, 7);
    EXPECT_EQ(snapshot.commandsAccepted, 11);
    EXPECT_EQ(snapshot.commandsRejected, 3);
    EXPECT_EQ(snapshot.worldData.width, 2);
    EXPECT_EQ(snapshot.worldData.height, 1);
    ASSERT_EQ(snapshot.organismIds.size(), 2u);
    EXPECT_EQ(snapshot.organismIds[0], OrganismId{ 3 });
    EXPECT_EQ(snapshot.organismIds[1], OrganismId{ 4 });
    ASSERT_EQ(snapshot.topCommandSignatures.size(), 1u);
    EXPECT_EQ(snapshot.topCommandSignatures[0].signature, "GrowLeaf");
    EXPECT_EQ(snapshot.topCommandSignatures[0].count, 5);
    ASSERT_EQ(snapshot.topCommandOutcomeSignatures.size(), 1u);
    EXPECT_EQ(snapshot.topCommandOutcomeSignatures[0].signature, "GrowLeaf -> SUCCESS");
    EXPECT_EQ(snapshot.topCommandOutcomeSignatures[0].count, 4);
    ASSERT_TRUE(snapshot.scenarioVideoFrame.has_value());
    EXPECT_EQ(snapshot.scenarioVideoFrame->frame_id, 42u);
    EXPECT_EQ(snapshot.scenarioVideoFrame->pixels, videoFrame.pixels);
    EXPECT_EQ(snapshot.fitnessPresentation.modelId, "duck");
    EXPECT_DOUBLE_EQ(snapshot.fitnessPresentation.totalFitness, 3.5);
}

TEST(TrainingBestSnapshotGeneratorTest, BuildsFallbackPresentationWhenDetailsAreMissing)
{
    WorldData worldData;
    worldData.width = 1;
    worldData.height = 1;
    worldData.cells.resize(1);
    const FitnessModelBundle fitnessModel =
        fitnessModelResolve(OrganismType::NES_DUCK, Scenario::EnumType::NesFlappyParatroopa);

    const Api::TrainingBestSnapshot snapshot = trainingBestSnapshotBuild(
        std::move(worldData),
        { OrganismId{ 1 } },
        FitnessEvaluation{
            .totalFitness = 1.25,
            .details = std::monostate{},
        },
        fitnessModel,
        2,
        1,
        0,
        {},
        {},
        std::nullopt);

    EXPECT_DOUBLE_EQ(snapshot.fitness, 1.25);
    EXPECT_EQ(snapshot.fitnessPresentation.modelId, "nes_duck");
    EXPECT_DOUBLE_EQ(snapshot.fitnessPresentation.totalFitness, 1.25);
}
