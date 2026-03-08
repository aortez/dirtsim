#include "server/api/TrainingBestSnapshot.h"
#include "server/tests/TestStateMachineFixture.h"
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::Tests;

TEST(TrainingBestSnapshotCacheTest, TrainingBestSnapshotCacheRoundTrips)
{
    TestStateMachineFixture fixture;

    EXPECT_FALSE(fixture.stateMachine->getCachedTrainingBestSnapshot().has_value());

    Api::TrainingBestSnapshot snapshot;
    snapshot.fitness = 2.5;
    snapshot.generation = 3;
    snapshot.commandsAccepted = 4;
    snapshot.commandsRejected = 5;
    snapshot.topCommandSignatures.push_back(
        Api::TrainingBestSnapshot::CommandSignatureCount{
            .signature = "GrowRoot(+0,+1)",
            .count = 7,
        });
    snapshot.topCommandOutcomeSignatures.push_back(
        Api::TrainingBestSnapshot::CommandSignatureCount{
            .signature = "GrowRoot(+0,+1) -> INVALID_TARGET",
            .count = 6,
        });

    Api::FitnessBreakdownReport breakdown;
    breakdown.organismType = OrganismType::DUCK;
    breakdown.modelId = "duck_v2";
    breakdown.modelVersion = 1;
    breakdown.totalFitness = 1.9;
    breakdown.totalFormula = "survival * (1 + movement)";
    breakdown.metrics.push_back(
        Api::FitnessMetric{
            .key = "survival",
            .label = "Survival",
            .group = "survival",
            .raw = 20.0,
            .normalized = 1.0,
            .reference = 20.0,
            .weight = std::nullopt,
            .contribution = std::nullopt,
            .unit = "seconds",
        });
    snapshot.fitnessBreakdown = std::move(breakdown);

    fixture.stateMachine->updateCachedTrainingBestSnapshot(snapshot);

    const auto cached = fixture.stateMachine->getCachedTrainingBestSnapshot();
    ASSERT_TRUE(cached.has_value());
    EXPECT_DOUBLE_EQ(cached->fitness, 2.5);
    EXPECT_EQ(cached->generation, 3);
    EXPECT_EQ(cached->commandsAccepted, 4);
    EXPECT_EQ(cached->commandsRejected, 5);
    ASSERT_EQ(cached->topCommandSignatures.size(), 1u);
    EXPECT_EQ(cached->topCommandSignatures[0].signature, "GrowRoot(+0,+1)");
    EXPECT_EQ(cached->topCommandSignatures[0].count, 7);
    ASSERT_EQ(cached->topCommandOutcomeSignatures.size(), 1u);
    EXPECT_EQ(
        cached->topCommandOutcomeSignatures[0].signature, "GrowRoot(+0,+1) -> INVALID_TARGET");
    EXPECT_EQ(cached->topCommandOutcomeSignatures[0].count, 6);
    ASSERT_TRUE(cached->fitnessBreakdown.has_value());
    EXPECT_EQ(cached->fitnessBreakdown->modelId, "duck_v2");
    EXPECT_EQ(cached->fitnessBreakdown->modelVersion, 1);
    ASSERT_EQ(cached->fitnessBreakdown->metrics.size(), 1u);
    EXPECT_EQ(cached->fitnessBreakdown->metrics[0].key, "survival");
    EXPECT_DOUBLE_EQ(cached->fitnessBreakdown->metrics[0].normalized, 1.0);

    fixture.stateMachine->clearCachedTrainingBestSnapshot();
    EXPECT_FALSE(fixture.stateMachine->getCachedTrainingBestSnapshot().has_value());
}
