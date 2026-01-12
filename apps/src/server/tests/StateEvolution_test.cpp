#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "server/StateMachine.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/states/Evolution.h"
#include "server/states/Idle.h"
#include "server/states/Shutdown.h"
#include "server/states/State.h"
#include <gtest/gtest.h>

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
    void SetUp() override { stateMachine = std::make_unique<StateMachine>(); }

    void TearDown() override { stateMachine.reset(); }

    std::unique_ptr<StateMachine> stateMachine;
};

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
    EXPECT_EQ(evolution.scenarioId, Scenario::EnumType::TreeGermination);

    // Verify: Response callback was invoked.
    ASSERT_TRUE(callbackInvoked) << "Response callback should be invoked";
    ASSERT_TRUE(capturedResponse.isValue()) << "Response should be success";
    EXPECT_TRUE(capturedResponse.value().started) << "Response should indicate started";
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

/**
 * @brief Test that evolution completes and transitions to Idle.
 */
TEST_F(StateEvolutionTest, CompletesAllGenerationsAndTransitionsToIdle)
{
    // Setup: Create Evolution state with minimal run.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;

    // Initialize the state.
    evolutionState.onEnter(*stateMachine);

    // Execute: Tick through all evaluations.
    std::optional<Any> result;

    // Generation 0, eval 0.
    result = evolutionState.tick(*stateMachine);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(evolutionState.generation, 1);

    // Generation 1, eval 0.
    result = evolutionState.tick(*stateMachine);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(evolutionState.generation, 2);

    // Generation 2 == maxGenerations, should complete.
    result = evolutionState.tick(*stateMachine);
    ASSERT_TRUE(result.has_value()) << "Should return state transition";
    ASSERT_TRUE(std::holds_alternative<Idle>(result->getVariant()))
        << "Should transition to Idle on completion";
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
    evolutionState.scenarioId = Scenario::EnumType::TreeGermination;

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
 * @brief Test that Exit command from Evolution transitions to Shutdown.
 */
TEST_F(StateEvolutionTest, ExitCommandTransitionsToShutdown)
{
    // Setup: Create Evolution state.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 10;
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
