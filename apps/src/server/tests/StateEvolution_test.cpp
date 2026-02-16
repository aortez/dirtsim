#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/api/TrainingBestSnapshot.h"
#include "server/states/Evolution.h"
#include "server/states/Idle.h"
#include "server/states/Shutdown.h"
#include "server/states/State.h"
#include "server/tests/TestStateMachineFixture.h"
#include <gtest/gtest.h>
#include <optional>
#include <vector>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::State;
using namespace DirtSim::Server::Tests;

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

struct EvolutionWorkerGuard {
    Evolution* evolution = nullptr;
    StateMachine* stateMachine = nullptr;

    ~EvolutionWorkerGuard()
    {
        if (evolution && stateMachine) {
            evolution->onExit(*stateMachine);
        }
    }
};

} // namespace

/**
 * @brief Test that EvolutionStart command transitions Idle to Evolution.
 */
TEST(StateEvolutionTest, EvolutionStartTransitionsIdleToEvolution)
{
    TestStateMachineFixture fixture;

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
    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

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

TEST(StateEvolutionTest, TrainingBestSnapshotCacheRoundTrips)
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

    fixture.stateMachine->clearCachedTrainingBestSnapshot();
    EXPECT_FALSE(fixture.stateMachine->getCachedTrainingBestSnapshot().has_value());
}

TEST(StateEvolutionTest, EvolutionStartMissingGenomeIdReturnsError)
{
    TestStateMachineFixture fixture;

    Idle idleState;

    Api::EvolutionStart::Command cmd;
    cmd.evolution.populationSize = 1;
    cmd.evolution.maxGenerations = 1;
    cmd.scenarioId = Scenario::EnumType::TreeGermination;
    cmd.organismType = OrganismType::TREE;

    PopulationSpec spec;
    spec.brainKind = TrainingBrainKind::NeuralNet;
    spec.count = 1;
    const GenomeId missingGenomeId = UUID::generate();
    spec.seedGenomes.push_back(missingGenomeId);
    cmd.population.push_back(spec);

    bool callbackInvoked = false;
    Api::EvolutionStart::Response response;
    Api::EvolutionStart::Cwc cwc(cmd, [&](Api::EvolutionStart::Response&& result) {
        callbackInvoked = true;
        response = std::move(result);
    });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isError());
    EXPECT_EQ(
        response.errorValue().message, "Seed genome not found: " + missingGenomeId.toShortString());
    EXPECT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
}

TEST(StateEvolutionTest, EvolutionStartWarmResumeInjectsBestGenomeSeed)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const GenomeId bestId = UUID::generate();
    const Genome bestGenome = Genome::constant(0.25);
    const GenomeMetadata bestMetadata{
        .name = "warm-best",
        .fitness = 9.0,
        .generation = 7,
        .createdTimestamp = 1234567890,
        .scenarioId = Scenario::EnumType::TreeGermination,
        .notes = "",
        .organismType = OrganismType::TREE,
        .brainKind = TrainingBrainKind::NeuralNet,
        .brainVariant = std::nullopt,
        .trainingSessionId = std::nullopt,
    };
    repo.store(bestId, bestGenome, bestMetadata);
    repo.markAsBest(bestId);

    Idle idleState;

    Api::EvolutionStart::Command cmd;
    cmd.resumePolicy = TrainingResumePolicy::WarmFromBest;
    cmd.evolution.populationSize = 4;
    cmd.evolution.maxGenerations = 1;
    cmd.scenarioId = Scenario::EnumType::TreeGermination;
    cmd.organismType = OrganismType::TREE;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 4;
    population.randomCount = 4;
    cmd.population.push_back(population);

    Api::EvolutionStart::Response response;
    Api::EvolutionStart::Cwc cwc(
        cmd, [&](Api::EvolutionStart::Response&& result) { response = std::move(result); });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(response.isValue());
    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()));

    const auto& evolution = std::get<Evolution>(newState.getVariant());
    ASSERT_EQ(evolution.trainingSpec.population.size(), 1u);
    const auto& spec = evolution.trainingSpec.population.front();
    ASSERT_EQ(spec.seedGenomes.size(), 1u);
    EXPECT_EQ(spec.seedGenomes.front(), bestId);
    EXPECT_EQ(spec.count, 4);
    EXPECT_EQ(spec.randomCount, 3);
}

TEST(StateEvolutionTest, EvolutionStartFreshResumeDoesNotInjectBestGenomeSeed)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const GenomeId bestId = UUID::generate();
    const Genome bestGenome = Genome::constant(0.5);
    const GenomeMetadata bestMetadata{
        .name = "fresh-best",
        .fitness = 4.0,
        .generation = 3,
        .createdTimestamp = 1234567890,
        .scenarioId = Scenario::EnumType::TreeGermination,
        .notes = "",
        .organismType = OrganismType::TREE,
        .brainKind = TrainingBrainKind::NeuralNet,
        .brainVariant = std::nullopt,
        .trainingSessionId = std::nullopt,
    };
    repo.store(bestId, bestGenome, bestMetadata);
    repo.markAsBest(bestId);

    Idle idleState;

    Api::EvolutionStart::Command cmd;
    cmd.resumePolicy = TrainingResumePolicy::Fresh;
    cmd.evolution.populationSize = 4;
    cmd.evolution.maxGenerations = 1;
    cmd.scenarioId = Scenario::EnumType::TreeGermination;
    cmd.organismType = OrganismType::TREE;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 4;
    population.randomCount = 4;
    cmd.population.push_back(population);

    Api::EvolutionStart::Response response;
    Api::EvolutionStart::Cwc cwc(
        cmd, [&](Api::EvolutionStart::Response&& result) { response = std::move(result); });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(response.isValue());
    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()));

    const auto& evolution = std::get<Evolution>(newState.getVariant());
    ASSERT_EQ(evolution.trainingSpec.population.size(), 1u);
    const auto& spec = evolution.trainingSpec.population.front();
    EXPECT_TRUE(spec.seedGenomes.empty());
    EXPECT_EQ(spec.count, 4);
    EXPECT_EQ(spec.randomCount, 4);
}

TEST(StateEvolutionTest, MissingBrainKindTriggersDeath)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;

    PopulationSpec spec;
    spec.brainKind = "MissingBrain";
    spec.count = 1;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    evolutionState.trainingSpec.organismType = OrganismType::TREE;
    evolutionState.trainingSpec.population.push_back(spec);

    EXPECT_DEATH({ evolutionState.onEnter(*fixture.stateMachine); }, ".*");
}

/**
 * @brief Test that EvolutionStop command transitions Evolution to Idle.
 */
TEST(StateEvolutionTest, EvolutionStopTransitionsEvolutionToIdle)
{
    TestStateMachineFixture fixture;

    // Setup: Create Evolution state with minimal config.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 10;
    evolutionState.evolutionConfig.maxSimulationTime = 0.1;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize the state (populates population).
    evolutionState.onEnter(*fixture.stateMachine);

    // Setup: Create EvolutionStop command with callback.
    bool callbackInvoked = false;
    Api::EvolutionStop::Response capturedResponse;

    Api::EvolutionStop::Command cmd;
    Api::EvolutionStop::Cwc cwc(cmd, [&](Api::EvolutionStop::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    // Execute: Send EvolutionStop command.
    State::Any newState = evolutionState.onEvent(cwc, *fixture.stateMachine);

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
TEST(StateEvolutionTest, TickEvaluatesOrganismsAndAdvancesGeneration)
{
    TestStateMachineFixture fixture;

    // Setup: Create Evolution state with tiny population.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 10;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016; // Single frame.
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize the state.
    evolutionState.onEnter(*fixture.stateMachine);

    // Verify initial state.
    EXPECT_EQ(evolutionState.generation, 0);
    EXPECT_EQ(evolutionState.currentEval, 0);
    EXPECT_EQ(evolutionState.population.size(), 2u);

    // Execute: First tick evaluates first organism.
    auto result1 = evolutionState.tick(*fixture.stateMachine);
    EXPECT_FALSE(result1.has_value()) << "Should stay in Evolution";
    EXPECT_EQ(evolutionState.currentEval, 1) << "Should advance to next organism";

    // Execute: Second tick completes generation.
    auto result2 = evolutionState.tick(*fixture.stateMachine);
    EXPECT_FALSE(result2.has_value()) << "Should stay in Evolution";
    EXPECT_EQ(evolutionState.generation, 1) << "Should advance to next generation";
    EXPECT_EQ(evolutionState.currentEval, 0) << "Should reset eval counter";
}

TEST(StateEvolutionTest, NonNeuralBrainsCloneAcrossGeneration)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::RuleBased;
    population.count = 2;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    evolutionState.trainingSpec.organismType = OrganismType::TREE;
    evolutionState.trainingSpec.population.push_back(population);

    evolutionState.onEnter(*fixture.stateMachine);

    evolutionState.tick(*fixture.stateMachine);
    evolutionState.tick(*fixture.stateMachine);

    EXPECT_EQ(evolutionState.generation, 1);
    for (const auto& individual : evolutionState.population) {
        EXPECT_EQ(individual.brainKind, TrainingBrainKind::RuleBased);
        EXPECT_FALSE(individual.genome.has_value());
    }
}

TEST(StateEvolutionTest, NeuralNetNoMutationPreservesGenomesUnderTiedFitness)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.mutationConfig = MutationConfig{
        .rate = 0.0,
        .sigma = 0.5,
        .resetRate = 0.0,
    };
    evolutionState.trainingSpec = makeTrainingSpec(2);

    evolutionState.onEnter(*fixture.stateMachine);

    std::vector<Genome> parents;
    parents.reserve(evolutionState.population.size());
    for (const auto& individual : evolutionState.population) {
        ASSERT_TRUE(individual.genome.has_value());
        parents.push_back(individual.genome.value());
    }

    constexpr int maxTicks = 20;
    for (int i = 0; i < maxTicks && evolutionState.generation < 1; ++i) {
        auto result = evolutionState.tick(*fixture.stateMachine);
        ASSERT_FALSE(result.has_value()) << "Should stay in Evolution";
    }

    ASSERT_EQ(evolutionState.generation, 1);
    ASSERT_EQ(evolutionState.population.size(), parents.size());

    for (const auto& individual : evolutionState.population) {
        ASSERT_TRUE(individual.genome.has_value());
        const auto& genome = individual.genome.value();
        bool matchesParent = false;
        for (const auto& parent : parents) {
            if (genome.weights == parent.weights) {
                matchesParent = true;
                break;
            }
        }
        EXPECT_TRUE(matchesParent);
    }
}

TEST(StateEvolutionTest, TiedFitnessKeepsExistingBestGenomeId)
{
    TestStateMachineFixture fixture;

    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const Genome seedGenome = Genome::constant(0.1);
    const GenomeId seedId = UUID::generate();
    const GenomeMetadata seedMeta{
        .name = "seed",
        .fitness = 1.0,
        .generation = 0,
        .createdTimestamp = 1234567890,
        .scenarioId = Scenario::EnumType::TreeGermination,
        .notes = "",
        .organismType = OrganismType::TREE,
        .brainKind = TrainingBrainKind::NeuralNet,
        .brainVariant = std::nullopt,
        .trainingSessionId = std::nullopt,
    };
    repo.store(seedId, seedGenome, seedMeta);

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 2;
    population.seedGenomes = { seedId, seedId };
    population.randomCount = 0;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    evolutionState.trainingSpec.organismType = OrganismType::TREE;
    evolutionState.trainingSpec.population = { population };

    evolutionState.onEnter(*fixture.stateMachine);

    auto result1 = evolutionState.tick(*fixture.stateMachine);
    ASSERT_FALSE(result1.has_value());
    const GenomeId firstBestId = evolutionState.bestGenomeId;
    ASSERT_FALSE(firstBestId.isNil());

    auto result2 = evolutionState.tick(*fixture.stateMachine);
    ASSERT_TRUE(result2.has_value());

    ASSERT_EQ(evolutionState.fitnessScores.size(), 2u);
    EXPECT_DOUBLE_EQ(evolutionState.fitnessScores[0], evolutionState.fitnessScores[1]);
    EXPECT_EQ(evolutionState.bestGenomeId, firstBestId);
}

TEST(StateEvolutionTest, NeuralNetMutationSurvivesTiedFitness)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.mutationConfig = MutationConfig{
        .rate = 0.0,
        .sigma = 0.5,
        .resetRate = 1.0,
    };
    evolutionState.trainingSpec = makeTrainingSpec(2);

    evolutionState.onEnter(*fixture.stateMachine);

    std::vector<Genome> parents;
    parents.reserve(evolutionState.population.size());
    for (const auto& individual : evolutionState.population) {
        ASSERT_TRUE(individual.genome.has_value());
        parents.push_back(individual.genome.value());
    }

    evolutionState.rng.seed(123u);

    constexpr int maxTicks = 20;
    for (int i = 0; i < maxTicks && evolutionState.generation < 1; ++i) {
        auto result = evolutionState.tick(*fixture.stateMachine);
        ASSERT_FALSE(result.has_value()) << "Should stay in Evolution";
    }

    ASSERT_EQ(evolutionState.generation, 1);
    ASSERT_EQ(evolutionState.population.size(), parents.size());

    bool foundMutation = false;
    for (const auto& individual : evolutionState.population) {
        ASSERT_TRUE(individual.genome.has_value());
        const auto& genome = individual.genome.value();
        bool matchesParent = false;
        for (const auto& parent : parents) {
            if (genome.weights == parent.weights) {
                matchesParent = true;
                break;
            }
        }
        if (!matchesParent) {
            foundMutation = true;
            break;
        }
    }

    EXPECT_TRUE(foundMutation);
}

TEST(StateEvolutionTest, NeuralNetMutationNotSelectedWithPositiveFitness)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.048;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.mutationConfig = MutationConfig{
        .rate = 0.0,
        .sigma = 0.5,
        .resetRate = 1.0,
    };
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const Genome seedGenome = Genome::constant(0.1);
    const GenomeId seedId = UUID::generate();
    const GenomeMetadata seedMeta{
        .name = "seed",
        .fitness = 1.0,
        .generation = 0,
        .createdTimestamp = 1234567890,
        .scenarioId = Scenario::EnumType::TreeGermination,
        .notes = "",
        .organismType = OrganismType::TREE,
        .brainKind = TrainingBrainKind::NeuralNet,
        .brainVariant = std::nullopt,
        .trainingSessionId = std::nullopt,
    };
    repo.store(seedId, seedGenome, seedMeta);

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 2;
    population.seedGenomes = { seedId, seedId };
    population.randomCount = 0;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    evolutionState.trainingSpec.organismType = OrganismType::TREE;
    evolutionState.trainingSpec.population = { population };

    evolutionState.onEnter(*fixture.stateMachine);

    std::vector<Genome> parents;
    parents.reserve(evolutionState.population.size());
    for (const auto& individual : evolutionState.population) {
        ASSERT_TRUE(individual.genome.has_value());
        parents.push_back(individual.genome.value());
    }

    evolutionState.rng.seed(42u);

    constexpr int maxTicks = 40;
    for (int i = 0; i < maxTicks && evolutionState.generation < 1; ++i) {
        auto result = evolutionState.tick(*fixture.stateMachine);
        ASSERT_FALSE(result.has_value()) << "Should stay in Evolution";
    }

    ASSERT_EQ(evolutionState.generation, 1);
    ASSERT_EQ(evolutionState.population.size(), parents.size());

    for (const auto& individual : evolutionState.population) {
        ASSERT_TRUE(individual.genome.has_value());
        const auto& genome = individual.genome.value();
        bool matchesParent = false;
        for (const auto& parent : parents) {
            if (genome.weights == parent.weights) {
                matchesParent = true;
                break;
            }
        }
        EXPECT_TRUE(matchesParent);
    }
}

/**
 * @brief Test that evolution completes and transitions after training result delivery.
 */
TEST(StateEvolutionTest, CompletesAllGenerationsAndTransitionsAfterTrainingResult)
{
    TestStateMachineFixture fixture;

    // Setup: Create Evolution state with minimal run.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(1);

    // Initialize the state.
    evolutionState.onEnter(*fixture.stateMachine);

    // Execute: Tick until transition occurs.
    std::optional<Any> result;
    constexpr int maxTicks = 10;
    for (int i = 0; i < maxTicks && !result.has_value(); ++i) {
        result = evolutionState.tick(*fixture.stateMachine);
    }

    ASSERT_TRUE(result.has_value()) << "Should transition after training result delivery";
    ASSERT_TRUE(std::holds_alternative<UnsavedTrainingResult>(result->getVariant()))
        << "Should transition to UnsavedTrainingResult";
    EXPECT_EQ(evolutionState.generation, 2);
}

/**
 * @brief Test that best genome is stored in repository.
 */
TEST(StateEvolutionTest, BestGenomeStoredInRepository)
{
    TestStateMachineFixture fixture;

    // Setup: Clear repository.
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();
    EXPECT_TRUE(repo.empty());

    // Setup: Create Evolution state.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize and run through one generation.
    evolutionState.onEnter(*fixture.stateMachine);

    // Tick through both organisms.
    evolutionState.tick(*fixture.stateMachine);
    evolutionState.tick(*fixture.stateMachine);

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
TEST(StateEvolutionTest, TickAdvancesEvaluationIncrementally)
{
    TestStateMachineFixture fixture;

    // Setup: Create Evolution state with longer simulation time.
    // Use population=2 and maxGenerations=2 so we can observe currentEval advancing.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.1; // ~6 physics steps at 0.016s each.
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize the state.
    evolutionState.onEnter(*fixture.stateMachine);

    // Verify: No runner exists yet.
    EXPECT_EQ(evolutionState.visibleRunner_, nullptr);

    // Execute: First tick should create world and advance one step.
    auto result1 = evolutionState.tick(*fixture.stateMachine);
    EXPECT_FALSE(result1.has_value()) << "Should stay in Evolution";
    EXPECT_NE(evolutionState.visibleRunner_, nullptr) << "Runner should exist mid-evaluation";
    EXPECT_EQ(evolutionState.currentEval, 0) << "Should still be on first organism";
    ASSERT_NE(evolutionState.visibleRunner_, nullptr);
    EXPECT_GT(evolutionState.visibleRunner_->getSimTime(), 0.0) << "Sim time should have advanced";
    EXPECT_LT(evolutionState.visibleRunner_->getSimTime(), 0.1)
        << "Sim time should not be complete";

    // Execute: Second tick should advance further but not complete.
    auto result2 = evolutionState.tick(*fixture.stateMachine);
    EXPECT_FALSE(result2.has_value()) << "Should stay in Evolution";
    EXPECT_NE(evolutionState.visibleRunner_, nullptr) << "Runner should still exist";
    EXPECT_EQ(evolutionState.currentEval, 0) << "Should still be on first organism";

    // Execute: Tick until first evaluation completes.
    int tickCount = 2;
    while (evolutionState.currentEval == 0 && tickCount < 20) {
        evolutionState.tick(*fixture.stateMachine);
        tickCount++;
    }

    // Verify: First evaluation completed after multiple ticks.
    EXPECT_GT(tickCount, 2) << "Should require multiple ticks for evaluation";
    EXPECT_EQ(evolutionState.currentEval, 1) << "Should have advanced to second organism";
    EXPECT_EQ(evolutionState.visibleRunner_, nullptr)
        << "Runner should be cleaned up between evals";
}

/**
 * @brief Test that EvolutionStop can be processed mid-evaluation.
 *
 * This is the key test for responsive event handling - verifies that stop
 * events don't have to wait for a full evaluation to complete.
 */
TEST(StateEvolutionTest, StopCommandProcessedMidEvaluation)
{
    TestStateMachineFixture fixture;

    // Setup: Create Evolution state with long simulation time.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 10;
    evolutionState.evolutionConfig.maxSimulationTime = 1.0; // Very long - would be ~62 ticks.
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(1);

    // Initialize and tick once to start evaluation.
    evolutionState.onEnter(*fixture.stateMachine);
    evolutionState.tick(*fixture.stateMachine);

    // Verify: Evaluation is in progress.
    EXPECT_NE(evolutionState.visibleRunner_, nullptr) << "Runner should exist mid-evaluation";
    ASSERT_NE(evolutionState.visibleRunner_, nullptr);
    EXPECT_LT(evolutionState.visibleRunner_->getSimTime(), 0.5) << "Should be early in evaluation";

    // Setup: Create EvolutionStop command.
    bool callbackInvoked = false;
    Api::EvolutionStop::Command cmd;
    Api::EvolutionStop::Cwc cwc(cmd, [&](Api::EvolutionStop::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    // Execute: Send stop command mid-evaluation.
    State::Any newState = evolutionState.onEvent(cwc, *fixture.stateMachine);

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
TEST(StateEvolutionTest, FullTrainingCycleProducesValidOutputs)
{
    TestStateMachineFixture fixture;

    // Setup: Clear repository for clean test.
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    // Setup: Create Evolution state with small but complete config.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 3;
    evolutionState.evolutionConfig.maxGenerations = 3;
    evolutionState.evolutionConfig.maxSimulationTime = 1.0; // 1 second per organism.
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(3);

    // Initialize.
    evolutionState.onEnter(*fixture.stateMachine);
    EXPECT_EQ(evolutionState.population.size(), 3u);
    EXPECT_EQ(evolutionState.generation, 0);

    // Run until evolution completes.
    int tickCount = 0;
    constexpr int MAX_TICKS = 10000; // Safety limit.
    std::optional<Any> finalState;

    while (tickCount < MAX_TICKS && !finalState.has_value()) {
        finalState = evolutionState.tick(*fixture.stateMachine);
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

TEST(StateEvolutionTest, ParallelWorkersSplitVisibleAndBackgroundEvaluations)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    EvolutionWorkerGuard guard{ &evolutionState, fixture.stateMachine.get() };
    evolutionState.evolutionConfig.populationSize = 5;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;
    evolutionState.evolutionConfig.maxParallelEvaluations = 3;
    evolutionState.trainingSpec = makeTrainingSpec(5);

    evolutionState.onEnter(*fixture.stateMachine);

    ASSERT_NE(evolutionState.workerState_, nullptr);
    EXPECT_EQ(evolutionState.workerState_->backgroundWorkerCount, 2);
    EXPECT_EQ(evolutionState.workerState_->allowedConcurrency.load(), 2);
    EXPECT_EQ(evolutionState.workerState_->workers.size(), 2u);
    EXPECT_GT(evolutionState.visibleQueue_.size(), 0u);
    EXPECT_LT(evolutionState.visibleQueue_.size(), evolutionState.population.size());

    std::optional<Any> finalState;
    constexpr int maxTicks = 2000;
    for (int i = 0; i < maxTicks && !finalState.has_value(); ++i) {
        finalState = evolutionState.tick(*fixture.stateMachine);
    }

    ASSERT_TRUE(finalState.has_value()) << "Evolution should complete with parallel workers";
    ASSERT_TRUE(std::holds_alternative<UnsavedTrainingResult>(finalState->getVariant()))
        << "Should transition to UnsavedTrainingResult";
    EXPECT_EQ(evolutionState.generation, 1);
    EXPECT_EQ(evolutionState.currentEval, evolutionState.evolutionConfig.populationSize);
}

TEST(StateEvolutionTest, BackgroundResultsArriveWhileVisibleEvaluationRunning)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    EvolutionWorkerGuard guard{ &evolutionState, fixture.stateMachine.get() };
    evolutionState.evolutionConfig.populationSize = 4;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.5;
    evolutionState.evolutionConfig.maxParallelEvaluations = 2;
    evolutionState.trainingSpec = makeTrainingSpec(4);

    evolutionState.onEnter(*fixture.stateMachine);

    bool sawBackgroundCompletion = false;
    constexpr int maxTicks = 200;
    for (int i = 0; i < maxTicks; ++i) {
        evolutionState.tick(*fixture.stateMachine);
        if (evolutionState.visibleRunner_ != nullptr
            && evolutionState.visibleRunner_->getSimTime()
                < evolutionState.evolutionConfig.maxSimulationTime
            && evolutionState.currentEval > 0) {
            sawBackgroundCompletion = true;
            break;
        }
    }

    EXPECT_TRUE(sawBackgroundCompletion)
        << "Background results should arrive while visible evaluation is running";
}

/**
 * @brief Test that Exit command from Evolution transitions to Shutdown.
 */
TEST(StateEvolutionTest, ExitCommandTransitionsToShutdown)
{
    TestStateMachineFixture fixture;

    // Setup: Create Evolution state.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 10;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(2);
    evolutionState.onEnter(*fixture.stateMachine);

    // Setup: Create Exit command.
    bool callbackInvoked = false;
    Api::Exit::Command cmd;
    Api::Exit::Cwc cwc(cmd, [&](Api::Exit::Response&& response) {
        callbackInvoked = true;
        EXPECT_TRUE(response.isValue());
    });

    // Execute: Send Exit command.
    State::Any newState = evolutionState.onEvent(cwc, *fixture.stateMachine);

    // Verify: State transitioned to Shutdown.
    ASSERT_TRUE(std::holds_alternative<Shutdown>(newState.getVariant()))
        << "Evolution + Exit should transition to Shutdown";
    EXPECT_TRUE(callbackInvoked);
}

TEST(StateEvolutionTest, TargetCpuPercentDefaultDisabled)
{
    EvolutionConfig config;
    EXPECT_EQ(config.targetCpuPercent, 0) << "Auto-tune should be disabled by default";
}

TEST(StateEvolutionTest, ConcurrencyThrottleInitializedToBackgroundWorkerCount)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    EvolutionWorkerGuard guard{ &evolutionState, fixture.stateMachine.get() };
    evolutionState.evolutionConfig.populationSize = 4;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;
    evolutionState.evolutionConfig.maxParallelEvaluations = 4;
    evolutionState.evolutionConfig.targetCpuPercent = 0; // Disabled.
    evolutionState.trainingSpec = makeTrainingSpec(4);

    evolutionState.onEnter(*fixture.stateMachine);

    ASSERT_NE(evolutionState.workerState_, nullptr);
    // 4 parallel - 1 main thread = 3 background workers.
    EXPECT_EQ(evolutionState.workerState_->backgroundWorkerCount, 3);
    EXPECT_EQ(evolutionState.workerState_->allowedConcurrency.load(), 3);
    EXPECT_EQ(evolutionState.workerState_->activeEvaluations.load(), 0);
}
