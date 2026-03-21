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
    snapshot.fitnessPresentation = Api::FitnessPresentation{
        .organismType = OrganismType::DUCK,
        .modelId = "duck",
        .totalFitness = 1.9,
        .summary = "Duck overview.",
        .sections =
            {
                Api::FitnessPresentationSection{
                    .key = "overview",
                    .label = "Overview",
                    .score = std::nullopt,
                    .metrics =
                        {
                            Api::FitnessPresentationMetric{
                                .key = "survival",
                                .label = "Survival",
                                .value = 20.0,
                                .reference = 20.0,
                                .normalized = 1.0,
                                .unit = "seconds",
                            },
                        },
                },
            },
    };

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
    EXPECT_EQ(cached->fitnessPresentation.modelId, "duck");
    ASSERT_EQ(cached->fitnessPresentation.sections.size(), 1u);
    ASSERT_EQ(cached->fitnessPresentation.sections[0].metrics.size(), 1u);
    EXPECT_EQ(cached->fitnessPresentation.sections[0].metrics[0].key, "survival");
    ASSERT_TRUE(cached->fitnessPresentation.sections[0].metrics[0].normalized.has_value());
    EXPECT_DOUBLE_EQ(cached->fitnessPresentation.sections[0].metrics[0].normalized.value(), 1.0);

    fixture.stateMachine->clearCachedTrainingBestSnapshot();
    EXPECT_FALSE(fixture.stateMachine->getCachedTrainingBestSnapshot().has_value());
}
