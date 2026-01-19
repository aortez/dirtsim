#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/StateMachine.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/states/Evolution.h"
#include "server/states/Idle.h"
#include "server/states/Shutdown.h"
#include "server/states/State.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::State;

/**
 * @brief Test fixture for Evolution state tests.
 *
 * Provides common setup: a StateMachine instance for state context.
 */
class StateEvolutionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        testDataDir_ = std::filesystem::temp_directory_path() / "dirtsim-test";
        stateMachine = std::make_unique<StateMachine>(testDataDir_);
    }

    void TearDown() override
    {
        stateMachine.reset();
        std::filesystem::remove_all(testDataDir_);
    }

    std::filesystem::path testDataDir_;

    std::unique_ptr<StateMachine> stateMachine;
};

namespace {

TrainingSpec makeTrainingSpec(int populationSize)
{
    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = populationSize;
    population.randomCount = populationSize;

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;
    spec.population.push_back(population);

    return spec;
}

} // namespace

/**
 * @brief Test that EvolutionStart command transitions Idle to Evolution.
 */
TEST_F(StateEvolutionTest, EvolutionStartTransitionsIdleToEvolution)
{
    // Setup: Create Idle state.
    Idle idleState;

    // Setup: Create EvolutionStart command with callback.
    bool callbackInvoked = false;
    Api::EvolutionStart::Response capturedResponse;

    Api::EvolutionStart::Command cmd;
    cmd.evolution.populationSize = 2;
    cmd.evolution.maxGenerations = 1;
    cmd.evolution.maxSimulationTime = 0.1; // Very short for testing.
    cmd.scenarioId = Scenario::EnumType::TreeGermination;

    Api::EvolutionStart::Cwc cwc(cmd, [&](Api::EvolutionStart::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    // Execute: Send EvolutionStart command to Idle state.
    State::Any newState = idleState.onEvent(cwc, *stateMachine);

    // Verify: State transitioned to Evolution.
    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()))
        << "Idle + EvolutionStart should transition to Evolution";

    // Verify: Evolution state has correct config.
    Evolution& evolution = std::get<Evolution>(newState.getVariant());
    EXPECT_EQ(evolution.evolutionConfig.populationSize, 2);
    EXPECT_EQ(evolution.evolutionConfig.maxGenerations, 1);
    EXPECT_EQ(evolution.trainingSpec.scenarioId, Scenario::EnumType::TreeGermination);
    EXPECT_EQ(evolution.trainingSpec.organismType, OrganismType::TREE);

    // Verify: Response callback was invoked.
    ASSERT_TRUE(callbackInvoked) << "Response callback should be invoked";
    ASSERT_TRUE(capturedResponse.isValue()) << "Response should be success";
    EXPECT_TRUE(capturedResponse.value().started) << "Response should indicate started";
}

TEST_F(StateEvolutionTest, EvolutionStartMissingGenomeIdTriggersDeath)
{
    Idle idleState;

    Api::EvolutionStart::Command cmd;
    cmd.evolution.populationSize = 1;
    cmd.evolution.maxGenerations = 1;
    cmd.scenarioId = Scenario::EnumType::TreeGermination;
    cmd.organismType = OrganismType::TREE;

    PopulationSpec spec;
    spec.brainKind = TrainingBrainKind::NeuralNet;
    spec.count = 1;
    spec.seedGenomes.push_back(INVALID_GENOME_ID);
    cmd.population.push_back(spec);

    Api::EvolutionStart::Cwc cwc(cmd, [&](Api::EvolutionStart::Response&& /*response*/) {});

    EXPECT_DEATH({ idleState.onEvent(cwc, *stateMachine); }, ".*");
}

TEST_F(StateEvolutionTest, MissingBrainKindTriggersDeath)
{
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;

    PopulationSpec spec;
    spec.brainKind = "MissingBrain";
    spec.count = 1;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    evolutionState.trainingSpec.organismType = OrganismType::TREE;
    evolutionState.trainingSpec.population.push_back(spec);

    EXPECT_DEATH({ evolutionState.onEnter(*stateMachine); }, ".*");
}

/**
 * @brief Test that EvolutionStop command transitions Evolution to Idle.
 */
TEST_F(StateEvolutionTest, EvolutionStopTransitionsEvolutionToIdle)
{
    // Setup: Create Evolution state with minimal config.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 10;
    evolutionState.evolutionConfig.maxSimulationTime = 0.1;
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize the state (populates population).
    evolutionState.onEnter(*stateMachine);

    // Setup: Create EvolutionStop command with callback.
    bool callbackInvoked = false;
    Api::EvolutionStop::Response capturedResponse;

    Api::EvolutionStop::Command cmd;
    Api::EvolutionStop::Cwc cwc(cmd, [&](Api::EvolutionStop::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    // Execute: Send EvolutionStop command.
    State::Any newState = evolutionState.onEvent(cwc, *stateMachine);

    // Verify: State transitioned to Idle.
    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()))
        << "Evolution + EvolutionStop should transition to Idle";

    // Verify: Response callback was invoked.
    ASSERT_TRUE(callbackInvoked) << "Response callback should be invoked";
    ASSERT_TRUE(capturedResponse.isValue()) << "Response should be success";
}

/**
 * @brief Test that tick() evaluates organisms and advances through population.
 */
TEST_F(StateEvolutionTest, TickEvaluatesOrganismsAndAdvancesGeneration)
{
    // Setup: Create Evolution state with tiny population.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 10;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016; // Single frame.
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize the state.
    evolutionState.onEnter(*stateMachine);

    // Verify initial state.
    EXPECT_EQ(evolutionState.generation, 0);
    EXPECT_EQ(evolutionState.currentEval, 0);
    EXPECT_EQ(evolutionState.population.size(), 2u);

    // Execute: First tick evaluates first organism.
    auto result1 = evolutionState.tick(*stateMachine);
    EXPECT_FALSE(result1.has_value()) << "Should stay in Evolution";
    EXPECT_EQ(evolutionState.currentEval, 1) << "Should advance to next organism";

    // Execute: Second tick completes generation.
    auto result2 = evolutionState.tick(*stateMachine);
    EXPECT_FALSE(result2.has_value()) << "Should stay in Evolution";
    EXPECT_EQ(evolutionState.generation, 1) << "Should advance to next generation";
    EXPECT_EQ(evolutionState.currentEval, 0) << "Should reset eval counter";
}

TEST_F(StateEvolutionTest, NonNeuralBrainsCloneAcrossGeneration)
{
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::RuleBased;
    population.count = 2;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    evolutionState.trainingSpec.organismType = OrganismType::TREE;
    evolutionState.trainingSpec.population.push_back(population);

    evolutionState.onEnter(*stateMachine);

    evolutionState.tick(*stateMachine);
    evolutionState.tick(*stateMachine);

    EXPECT_EQ(evolutionState.generation, 1);
    for (const auto& individual : evolutionState.population) {
        EXPECT_EQ(individual.brainKind, TrainingBrainKind::RuleBased);
        EXPECT_FALSE(individual.genome.has_value());
    }
}

/**
 * @brief Test that evolution completes and transitions after training result delivery.
 */
TEST_F(StateEvolutionTest, CompletesAllGenerationsAndTransitionsAfterTrainingResult)
{
    // Setup: Create Evolution state with minimal run.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;
    evolutionState.trainingSpec = makeTrainingSpec(1);

    // Initialize the state.
    evolutionState.onEnter(*stateMachine);

    // Execute: Tick until transition occurs.
    std::optional<Any> result;
    constexpr int maxTicks = 10;
    for (int i = 0; i < maxTicks && !result.has_value(); ++i) {
        result = evolutionState.tick(*stateMachine);
    }

    ASSERT_TRUE(result.has_value()) << "Should transition after training result delivery";
    ASSERT_TRUE(std::holds_alternative<UnsavedTrainingResult>(result->getVariant()))
        << "Should transition to UnsavedTrainingResult";
    EXPECT_EQ(evolutionState.generation, 2);
}

/**
 * @brief Test that best genome is stored in repository.
 */
TEST_F(StateEvolutionTest, BestGenomeStoredInRepository)
{
    // Setup: Clear repository.
    auto& repo = stateMachine->getGenomeRepository();
    repo.clear();
    EXPECT_TRUE(repo.empty());

    // Setup: Create Evolution state.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize and run through one generation.
    evolutionState.onEnter(*stateMachine);

    // Tick through both organisms.
    evolutionState.tick(*stateMachine);
    evolutionState.tick(*stateMachine);

    // Verify: Repository should have at least one genome stored.
    EXPECT_FALSE(repo.empty()) << "Repository should have stored genome(s)";

    // Verify: Best genome should be marked.
    auto bestId = repo.getBestId();
    ASSERT_TRUE(bestId.has_value()) << "Best genome should be marked";

    // Verify: Can retrieve best genome.
    auto bestGenome = repo.getBest();
    ASSERT_TRUE(bestGenome.has_value()) << "Should be able to retrieve best genome";
    EXPECT_FALSE(bestGenome->weights.empty()) << "Genome should have weights";

    // Verify: Metadata is correct.
    auto metadata = repo.getMetadata(*bestId);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->scenarioId, Scenario::EnumType::TreeGermination);
    EXPECT_GE(metadata->fitness, 0.0) << "Fitness should be non-negative";
}

/**
 * @brief Test that tick() advances evaluation incrementally (non-blocking).
 *
 * With a longer simulation time, multiple ticks are needed per evaluation.
 * This verifies the non-blocking architecture where each tick does one physics step.
 */
TEST_F(StateEvolutionTest, TickAdvancesEvaluationIncrementally)
{
    // Setup: Create Evolution state with longer simulation time.
    // Use population=2 and maxGenerations=2 so we can observe currentEval advancing.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.1; // ~6 physics steps at 0.016s each.
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize the state.
    evolutionState.onEnter(*stateMachine);

    // Verify: No world exists yet.
    EXPECT_EQ(evolutionState.evalWorld_, nullptr);

    // Execute: First tick should create world and advance one step.
    auto result1 = evolutionState.tick(*stateMachine);
    EXPECT_FALSE(result1.has_value()) << "Should stay in Evolution";
    EXPECT_NE(evolutionState.evalWorld_, nullptr) << "World should exist mid-evaluation";
    EXPECT_EQ(evolutionState.currentEval, 0) << "Should still be on first organism";
    EXPECT_GT(evolutionState.evalSimTime_, 0.0) << "Sim time should have advanced";
    EXPECT_LT(evolutionState.evalSimTime_, 0.1) << "Sim time should not be complete";

    // Execute: Second tick should advance further but not complete.
    auto result2 = evolutionState.tick(*stateMachine);
    EXPECT_FALSE(result2.has_value()) << "Should stay in Evolution";
    EXPECT_NE(evolutionState.evalWorld_, nullptr) << "World should still exist";
    EXPECT_EQ(evolutionState.currentEval, 0) << "Should still be on first organism";

    // Execute: Tick until first evaluation completes.
    int tickCount = 2;
    while (evolutionState.currentEval == 0 && tickCount < 20) {
        evolutionState.tick(*stateMachine);
        tickCount++;
    }

    // Verify: First evaluation completed after multiple ticks.
    EXPECT_GT(tickCount, 2) << "Should require multiple ticks for evaluation";
    EXPECT_EQ(evolutionState.currentEval, 1) << "Should have advanced to second organism";
    EXPECT_EQ(evolutionState.evalWorld_, nullptr) << "World should be cleaned up between evals";
}

/**
 * @brief Test that EvolutionStop can be processed mid-evaluation.
 *
 * This is the key test for responsive event handling - verifies that stop
 * events don't have to wait for a full evaluation to complete.
 */
TEST_F(StateEvolutionTest, StopCommandProcessedMidEvaluation)
{
    // Setup: Create Evolution state with long simulation time.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 10;
    evolutionState.evolutionConfig.maxSimulationTime = 1.0; // Very long - would be ~62 ticks.
    evolutionState.trainingSpec = makeTrainingSpec(1);

    // Initialize and tick once to start evaluation.
    evolutionState.onEnter(*stateMachine);
    evolutionState.tick(*stateMachine);

    // Verify: Evaluation is in progress.
    EXPECT_NE(evolutionState.evalWorld_, nullptr) << "World should exist mid-evaluation";
    EXPECT_LT(evolutionState.evalSimTime_, 0.5) << "Should be early in evaluation";

    // Setup: Create EvolutionStop command.
    bool callbackInvoked = false;
    Api::EvolutionStop::Command cmd;
    Api::EvolutionStop::Cwc cwc(cmd, [&](Api::EvolutionStop::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    // Execute: Send stop command mid-evaluation.
    State::Any newState = evolutionState.onEvent(cwc, *stateMachine);

    // Verify: Stop was processed immediately (non-blocking).
    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()))
        << "Should transition to Idle immediately";
    EXPECT_TRUE(callbackInvoked) << "Response callback should be invoked";
}

/**
 * @brief Integration test: run a full training cycle and verify outputs.
 *
 * Runs 3 generations with population of 3, verifying:
 * - All generations complete
 * - Fitness scores are tracked
 * - Best genome is stored in repository
 * - Repository contains expected data
 */
TEST_F(StateEvolutionTest, FullTrainingCycleProducesValidOutputs)
{
    // Setup: Clear repository for clean test.
    auto& repo = stateMachine->getGenomeRepository();
    repo.clear();

    // Setup: Create Evolution state with small but complete config.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 3;
    evolutionState.evolutionConfig.maxGenerations = 3;
    evolutionState.evolutionConfig.maxSimulationTime = 1.0; // 1 second per organism.
    evolutionState.trainingSpec = makeTrainingSpec(3);

    // Initialize.
    evolutionState.onEnter(*stateMachine);
    EXPECT_EQ(evolutionState.population.size(), 3u);
    EXPECT_EQ(evolutionState.generation, 0);

    // Run until evolution completes.
    int tickCount = 0;
    constexpr int MAX_TICKS = 10000; // Safety limit.
    std::optional<Any> finalState;

    while (tickCount < MAX_TICKS && !finalState.has_value()) {
        finalState = evolutionState.tick(*stateMachine);
        tickCount++;
    }

    // Verify: Evolution completed.
    const bool completed =
        evolutionState.generation >= evolutionState.evolutionConfig.maxGenerations
        && evolutionState.currentEval >= evolutionState.evolutionConfig.populationSize;
    ASSERT_TRUE(completed) << "Evolution should complete within tick limit";
    ASSERT_TRUE(finalState.has_value()) << "Should transition after training result delivery";
    ASSERT_TRUE(std::holds_alternative<UnsavedTrainingResult>(finalState->getVariant()))
        << "Should transition to UnsavedTrainingResult";

    // Verify: Ran through all generations.
    EXPECT_EQ(evolutionState.generation, 3) << "Should have completed 3 generations";

    // Verify: Best fitness was tracked.
    EXPECT_GT(evolutionState.bestFitnessAllTime, 0.0)
        << "Best fitness should be positive (tree survives some time)";

    // Verify: Repository has stored genomes.
    EXPECT_FALSE(repo.empty()) << "Repository should have stored genomes";

    // Verify: Best genome is marked.
    auto bestId = repo.getBestId();
    ASSERT_TRUE(bestId.has_value()) << "Best genome should be marked";

    // Verify: Can retrieve best genome with valid data.
    auto bestGenome = repo.getBest();
    ASSERT_TRUE(bestGenome.has_value()) << "Should retrieve best genome";
    EXPECT_FALSE(bestGenome->weights.empty()) << "Genome should have weights";

    // Verify: Metadata is correct.
    auto metadata = repo.getMetadata(*bestId);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->scenarioId, Scenario::EnumType::TreeGermination);
    EXPECT_GT(metadata->fitness, 0.0) << "Best fitness should be positive";
    EXPECT_EQ(metadata->fitness, evolutionState.bestFitnessAllTime)
        << "Stored fitness should match tracked best";
}

/**
 * @brief Test that Exit command from Evolution transitions to Shutdown.
 */
TEST_F(StateEvolutionTest, ExitCommandTransitionsToShutdown)
{
    // Setup: Create Evolution state.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 10;
    evolutionState.trainingSpec = makeTrainingSpec(2);
    evolutionState.onEnter(*stateMachine);

    // Setup: Create Exit command.
    bool callbackInvoked = false;
    Api::Exit::Command cmd;
    Api::Exit::Cwc cwc(cmd, [&](Api::Exit::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    // Execute: Send Exit command.
    State::Any newState = evolutionState.onEvent(cwc, *stateMachine);

    // Verify: State transitioned to Shutdown.
    ASSERT_TRUE(std::holds_alternative<Shutdown>(newState.getVariant()))
        << "Evolution + Exit should transition to Shutdown";
    EXPECT_TRUE(callbackInvoked);
}
