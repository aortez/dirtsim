#include "core/ScenarioId.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingRunner.h"
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
    TrainingRunner runner(
        Genome::random(rng_), Scenario::EnumType::TreeGermination, config_, genomeRepository_);

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
    TrainingRunner runner(
        Genome::random(rng_), Scenario::EnumType::TreeGermination, config_, genomeRepository_);

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
    EXPECT_GE(status.maxEnergy, 0.0);
}

} // namespace DirtSim
