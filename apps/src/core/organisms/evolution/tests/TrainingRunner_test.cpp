#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingRunner.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include <gtest/gtest.h>
#include <random>

namespace DirtSim {

class TrainingRunnerTest : public ::testing::Test {
protected:
    void SetUp() override { rng_.seed(42); }

    std::mt19937 rng_;
    EvolutionConfig config_;
    GenomeRepository genomeRepository_;
};

// Proves the core design - that we can step incrementally without blocking.
TEST_F(TrainingRunnerTest, StepIsIncrementalNotBlocking)
{
    config_.maxSimulationTime = 1.0;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = Genome::random(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);

    // Step once - should return quickly, still running.
    auto status1 = runner.step(1);
    EXPECT_EQ(status1.state, TrainingRunner::State::Running);
    EXPECT_NEAR(runner.getSimTime(), 0.016, 0.001);

    // Step again - time accumulates, still running.
    auto status2 = runner.step(1);
    EXPECT_EQ(status2.state, TrainingRunner::State::Running);
    EXPECT_NEAR(runner.getSimTime(), 0.032, 0.001);

    // World exists and is accessible between steps.
    EXPECT_NE(runner.getWorld(), nullptr);
}

// Proves we can finish and get results.
TEST_F(TrainingRunnerTest, CompletionReturnsFitnessResults)
{
    config_.maxSimulationTime = 0.048; // 3 frames - quick completion.

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = Genome::random(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);

    // Step until complete.
    TrainingRunner::Status status;
    int steps = 0;
    while ((status = runner.step(1)).state == TrainingRunner::State::Running) {
        steps++;
        ASSERT_LT(steps, 100) << "Should complete within reasonable steps";
    }

    // Verify completion state.
    EXPECT_EQ(status.state, TrainingRunner::State::TimeExpired);

    // Verify fitness metrics are populated.
    EXPECT_NEAR(status.lifespan, config_.maxSimulationTime, 0.02);
    EXPECT_GE(status.distanceTraveled, 0.0);
    EXPECT_GE(status.maxEnergy, 0.0);
}

TEST_F(TrainingRunnerTest, SpawnPrefersNearestAirInTopHalf)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = Genome::random(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);
    World* world = runner.getWorld();
    ASSERT_NE(world, nullptr);

    auto& data = world->getData();
    const int centerX = data.width / 2;
    const int centerY = data.height / 2;
    const int width = data.width;
    const int height = data.height;

    for (int y = 0; y <= centerY; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }
    for (int y = centerY + 1; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }

    const int expectedX = centerX - 1;
    const int expectedY = centerY - 1;
    const int fartherX = centerX - 3;
    const int fartherY = centerY;
    const int bottomX = centerX;
    const int bottomY = centerY + 1;

    data.at(expectedX, expectedY).clear();
    data.at(fartherX, fartherY).clear();
    data.at(bottomX, bottomY).clear();

    runner.step(0);

    EXPECT_TRUE(world->getOrganismManager().hasOrganism({ expectedX, expectedY }));
    EXPECT_FALSE(world->getOrganismManager().hasOrganism({ bottomX, bottomY }));
}

TEST_F(TrainingRunnerTest, SpawnFallsBackToBottomHalfWhenTopHalfIsFull)
{
    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;

    TrainingRunner::Individual individual;
    individual.brain.brainKind = TrainingBrainKind::NeuralNet;
    individual.genome = Genome::random(rng_);

    TrainingRunner runner(spec, individual, config_, genomeRepository_);
    World* world = runner.getWorld();
    ASSERT_NE(world, nullptr);

    auto& data = world->getData();
    const int centerX = data.width / 2;
    const int centerY = data.height / 2;
    const int width = data.width;
    const int height = data.height;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0f);
        }
    }

    const int bottomX = centerX + 1;
    const int bottomY = centerY + 1;
    data.at(bottomX, bottomY).clear();

    runner.step(0);

    EXPECT_TRUE(world->getOrganismManager().hasOrganism({ bottomX, bottomY }));
}

} // namespace DirtSim
