#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/Event.h"
#include "server/api/EventSubscribe.h"
#include "server/api/EvolutionMutationControlsSet.h"
#include "server/api/EvolutionPauseSet.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/EvolutionStart.h"
#include "server/api/EvolutionStop.h"
#include "server/api/TimerStatsGet.h"
#include "server/api/TrainingResult.h"
#include "server/states/Evolution.h"
#include "server/states/Idle.h"
#include "server/states/Shutdown.h"
#include "server/states/State.h"
#include "server/tests/TestStateMachineFixture.h"
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::State;
using namespace DirtSim::Server::Tests;

namespace {

Genome makeNeuralNetGenome(WeightType value)
{
    return Genome(static_cast<size_t>(NeuralNetBrain::getGenomeLayout().totalSize()), value);
}

Genome makeDuckRecurrentV2Genome(WeightType value)
{
    return Genome(
        static_cast<size_t>(DuckNeuralNetRecurrentBrainV2::getGenomeLayout().totalSize()), value);
}

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

GenomeMetadata makeManagedGenomeMetadata(
    const std::string& name,
    double fitness,
    OrganismType organismType,
    const std::string& brainKind,
    GenomePoolId genomePoolId = GenomePoolId::DirtSim)
{
    return GenomeMetadata{
        .name = name,
        .fitness = fitness,
        .robustFitness = fitness,
        .robustEvalCount = 1,
        .robustFitnessSamples = { fitness },
        .generation = 1,
        .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
        .scenarioId = Scenario::EnumType::TreeGermination,
        .notes = "",
        .organismType = organismType,
        .brainKind = brainKind,
        .brainVariant = std::nullopt,
        .trainingSessionId = UUID::generate(),
        .genomePoolId = genomePoolId,
    };
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

void eventSubscribe(TestStateMachineFixture& fixture, const std::string& connectionId)
{
    bool callbackInvoked = false;
    Api::EventSubscribe::Response response;
    Api::EventSubscribe::Command cmd{
        .enabled = true,
        .connectionId = connectionId,
    };
    Api::EventSubscribe::Cwc cwc(cmd, [&](Api::EventSubscribe::Response&& value) {
        callbackInvoked = true;
        response = std::move(value);
    });

    fixture.stateMachine->handleEvent(Server::Event{ cwc });

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isValue());
}

std::optional<Api::EvolutionProgress> latestEvolutionProgress(const MockWebSocketService& mockWs)
{
    for (auto it = mockWs.sentClientBinaries().rbegin(); it != mockWs.sentClientBinaries().rend();
         ++it) {
        const auto envelope = Network::deserialize_envelope(it->data);
        if (envelope.message_type == Api::EvolutionProgress::name()) {
            return Network::deserialize_payload<Api::EvolutionProgress>(envelope.payload);
        }
    }
    return std::nullopt;
}

struct StateAnyEvolutionGuard {
    State::Any* state = nullptr;
    StateMachine* stateMachine = nullptr;

    ~StateAnyEvolutionGuard()
    {
        if (!state || !stateMachine) {
            return;
        }
        if (std::holds_alternative<Evolution>(state->getVariant())) {
            std::get<Evolution>(state->getVariant()).onExit(*stateMachine);
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

TEST(StateEvolutionTest, EvolutionStartKeepsNesFlappyParallelism)
{
    TestStateMachineFixture fixture;
    Idle idleState;

    bool callbackInvoked = false;
    Api::EvolutionStart::Response capturedResponse;

    Api::EvolutionStart::Command cmd;
    cmd.evolution.populationSize = 4;
    cmd.evolution.maxParallelEvaluations = 4;
    cmd.evolution.maxGenerations = 1;
    cmd.evolution.maxSimulationTime = 0.1;
    cmd.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    cmd.organismType = OrganismType::NES_DUCK;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
    population.count = 4;
    population.randomCount = 4;
    cmd.population.push_back(population);

    Api::EvolutionStart::Cwc cwc(cmd, [&](Api::EvolutionStart::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()));
    const Evolution& evolution = std::get<Evolution>(newState.getVariant());
    EXPECT_EQ(evolution.evolutionConfig.maxParallelEvaluations, 4);

    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(capturedResponse.isValue());
}

TEST(StateEvolutionTest, EvolutionStartDefaultsToDuckRecurrentBrainForNesFlappyOrganism)
{
    TestStateMachineFixture fixture;
    Idle idleState;

    Api::EvolutionStart::Response capturedResponse;
    Api::EvolutionStart::Command cmd;
    cmd.evolution.populationSize = 3;
    cmd.evolution.maxGenerations = 1;
    cmd.evolution.maxSimulationTime = 0.1;
    cmd.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    cmd.organismType = OrganismType::NES_DUCK;

    Api::EvolutionStart::Cwc cwc(cmd, [&](Api::EvolutionStart::Response&& response) {
        capturedResponse = std::move(response);
    });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()));
    ASSERT_TRUE(capturedResponse.isValue());

    const Evolution& evolution = std::get<Evolution>(newState.getVariant());
    ASSERT_EQ(evolution.trainingSpec.population.size(), 1u);
    const PopulationSpec& population = evolution.trainingSpec.population.front();
    EXPECT_EQ(population.brainKind, TrainingBrainKind::DuckNeuralNetRecurrentV2);
    EXPECT_EQ(population.count, 3);
    EXPECT_EQ(population.randomCount, 3);
}

TEST(StateEvolutionTest, EvolutionStartDefaultsToDuckRecurrentBrainForDuckClockScenario)
{
    TestStateMachineFixture fixture;
    Idle idleState;

    Api::EvolutionStart::Response capturedResponse;
    Api::EvolutionStart::Command cmd;
    cmd.evolution.populationSize = 3;
    cmd.evolution.maxGenerations = 1;
    cmd.evolution.maxSimulationTime = 0.1;
    cmd.scenarioId = Scenario::EnumType::Clock;
    cmd.organismType = OrganismType::DUCK;

    Api::EvolutionStart::Cwc cwc(cmd, [&](Api::EvolutionStart::Response&& response) {
        capturedResponse = std::move(response);
    });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()));
    ASSERT_TRUE(capturedResponse.isValue());

    const Evolution& evolution = std::get<Evolution>(newState.getVariant());
    ASSERT_EQ(evolution.trainingSpec.population.size(), 1u);
    const PopulationSpec& population = evolution.trainingSpec.population.front();
    EXPECT_EQ(population.brainKind, TrainingBrainKind::DuckNeuralNetRecurrentV2);
    EXPECT_EQ(population.count, 3);
    EXPECT_EQ(population.randomCount, 3);
}

TEST(StateEvolutionTest, EvolutionStartCapsParallelEvaluationsAtPopulationSize)
{
    TestStateMachineFixture fixture;
    Idle idleState;

    Api::EvolutionStart::Response capturedResponse;
    Api::EvolutionStart::Command cmd;
    cmd.evolution.populationSize = 2;
    cmd.evolution.maxParallelEvaluations = 4;
    cmd.evolution.maxGenerations = 1;
    cmd.evolution.maxSimulationTime = 0.1;
    cmd.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
    cmd.organismType = OrganismType::NES_DUCK;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
    population.count = 2;
    population.randomCount = 2;
    cmd.population.push_back(population);

    Api::EvolutionStart::Cwc cwc(cmd, [&](Api::EvolutionStart::Response&& response) {
        capturedResponse = std::move(response);
    });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()));
    ASSERT_TRUE(capturedResponse.isValue());

    // maxParallelEvaluations of 4 is capped at population size of 2.
    const Evolution& evolution = std::get<Evolution>(newState.getVariant());
    EXPECT_EQ(evolution.evolutionConfig.maxParallelEvaluations, 2);
}

TEST(StateEvolutionTest, EvolutionStartAllowsZeroMaxGenerations)
{
    TestStateMachineFixture fixture;
    Idle idleState;

    bool callbackInvoked = false;
    Api::EvolutionStart::Response capturedResponse;

    Api::EvolutionStart::Command cmd;
    cmd.evolution.populationSize = 2;
    cmd.evolution.maxGenerations = 0;
    cmd.evolution.maxSimulationTime = 0.1;
    cmd.scenarioId = Scenario::EnumType::TreeGermination;

    Api::EvolutionStart::Cwc cwc(cmd, [&](Api::EvolutionStart::Response&& response) {
        callbackInvoked = true;
        capturedResponse = std::move(response);
    });

    State::Any newState = idleState.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()));
    ASSERT_TRUE(callbackInvoked);
    ASSERT_TRUE(capturedResponse.isValue());

    const Evolution& evolution = std::get<Evolution>(newState.getVariant());
    EXPECT_EQ(evolution.evolutionConfig.maxGenerations, 0);
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
    const Genome bestGenome = makeNeuralNetGenome(0.25f);
    const GenomeMetadata bestMetadata{
        .name = "warm-best",
        .fitness = 9.0,
        .robustFitness = 8.5,
        .robustEvalCount = 4,
        .robustFitnessSamples = { 7.0, 8.0, 9.0, 10.0 },
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
    const Genome bestGenome = makeNeuralNetGenome(0.5f);
    const GenomeMetadata bestMetadata{
        .name = "fresh-best",
        .fitness = 4.0,
        .robustFitness = 4.0,
        .robustEvalCount = 4,
        .robustFitnessSamples = { 3.0, 4.0, 4.0, 5.0 },
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

TEST(StateEvolutionTest, EvolutionStartWarmResumeInjectsMultipleRobustSeeds)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const auto makeMetadata = [](const std::string& name, double fitness, double robustFitness) {
        return GenomeMetadata{
            .name = name,
            .fitness = fitness,
            .robustFitness = robustFitness,
            .robustEvalCount = 5,
            .robustFitnessSamples = {
                robustFitness - 1.0,
                robustFitness,
                robustFitness + 1.0,
                robustFitness,
                robustFitness,
            },
            .generation = 7,
            .createdTimestamp = 1234567890,
            .scenarioId = Scenario::EnumType::TreeGermination,
            .notes = "",
            .organismType = OrganismType::TREE,
            .brainKind = TrainingBrainKind::NeuralNet,
            .brainVariant = std::nullopt,
            .trainingSessionId = std::nullopt,
        };
    };

    const GenomeId outlierPeak = UUID::generate();
    const GenomeId robustA = UUID::generate();
    const GenomeId robustB = UUID::generate();
    const GenomeId weak = UUID::generate();
    repo.store(outlierPeak, makeNeuralNetGenome(0.1f), makeMetadata("outlier", 9999.0, 10.0));
    repo.store(robustA, makeNeuralNetGenome(0.2f), makeMetadata("robust-a", 90.0, 50.0));
    repo.store(robustB, makeNeuralNetGenome(0.3f), makeMetadata("robust-b", 80.0, 40.0));
    repo.store(weak, makeNeuralNetGenome(0.4f), makeMetadata("weak", 70.0, 5.0));
    repo.markAsBest(outlierPeak);

    Idle idleState;

    Api::EvolutionStart::Command cmd;
    cmd.resumePolicy = TrainingResumePolicy::WarmFromBest;
    cmd.evolution.populationSize = 5;
    cmd.evolution.maxGenerations = 1;
    cmd.evolution.warmStartSeedCount = 1;
    cmd.evolution.warmStartSeedPercent = 60.0;
    cmd.evolution.warmStartFitnessFloorPercentile = 60.0;
    cmd.evolution.warmStartMinRobustEvalCount = 3;
    cmd.scenarioId = Scenario::EnumType::TreeGermination;
    cmd.organismType = OrganismType::TREE;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 5;
    population.randomCount = 5;
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
    ASSERT_EQ(spec.seedGenomes.size(), 3u);
    EXPECT_EQ(spec.seedGenomes[0], robustA);
    EXPECT_NE(
        std::find(spec.seedGenomes.begin(), spec.seedGenomes.end(), robustB),
        spec.seedGenomes.end());
    EXPECT_NE(
        std::find(spec.seedGenomes.begin(), spec.seedGenomes.end(), outlierPeak),
        spec.seedGenomes.end());
    EXPECT_EQ(
        std::find(spec.seedGenomes.begin(), spec.seedGenomes.end(), weak), spec.seedGenomes.end());
    EXPECT_EQ(spec.randomCount, 2);
}

TEST(StateEvolutionTest, EvolutionStartWarmResumeSkipsIncompatibleGenomeLayouts)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const auto makeMetadata = [](const std::string& name, double robustFitness) {
        return GenomeMetadata{
            .name = name,
            .fitness = robustFitness,
            .robustFitness = robustFitness,
            .robustEvalCount = 4,
            .robustFitnessSamples = {
                robustFitness - 1.0,
                robustFitness,
                robustFitness + 1.0,
                robustFitness,
            },
            .generation = 7,
            .createdTimestamp = 1234567890,
            .scenarioId = Scenario::EnumType::Clock,
            .notes = "",
            .organismType = OrganismType::DUCK,
            .brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2,
            .brainVariant = std::nullopt,
            .trainingSessionId = std::nullopt,
        };
    };

    const GenomeId staleId = UUID::generate();
    const GenomeId validId = UUID::generate();
    repo.store(staleId, Genome(10, 0.25f), makeMetadata("stale-layout", 100.0));
    repo.store(validId, makeDuckRecurrentV2Genome(0.5f), makeMetadata("valid-layout", 90.0));

    Idle idleState;

    Api::EvolutionStart::Command cmd;
    cmd.resumePolicy = TrainingResumePolicy::WarmFromBest;
    cmd.evolution.populationSize = 3;
    cmd.evolution.maxGenerations = 1;
    cmd.evolution.maxSimulationTime = 0.1;
    cmd.evolution.warmStartSeedCount = 1;
    cmd.evolution.warmStartSeedPercent = 0.0;
    cmd.evolution.warmStartMinRobustEvalCount = 1;
    cmd.scenarioId = Scenario::EnumType::Clock;
    cmd.organismType = OrganismType::DUCK;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
    population.count = 3;
    population.randomCount = 3;
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
    EXPECT_EQ(spec.seedGenomes.front(), validId);
    EXPECT_EQ(spec.randomCount, 2);
}

TEST(StateEvolutionTest, EvolutionStartWarmResumeUsesSingleEvalSeedsForDeterministicSmb)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const GenomeId smbBestId = UUID::generate();
    const GenomeMetadata smbBestMetadata{
        .name = "smb-best",
        .fitness = 712.0,
        .robustFitness = 712.0,
        .robustEvalCount = 1,
        .robustFitnessSamples = { 712.0 },
        .generation = 12,
        .createdTimestamp = 1234567890,
        .scenarioId = Scenario::EnumType::NesSuperMarioBros,
        .notes = "",
        .organismType = OrganismType::NES_DUCK,
        .brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2,
        .brainVariant = std::nullopt,
        .trainingSessionId = std::nullopt,
        .genomePoolId = GenomePoolId::Smb,
    };
    repo.store(smbBestId, makeDuckRecurrentV2Genome(0.5f), smbBestMetadata);

    Idle idleState;

    Api::EvolutionStart::Command cmd;
    cmd.resumePolicy = TrainingResumePolicy::WarmFromBest;
    cmd.evolution.populationSize = 3;
    cmd.evolution.maxGenerations = 1;
    cmd.evolution.maxSimulationTime = 0.1;
    cmd.evolution.warmStartSeedCount = 1;
    cmd.evolution.warmStartSeedPercent = 0.0;
    cmd.evolution.warmStartMinRobustEvalCount = 3;
    cmd.scenarioId = Scenario::EnumType::NesSuperMarioBros;
    cmd.organismType = OrganismType::NES_DUCK;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2;
    population.count = 3;
    population.randomCount = 3;
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
    EXPECT_EQ(spec.seedGenomes.front(), smbBestId);
    EXPECT_EQ(spec.randomCount, 2);
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

    // Execute: First tick starts async evaluation work.
    auto result1 = evolutionState.tick(*fixture.stateMachine);
    EXPECT_FALSE(result1.has_value()) << "Should stay in Evolution";

    // Poll until the first generation completes. Async execution means a single tick may observe
    // zero, one, or many completed evaluations depending on worker timing.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && evolutionState.generation < 1) {
        auto next = evolutionState.tick(*fixture.stateMachine);
        EXPECT_FALSE(next.has_value()) << "Should stay in Evolution";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(evolutionState.generation, 1) << "Should advance to next generation";
    EXPECT_EQ(evolutionState.currentEval, 0) << "Should reset eval counter";
}

TEST(StateEvolutionTest, TimerStatsGetReturnsLiveVisibleRunnerTimersDuringEvaluation)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.5;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(1);

    evolutionState.onEnter(*fixture.stateMachine);
    EvolutionWorkerGuard guard{ .evolution = &evolutionState,
                                .stateMachine = fixture.stateMachine.get() };

    auto tickResult = evolutionState.tick(*fixture.stateMachine);
    ASSERT_FALSE(tickResult.has_value());
    ASSERT_EQ(evolutionState.currentEval, 0);
    ASSERT_NE(evolutionState.executor_, nullptr);

    bool sawVisibleProgress = false;
    bool sawLiveTimers = false;
    size_t lastTimerCount = 0u;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !sawLiveTimers) {
        if (!evolutionState.executor_->hasVisibleEvaluation()
            || evolutionState.executor_->visibleSimTimeGet() <= 0.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        sawVisibleProgress = true;

        bool callbackInvoked = false;
        Api::TimerStatsGet::Response capturedResponse;
        Api::TimerStatsGet::Command cmd;
        Api::TimerStatsGet::Cwc cwc(cmd, [&](Api::TimerStatsGet::Response&& response) {
            callbackInvoked = true;
            capturedResponse = std::move(response);
        });

        evolutionState.onEvent(cwc, *fixture.stateMachine);

        ASSERT_TRUE(callbackInvoked);
        ASSERT_TRUE(capturedResponse.isValue());

        const auto& timers = capturedResponse.value().timers;
        lastTimerCount = timers.size();
        const auto totalSimulation = timers.find("total_simulation");
        sawLiveTimers = totalSimulation != timers.end() && totalSimulation->second.calls > 0u
            && totalSimulation->second.total_ms > 0.0;
        if (!sawLiveTimers) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ASSERT_TRUE(sawVisibleProgress)
        << "Expected executor to expose a progressed visible evaluation before completion";
    ASSERT_TRUE(sawLiveTimers)
        << "Expected TimerStatsGet to expose live visible runner timers; last timer count was "
        << lastTimerCount;
    EXPECT_EQ(evolutionState.currentEval, 0);
}

TEST(StateEvolutionTest, BestFitnessThisGenUpdatesOnlyAfterRobustPass)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.1;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.evolutionConfig.robustFitnessEvaluationCount = 2;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 1;
    population.randomCount = 1;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::Clock;
    evolutionState.trainingSpec.organismType = OrganismType::DUCK;
    evolutionState.trainingSpec.population = { population };

    evolutionState.onEnter(*fixture.stateMachine);

    EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, 0.0);
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);

    auto result1 = evolutionState.tick(*fixture.stateMachine);
    EXPECT_FALSE(result1.has_value()) << "Should stay in Evolution";
    EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, 0.0)
        << "Raw generation evals should not update latest robust fitness";
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);

    const auto generationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < generationDeadline
           && evolutionState.currentEval < 1) {
        auto next = evolutionState.tick(*fixture.stateMachine);
        EXPECT_FALSE(next.has_value()) << "Robust finalization should not happen before eval ends";
        EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, 0.0)
            << "Raw generation evals should not update latest robust fitness";
        EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_EQ(evolutionState.currentEval, 1)
        << "Single-member population should finish generation eval before robust validation";
    EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, 0.0);
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);
    EXPECT_EQ(evolutionState.bestThisGenOrigin_, Evolution::IndividualOrigin::Unknown);

    std::optional<Any> finalState;
    const auto robustDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < robustDeadline && !finalState.has_value()) {
        finalState = evolutionState.tick(*fixture.stateMachine);
        if (!finalState.has_value()) {
            EXPECT_EQ(evolutionState.currentEval, 1);
            EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, 0.0)
                << "Best fitness should stay unset until robust validation finishes";
            EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);
            EXPECT_EQ(evolutionState.bestThisGenOrigin_, Evolution::IndividualOrigin::Unknown);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(finalState.has_value()) << "Evolution should complete after robust finalization";
    EXPECT_TRUE(std::holds_alternative<UnsavedTrainingResult>(finalState->getVariant()));
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 1u);
    EXPECT_GT(evolutionState.bestFitnessThisGen, 0.0);
    EXPECT_EQ(evolutionState.bestThisGenOrigin_, Evolution::IndividualOrigin::Seed)
        << "Robust pass finalization should promote the evaluated genome as this generation's best";
}

TEST(StateEvolutionTest, RobustPassKeepsOriginalFirstSampleFitnessAfterWindowTrim)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.5;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.evolutionConfig.robustFitnessEvaluationCount = 10;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 1;
    population.randomCount = 1;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::Clock;
    evolutionState.trainingSpec.organismType = OrganismType::DUCK;
    evolutionState.trainingSpec.population = { population };

    evolutionState.onEnter(*fixture.stateMachine);

    bool firstSampleCaptured = false;
    double firstSampleFitness = 0.0;
    const auto firstSampleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < firstSampleDeadline && !firstSampleCaptured) {
        auto next = evolutionState.tick(*fixture.stateMachine);
        ASSERT_FALSE(next.has_value()) << "Evolution should still be running";
        if (evolutionState.currentEval >= 1 && !evolutionState.fitnessScores.empty()) {
            firstSampleFitness = evolutionState.fitnessScores[0];
            firstSampleCaptured = true;
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ASSERT_TRUE(firstSampleCaptured) << "Expected to capture first sample fitness";
    EXPECT_GT(firstSampleFitness, 0.0);

    std::optional<Any> finalState;
    const auto completionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < completionDeadline && !finalState.has_value()) {
        finalState = evolutionState.tick(*fixture.stateMachine);
        if (!finalState.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ASSERT_TRUE(finalState.has_value()) << "Evolution should complete";
    ASSERT_TRUE(std::holds_alternative<UnsavedTrainingResult>(finalState->getVariant()));

    const auto bestId = repo.getBestId();
    ASSERT_TRUE(bestId.has_value());
    const auto metadata = repo.getMetadata(*bestId);
    ASSERT_TRUE(metadata.has_value());
    ASSERT_EQ(metadata->robustEvalCount, 10);
    ASSERT_EQ(metadata->robustFitnessSamples.size(), 7u);
    EXPECT_LT(metadata->robustFitnessSamples.size(), static_cast<size_t>(metadata->robustEvalCount))
        << "Robust fitness samples should be trimmed to the rolling window";

    EXPECT_DOUBLE_EQ(metadata->fitness, firstSampleFitness)
        << "Stored fitness should preserve the original first robust sample";

    const auto cachedSnapshot = fixture.stateMachine->getCachedTrainingBestSnapshot();
    ASSERT_TRUE(cachedSnapshot.has_value());
    EXPECT_DOUBLE_EQ(cachedSnapshot->fitness, metadata->fitness)
        << "Best snapshot should report the captured sample fitness for the displayed run";
    EXPECT_DOUBLE_EQ(cachedSnapshot->fitnessPresentation.totalFitness, metadata->fitness)
        << "Best snapshot presentation should stay aligned with the captured sample fitness";
}

TEST(StateEvolutionTest, DuckClockRobustPassKeepsConfiguredEvalCount)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.evolutionConfig.robustFitnessEvaluationCount = 3;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 1;
    population.randomCount = 1;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::Clock;
    evolutionState.trainingSpec.organismType = OrganismType::DUCK;
    evolutionState.trainingSpec.population = { population };

    evolutionState.onEnter(*fixture.stateMachine);

    std::optional<Any> finalState;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !finalState.has_value()) {
        finalState = evolutionState.tick(*fixture.stateMachine);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(finalState.has_value()) << "Evolution should complete";
    ASSERT_TRUE(std::holds_alternative<UnsavedTrainingResult>(finalState->getVariant()));

    const auto bestId = repo.getBestId();
    ASSERT_TRUE(bestId.has_value());
    const auto metadata = repo.getMetadata(*bestId);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->robustEvalCount, 3);
}

TEST(StateEvolutionTest, DuckClockVisibleEvaluationWaitsForFourPassesBeforeAdvancingEval)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.evolutionConfig.robustFitnessEvaluationCount = 0;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 2;
    population.randomCount = 2;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::Clock;
    evolutionState.trainingSpec.organismType = OrganismType::DUCK;
    evolutionState.trainingSpec.population = { population };

    evolutionState.onEnter(*fixture.stateMachine);

    EXPECT_EQ(evolutionState.currentEval, 0);

    auto firstTick = evolutionState.tick(*fixture.stateMachine);
    EXPECT_FALSE(firstTick.has_value());
    EXPECT_EQ(evolutionState.currentEval, 0)
        << "Duck clock eval should not advance on the first tick";

    std::optional<Any> tickResult;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && evolutionState.currentEval < 1
           && !tickResult.has_value()) {
        tickResult = evolutionState.tick(*fixture.stateMachine);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_FALSE(tickResult.has_value());
    EXPECT_EQ(evolutionState.currentEval, 1)
        << "Duck clock eval should advance only after the merged worker result completes";
}

TEST(StateEvolutionTest, DuckClockConfiguredCountZeroSkipsRobustPassAndStoresSingleSampleBest)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.evolutionConfig.robustFitnessEvaluationCount = 0;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 1;
    population.randomCount = 1;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::Clock;
    evolutionState.trainingSpec.organismType = OrganismType::DUCK;
    evolutionState.trainingSpec.population = { population };

    evolutionState.onEnter(*fixture.stateMachine);

    std::optional<Any> finalTick;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !finalTick.has_value()) {
        finalTick = evolutionState.tick(*fixture.stateMachine);
        if (!finalTick.has_value()) {
            EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u)
                << "Robust pass should remain disabled while generation eval runs";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(finalTick.has_value());
    EXPECT_TRUE(std::holds_alternative<UnsavedTrainingResult>(finalTick->getVariant()));
    EXPECT_EQ(evolutionState.currentEval, 1);
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);
    EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, evolutionState.bestFitnessAllTime);

    const auto bestId = repo.getBestId();
    ASSERT_TRUE(bestId.has_value());
    const auto metadata = repo.getMetadata(*bestId);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->robustEvalCount, 1);
    ASSERT_EQ(metadata->robustFitnessSamples.size(), 1u);
    EXPECT_DOUBLE_EQ(metadata->robustFitness, metadata->fitness);
}

TEST(StateEvolutionTest, DuckClockRobustValidationWaitsForAllSamplesBeforeFinalizing)
{
    TestStateMachineFixture fixture;
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.evolutionConfig.robustFitnessEvaluationCount = 2;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = 1;
    population.randomCount = 1;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::Clock;
    evolutionState.trainingSpec.organismType = OrganismType::DUCK;
    evolutionState.trainingSpec.population = { population };

    evolutionState.onEnter(*fixture.stateMachine);

    const auto generationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < generationDeadline
           && evolutionState.currentEval < 1) {
        auto tickResult = evolutionState.tick(*fixture.stateMachine);
        EXPECT_FALSE(tickResult.has_value());
        EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u)
            << "Robust pass should not finalize before generation eval completes";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(evolutionState.currentEval, 1);
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);

    std::optional<Any> robustFinalizeTick;
    const auto robustDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < robustDeadline && !robustFinalizeTick.has_value()) {
        robustFinalizeTick = evolutionState.tick(*fixture.stateMachine);
        if (!robustFinalizeTick.has_value()) {
            EXPECT_EQ(evolutionState.currentEval, 1);
            EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u)
                << "Duck clock should wait for all validation samples";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(robustFinalizeTick.has_value());
    EXPECT_TRUE(std::holds_alternative<UnsavedTrainingResult>(robustFinalizeTick->getVariant()));
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 1u)
        << "Duck clock robust validation should finalize after all samples complete";

    const auto bestId = repo.getBestId();
    ASSERT_TRUE(bestId.has_value());
    const auto metadata = repo.getMetadata(*bestId);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->robustEvalCount, 2);
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

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && evolutionState.generation < 1) {
        auto tickResult = evolutionState.tick(*fixture.stateMachine);
        EXPECT_FALSE(tickResult.has_value()) << "Generation advance should stay within Evolution";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(evolutionState.generation, 1);
    for (const auto& individual : evolutionState.population) {
        EXPECT_EQ(individual.brainKind, TrainingBrainKind::RuleBased);
        EXPECT_FALSE(individual.genome.has_value());
    }
}

TEST(StateEvolutionTest, NonNeuralBrainsUpdateBestFitnessWithoutRobustPass)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.1;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;

    PopulationSpec population;
    population.brainKind = TrainingBrainKind::RuleBased;
    population.count = 2;

    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    evolutionState.trainingSpec.organismType = OrganismType::TREE;
    evolutionState.trainingSpec.population.push_back(population);

    evolutionState.onEnter(*fixture.stateMachine);

    EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, 0.0);
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);
    EXPECT_EQ(evolutionState.bestThisGenOrigin_, Evolution::IndividualOrigin::Unknown);

    std::optional<Any> finalState;
    bool sawProcessedResult = false;
    const auto firstResultDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < firstResultDeadline && !sawProcessedResult) {
        finalState = evolutionState.tick(*fixture.stateMachine);
        if (evolutionState.currentEval >= 1) {
            sawProcessedResult = true;
            break;
        }

        EXPECT_FALSE(finalState.has_value()) << "Training should not complete before any eval ends";
        EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, 0.0);
        EXPECT_EQ(evolutionState.bestThisGenOrigin_, Evolution::IndividualOrigin::Unknown);
        EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(sawProcessedResult) << "Expected a non-neural result to be processed";
    EXPECT_GT(evolutionState.bestFitnessThisGen, 0.0);
    EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, evolutionState.bestFitnessAllTime);
    EXPECT_EQ(evolutionState.bestThisGenOrigin_, Evolution::IndividualOrigin::Seed);
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);

    const auto completionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < completionDeadline && !finalState.has_value()) {
        finalState = evolutionState.tick(*fixture.stateMachine);
        EXPECT_GT(evolutionState.bestFitnessThisGen, 0.0);
        EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, evolutionState.bestFitnessAllTime);
        EXPECT_EQ(evolutionState.bestThisGenOrigin_, Evolution::IndividualOrigin::Seed);
        EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(finalState.has_value()) << "Evolution should finish after both rule-based evals";
    EXPECT_TRUE(std::holds_alternative<UnsavedTrainingResult>(finalState->getVariant()));

    const double expectedBest =
        std::max(evolutionState.fitnessScores[0], evolutionState.fitnessScores[1]);
    EXPECT_DOUBLE_EQ(evolutionState.bestFitnessThisGen, expectedBest);
    EXPECT_DOUBLE_EQ(evolutionState.bestFitnessAllTime, expectedBest);
    EXPECT_EQ(evolutionState.bestThisGenOrigin_, Evolution::IndividualOrigin::Seed);
    EXPECT_EQ(evolutionState.robustEvaluationCount_, 0u);
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
        .perturbationsPerOffspring = 0,
        .resetsPerOffspring = 0,
        .sigma = 0.5,
    };
    evolutionState.trainingSpec = makeTrainingSpec(2);

    evolutionState.onEnter(*fixture.stateMachine);

    std::vector<Genome> parents;
    parents.reserve(evolutionState.population.size());
    for (const auto& individual : evolutionState.population) {
        ASSERT_TRUE(individual.genome.has_value());
        parents.push_back(individual.genome.value());
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && evolutionState.generation < 1) {
        auto result = evolutionState.tick(*fixture.stateMachine);
        ASSERT_FALSE(result.has_value()) << "Should stay in Evolution";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_EQ(evolutionState.generation, 1);
    ASSERT_EQ(evolutionState.population.size(), parents.size() * 2);

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

    const Genome seedGenome = makeNeuralNetGenome(0.1f);
    const GenomeId seedId = UUID::generate();
    const GenomeMetadata seedMeta{
        .name = "seed",
        .fitness = 1.0,
        .robustFitness = 1.0,
        .robustEvalCount = 1,
        .robustFitnessSamples = { 1.0 },
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
    repo.markAsBest(seedId);

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

    EXPECT_TRUE(evolutionState.bestGenomeId.isNil());

    std::optional<Any> finalState;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !finalState.has_value()) {
        finalState = evolutionState.tick(*fixture.stateMachine);
        if (!finalState.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    ASSERT_TRUE(finalState.has_value());

    ASSERT_EQ(evolutionState.fitnessScores.size(), 2u);
    EXPECT_DOUBLE_EQ(evolutionState.fitnessScores[0], evolutionState.fitnessScores[1]);
    EXPECT_FALSE(evolutionState.bestGenomeId.isNil());
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
        .perturbationsPerOffspring = 0,
        .resetsPerOffspring = 5000,
        .sigma = 0.5,
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

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && evolutionState.generation < 1) {
        auto result = evolutionState.tick(*fixture.stateMachine);
        ASSERT_FALSE(result.has_value()) << "Should stay in Evolution";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_EQ(evolutionState.generation, 1);
    ASSERT_EQ(evolutionState.population.size(), parents.size() * 2);

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

TEST(StateEvolutionTest, NeuralNetMutationCanSurviveWithPositiveFitness)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.048;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.mutationConfig = MutationConfig{
        .perturbationsPerOffspring = 0,
        .resetsPerOffspring = 5000,
        .sigma = 0.5,
    };
    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const Genome seedGenome = makeNeuralNetGenome(0.1f);
    const GenomeId seedId = UUID::generate();
    const GenomeMetadata seedMeta{
        .name = "seed",
        .fitness = 1.0,
        .robustFitness = 1.0,
        .robustEvalCount = 1,
        .robustFitnessSamples = { 1.0 },
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

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && evolutionState.generation < 1) {
        auto result = evolutionState.tick(*fixture.stateMachine);
        ASSERT_FALSE(result.has_value()) << "Should stay in Evolution";
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_EQ(evolutionState.generation, 1);
    ASSERT_EQ(evolutionState.population.size(), parents.size() * 2);

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
        }
    }
    EXPECT_TRUE(foundMutation);
}

TEST(StateEvolutionTest, AdaptiveBudgetedMutationTracksPhaseAndKeepsPopulationSize)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.maxGenerations = 4;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 0.0;
    evolutionState.evolutionConfig.robustFitnessEvaluationCount = 0;
    evolutionState.evolutionConfig.stagnationWindowGenerations = 1;
    evolutionState.evolutionConfig.recoveryWindowGenerations = 3;
    evolutionState.mutationConfig = MutationConfig{
        .perturbationsPerOffspring = 10,
        .resetsPerOffspring = 1,
        .sigma = 0.05,
    };

    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.clear();

    const Genome seedGenome = makeNeuralNetGenome(0.1f);
    const GenomeId seedId = UUID::generate();
    const GenomeMetadata seedMeta{
        .name = "seed",
        .fitness = 1.0,
        .robustFitness = 1.0,
        .robustEvalCount = 1,
        .robustFitnessSamples = { 1.0 },
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

    const auto advanceToGeneration = [&evolutionState, &fixture](int targetGeneration) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline
               && evolutionState.generation < targetGeneration) {
            auto result = evolutionState.tick(*fixture.stateMachine);
            ASSERT_FALSE(result.has_value()) << "Should stay in Evolution";
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(evolutionState.generation, targetGeneration);
    };

    advanceToGeneration(1);
    EXPECT_EQ(evolutionState.trainingPhaseTracker_.status().phase, TrainingPhase::Normal);
    EXPECT_EQ(evolutionState.lastEffectiveAdaptiveMutation_.mode, AdaptiveMutationMode::Baseline);
    EXPECT_EQ(
        evolutionState.lastEffectiveAdaptiveMutation_.mutationConfig.perturbationsPerOffspring,
        evolutionState.mutationConfig.perturbationsPerOffspring);
    EXPECT_EQ(evolutionState.population.size(), 4u);

    advanceToGeneration(2);
    EXPECT_EQ(evolutionState.trainingPhaseTracker_.status().phase, TrainingPhase::Plateau);
    EXPECT_EQ(evolutionState.lastEffectiveAdaptiveMutation_.mode, AdaptiveMutationMode::Explore);
    EXPECT_GT(
        evolutionState.lastEffectiveAdaptiveMutation_.mutationConfig.perturbationsPerOffspring,
        evolutionState.mutationConfig.perturbationsPerOffspring);
    EXPECT_GT(
        evolutionState.lastEffectiveAdaptiveMutation_.mutationConfig.resetsPerOffspring,
        evolutionState.mutationConfig.resetsPerOffspring);
    EXPECT_EQ(evolutionState.population.size(), 4u);

    const EffectiveAdaptiveMutation exploreMutation = evolutionState.lastEffectiveAdaptiveMutation_;

    advanceToGeneration(3);
    EXPECT_EQ(evolutionState.trainingPhaseTracker_.status().phase, TrainingPhase::Stuck);
    EXPECT_EQ(evolutionState.lastEffectiveAdaptiveMutation_.mode, AdaptiveMutationMode::Rescue);
    EXPECT_GT(
        evolutionState.lastEffectiveAdaptiveMutation_.mutationConfig.perturbationsPerOffspring,
        exploreMutation.mutationConfig.perturbationsPerOffspring);
    EXPECT_GT(
        evolutionState.lastEffectiveAdaptiveMutation_.mutationConfig.resetsPerOffspring,
        exploreMutation.mutationConfig.resetsPerOffspring);
    EXPECT_GT(
        evolutionState.lastEffectiveAdaptiveMutation_.mutationConfig.sigma,
        exploreMutation.mutationConfig.sigma);
    EXPECT_EQ(evolutionState.population.size(), 4u);
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !result.has_value()) {
        result = evolutionState.tick(*fixture.stateMachine);
        if (!result.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ASSERT_TRUE(result.has_value()) << "Should transition after training result delivery";
    ASSERT_TRUE(std::holds_alternative<UnsavedTrainingResult>(result->getVariant()))
        << "Should transition to UnsavedTrainingResult";
    EXPECT_EQ(evolutionState.generation, 2);
}

TEST(StateEvolutionTest, CompletesAllGenerationsWhenTrainingResultDeliveryFails)
{
    TestStateMachineFixture fixture;
    fixture.mockWebSocketService->expectError<Api::TrainingResult>("No UI peer available");

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.016;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(1);

    evolutionState.onEnter(*fixture.stateMachine);

    std::optional<Any> result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !result.has_value()) {
        result = evolutionState.tick(*fixture.stateMachine);
        if (!result.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ASSERT_TRUE(result.has_value()) << "Should still transition after training completion";
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

    std::optional<Any> finalState;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !finalState.has_value()) {
        finalState = evolutionState.tick(*fixture.stateMachine);
        if (!finalState.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    ASSERT_TRUE(finalState.has_value());

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
 * @brief Test that tick() polls async evaluation progress incrementally.
 *
 * With a longer simulation time, worker-owned evaluation progress should become visible before the
 * main thread drains a completed result. This verifies tick() stays responsive while background
 * evaluation advances.
 */
TEST(StateEvolutionTest, TickAdvancesEvaluationIncrementally)
{
    TestStateMachineFixture fixture;

    // Setup: Create Evolution state with longer simulation time.
    // Use population=2 and maxGenerations=2 so we can observe early generation progress.
    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 2;
    evolutionState.evolutionConfig.maxSimulationTime = 0.5;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(2);

    // Initialize the state.
    evolutionState.onEnter(*fixture.stateMachine);
    EvolutionWorkerGuard guard{ .evolution = &evolutionState,
                                .stateMachine = fixture.stateMachine.get() };
    ASSERT_NE(evolutionState.executor_, nullptr);

    // Execute: First tick should start async work without blocking for completion.
    auto firstTick = evolutionState.tick(*fixture.stateMachine);
    ASSERT_FALSE(firstTick.has_value()) << "Should stay in Evolution";

    bool sawVisibleProgress = false;
    const auto visibleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < visibleDeadline && !sawVisibleProgress) {
        sawVisibleProgress = evolutionState.executor_->hasVisibleEvaluation()
            && evolutionState.executor_->visibleSimTimeGet() > 0.0;
        if (!sawVisibleProgress) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ASSERT_TRUE(sawVisibleProgress)
        << "Expected async worker progress to become visible before the first result is drained";
    EXPECT_EQ(evolutionState.currentEval, 0)
        << "No completed result should be observed before the main thread polls again";
    EXPECT_EQ(evolutionState.generation, 0);

    bool sawStateAdvance = false;
    const auto advanceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < advanceDeadline && !sawStateAdvance) {
        auto tickResult = evolutionState.tick(*fixture.stateMachine);
        sawStateAdvance = evolutionState.currentEval >= 1 || evolutionState.generation >= 1;
        if (!sawStateAdvance) {
            EXPECT_FALSE(tickResult.has_value()) << "Training should still be running";
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    ASSERT_TRUE(sawStateAdvance)
        << "Expected polling tick() to eventually drain completed work or advance the generation";
    EXPECT_TRUE(evolutionState.currentEval >= 1 || evolutionState.generation >= 1);
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
    std::optional<Any> finalState;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() < deadline && !finalState.has_value()) {
        finalState = evolutionState.tick(*fixture.stateMachine);
        if (!finalState.has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
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
    const double bestDisplayFitness =
        (metadata->robustEvalCount > 0 || !metadata->robustFitnessSamples.empty())
        ? metadata->robustFitness
        : metadata->fitness;
    EXPECT_EQ(bestDisplayFitness, evolutionState.bestFitnessAllTime)
        << "Stored fitness should match tracked best";
}

TEST(StateEvolutionTest, EvolutionPauseProgressReportsPausedWithoutGrowingTrainingTime)
{
    TestStateMachineFixture fixture;
    eventSubscribe(fixture, "events");

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 1.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(1);

    evolutionState.onEnter(*fixture.stateMachine);
    State::Any currentState = std::move(evolutionState);
    StateAnyEvolutionGuard guard{ .state = &currentState,
                                  .stateMachine = fixture.stateMachine.get() };

    auto& initialEvolution = std::get<Evolution>(currentState.getVariant());
    auto tickResult = initialEvolution.tick(*fixture.stateMachine);
    ASSERT_FALSE(tickResult.has_value());

    const auto progressDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < progressDeadline) {
        auto& currentEvolution = std::get<Evolution>(currentState.getVariant());
        currentEvolution.tick(*fixture.stateMachine);
        if (currentEvolution.executor_ && currentEvolution.executor_->visibleSimTimeGet() > 0.0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto& readyEvolution = std::get<Evolution>(currentState.getVariant());
    ASSERT_NE(readyEvolution.executor_, nullptr);
    ASSERT_GT(readyEvolution.executor_->visibleSimTimeGet(), 0.0);

    const auto sendPause = [&](bool paused) {
        bool callbackInvoked = false;
        Api::EvolutionPauseSet::Response response;
        Api::EvolutionPauseSet::Command cmd{ .paused = paused };
        Api::EvolutionPauseSet::Cwc cwc(cmd, [&](Api::EvolutionPauseSet::Response&& value) {
            callbackInvoked = true;
            response = std::move(value);
        });

        fixture.mockWebSocketService->clearSentClientBinaries();
        State::Any nextState =
            std::get<Evolution>(currentState.getVariant()).onEvent(cwc, *fixture.stateMachine);
        EXPECT_TRUE(callbackInvoked);
        EXPECT_TRUE(response.isValue());
        currentState = std::move(nextState);
        EXPECT_TRUE(std::holds_alternative<Evolution>(currentState.getVariant()));
        return response;
    };

    const auto firstResponse = sendPause(true);
    ASSERT_TRUE(firstResponse.isValue());
    EXPECT_TRUE(firstResponse.value().paused);
    EXPECT_TRUE(std::get<Evolution>(currentState.getVariant()).executor_->isPaused());

    const auto firstProgress = latestEvolutionProgress(*fixture.mockWebSocketService);
    ASSERT_TRUE(firstProgress.has_value());
    EXPECT_TRUE(firstProgress->isPaused);
    const double pausedTrainingSeconds = firstProgress->totalTrainingSeconds;

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const auto secondResponse = sendPause(true);
    ASSERT_TRUE(secondResponse.isValue());
    EXPECT_TRUE(secondResponse.value().paused);

    const auto secondProgress = latestEvolutionProgress(*fixture.mockWebSocketService);
    ASSERT_TRUE(secondProgress.has_value());
    EXPECT_TRUE(secondProgress->isPaused);
    EXPECT_NEAR(secondProgress->totalTrainingSeconds, pausedTrainingSeconds, 0.01);
}

TEST(StateEvolutionTest, EvolutionProgressReportsManagedArchiveOccupancyForTrainingBuckets)
{
    TestStateMachineFixture fixture;
    eventSubscribe(fixture, "events");

    auto& repo = fixture.stateMachine->getGenomeRepository();
    repo.store(
        UUID::generate(),
        makeNeuralNetGenome(0.1f),
        makeManagedGenomeMetadata(
            "tree_net_a", 1.0, OrganismType::TREE, TrainingBrainKind::NeuralNet));
    repo.store(
        UUID::generate(),
        makeNeuralNetGenome(0.2f),
        makeManagedGenomeMetadata(
            "tree_net_b", 2.0, OrganismType::TREE, TrainingBrainKind::NeuralNet));
    repo.store(
        UUID::generate(),
        makeNeuralNetGenome(0.3f),
        makeManagedGenomeMetadata(
            "tree_rule", 3.0, OrganismType::TREE, TrainingBrainKind::RuleBased));

    auto unmanagedTreeNet = makeManagedGenomeMetadata(
        "tree_net_unmanaged", 4.0, OrganismType::TREE, TrainingBrainKind::NeuralNet);
    unmanagedTreeNet.trainingSessionId = std::nullopt;
    repo.store(UUID::generate(), makeNeuralNetGenome(0.4f), unmanagedTreeNet);
    repo.store(
        UUID::generate(),
        makeNeuralNetGenome(0.5f),
        makeManagedGenomeMetadata(
            "tree_net_other_pool",
            5.0,
            OrganismType::TREE,
            TrainingBrainKind::NeuralNet,
            GenomePoolId::FlappyParatroopa));
    repo.store(
        UUID::generate(),
        makeNeuralNetGenome(0.6f),
        makeManagedGenomeMetadata(
            "duck_net", 6.0, OrganismType::DUCK, TrainingBrainKind::NeuralNet));

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 2;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 1.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.evolutionConfig.genomeArchiveMaxSize = 2;
    evolutionState.trainingSpec.scenarioId = Scenario::EnumType::TreeGermination;
    evolutionState.trainingSpec.organismType = OrganismType::TREE;
    evolutionState.trainingSpec.population.push_back(
        PopulationSpec{
            .brainKind = TrainingBrainKind::NeuralNet,
            .brainVariant = std::nullopt,
            .count = 1,
            .seedGenomes = {},
            .randomCount = 1,
        });
    evolutionState.trainingSpec.population.push_back(
        PopulationSpec{
            .brainKind = TrainingBrainKind::RuleBased,
            .brainVariant = std::nullopt,
            .count = 1,
            .seedGenomes = {},
            .randomCount = 0,
        });

    evolutionState.onEnter(*fixture.stateMachine);
    State::Any currentState = std::move(evolutionState);
    StateAnyEvolutionGuard guard{ .state = &currentState,
                                  .stateMachine = fixture.stateMachine.get() };

    bool pauseCallbackInvoked = false;
    Api::EvolutionPauseSet::Response pauseResponse;
    Api::EvolutionPauseSet::Command pauseCmd{ .paused = true };
    Api::EvolutionPauseSet::Cwc pauseCwc(
        pauseCmd, [&](Api::EvolutionPauseSet::Response&& response) {
            pauseCallbackInvoked = true;
            pauseResponse = std::move(response);
        });

    State::Any pausedState =
        std::get<Evolution>(currentState.getVariant()).onEvent(pauseCwc, *fixture.stateMachine);
    ASSERT_TRUE(pauseCallbackInvoked);
    ASSERT_TRUE(pauseResponse.isValue());
    currentState = std::move(pausedState);
    ASSERT_TRUE(std::holds_alternative<Evolution>(currentState.getVariant()));

    const auto progress = latestEvolutionProgress(*fixture.mockWebSocketService);
    ASSERT_TRUE(progress.has_value());
    EXPECT_EQ(progress->totalGenomeCount, 3);
    EXPECT_EQ(progress->genomeArchiveMaxSize, 4);
}

TEST(StateEvolutionTest, EvolutionMutationControlsSetUpdatesRunningConfig)
{
    TestStateMachineFixture fixture;
    eventSubscribe(fixture, "events");

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 1.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(1);

    evolutionState.onEnter(*fixture.stateMachine);
    State::Any currentState = std::move(evolutionState);
    StateAnyEvolutionGuard guard{ .state = &currentState,
                                  .stateMachine = fixture.stateMachine.get() };

    bool callbackInvoked = false;
    Api::EvolutionMutationControlsSet::Response response;
    Api::EvolutionMutationControlsSet::Command cmd{
        .mutationConfig =
            MutationConfig{
                .perturbationsPerOffspring = 6000,
                .resetsPerOffspring = 250,
                .sigma = 0.5,
            },
        .stagnationWindowGenerations = 0,
        .recoveryWindowGenerations = 999,
        .controlMode = AdaptiveMutationControlMode::Rescue,
    };
    Api::EvolutionMutationControlsSet::Cwc cwc(
        cmd, [&](Api::EvolutionMutationControlsSet::Response&& value) {
            callbackInvoked = true;
            response = std::move(value);
        });

    State::Any nextState =
        std::get<Evolution>(currentState.getVariant()).onEvent(cwc, *fixture.stateMachine);

    EXPECT_TRUE(callbackInvoked);
    ASSERT_TRUE(response.isValue());
    EXPECT_TRUE(std::holds_alternative<Evolution>(nextState.getVariant()));

    const auto& okay = response.value();
    EXPECT_EQ(okay.mutationConfig.perturbationsPerOffspring, 5000);
    EXPECT_EQ(okay.mutationConfig.resetsPerOffspring, 200);
    EXPECT_DOUBLE_EQ(okay.mutationConfig.sigma, 0.3);
    EXPECT_EQ(okay.stagnationWindowGenerations, 1);
    EXPECT_EQ(okay.recoveryWindowGenerations, 100);
    EXPECT_EQ(okay.controlMode, AdaptiveMutationControlMode::Rescue);

    currentState = std::move(nextState);
    const auto& updated = std::get<Evolution>(currentState.getVariant());
    EXPECT_EQ(updated.mutationConfig.perturbationsPerOffspring, 5000);
    EXPECT_EQ(updated.mutationConfig.resetsPerOffspring, 200);
    EXPECT_DOUBLE_EQ(updated.mutationConfig.sigma, 0.3);
    EXPECT_EQ(updated.evolutionConfig.stagnationWindowGenerations, 1);
    EXPECT_EQ(updated.evolutionConfig.recoveryWindowGenerations, 100);
    EXPECT_EQ(updated.mutationControlMode_, AdaptiveMutationControlMode::Rescue);

    const auto progress = latestEvolutionProgress(*fixture.mockWebSocketService);
    ASSERT_TRUE(progress.has_value());
    EXPECT_EQ(progress->trainingPhase, TrainingPhase::Normal);
}

TEST(StateEvolutionTest, EvolutionStopStillWorksWhilePaused)
{
    TestStateMachineFixture fixture;

    Evolution evolutionState;
    evolutionState.evolutionConfig.populationSize = 1;
    evolutionState.evolutionConfig.maxGenerations = 1;
    evolutionState.evolutionConfig.maxSimulationTime = 1.0;
    evolutionState.evolutionConfig.maxParallelEvaluations = 1;
    evolutionState.trainingSpec = makeTrainingSpec(1);

    evolutionState.onEnter(*fixture.stateMachine);
    State::Any currentState = std::move(evolutionState);
    StateAnyEvolutionGuard guard{ .state = &currentState,
                                  .stateMachine = fixture.stateMachine.get() };

    const auto progressDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < progressDeadline) {
        auto& currentEvolution = std::get<Evolution>(currentState.getVariant());
        currentEvolution.tick(*fixture.stateMachine);
        if (currentEvolution.executor_ && currentEvolution.executor_->visibleSimTimeGet() > 0.0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(std::holds_alternative<Evolution>(currentState.getVariant()));
    ASSERT_NE(std::get<Evolution>(currentState.getVariant()).executor_, nullptr);

    Api::EvolutionPauseSet::Command pauseCmd{ .paused = true };
    Api::EvolutionPauseSet::Response pauseResponse;
    Api::EvolutionPauseSet::Cwc pauseCwc(
        pauseCmd,
        [&](Api::EvolutionPauseSet::Response&& response) { pauseResponse = std::move(response); });
    State::Any pausedState =
        std::get<Evolution>(currentState.getVariant()).onEvent(pauseCwc, *fixture.stateMachine);
    ASSERT_TRUE(pauseResponse.isValue());
    EXPECT_TRUE(pauseResponse.value().paused);
    currentState = std::move(pausedState);
    ASSERT_TRUE(std::holds_alternative<Evolution>(currentState.getVariant()));

    bool stopCallbackInvoked = false;
    Api::EvolutionStop::Response stopResponse;
    Api::EvolutionStop::Command stopCmd;
    Api::EvolutionStop::Cwc stopCwc(stopCmd, [&](Api::EvolutionStop::Response&& response) {
        stopCallbackInvoked = true;
        stopResponse = std::move(response);
    });

    State::Any nextState =
        std::get<Evolution>(currentState.getVariant()).onEvent(stopCwc, *fixture.stateMachine);

    EXPECT_TRUE(stopCallbackInvoked);
    ASSERT_TRUE(stopResponse.isValue());
    EXPECT_TRUE(std::holds_alternative<Idle>(nextState.getVariant()));
    currentState = std::move(nextState);
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
    EXPECT_DOUBLE_EQ(config.warmStartSeedPercent, 20.0)
        << "Warm-start seed percent should default to 20%";
    EXPECT_TRUE(config.warmStartAlwaysIncludeBest)
        << "Warm start should include the best robust genome by default";
    EXPECT_DOUBLE_EQ(config.warmStartNoveltyWeight, 0.3)
        << "Warm-start novelty mixing should default to 30%";
    EXPECT_DOUBLE_EQ(config.warmStartFitnessFloorPercentile, 60.0)
        << "Warm-start stochastic sampling should default to top 40% by robust fitness";
    EXPECT_EQ(config.diversityEliteCount, 1)
        << "Diversity elitism should retain one near-best elite";
    EXPECT_DOUBLE_EQ(config.diversityEliteFitnessEpsilon, 0.0)
        << "Diversity elitism epsilon should default to exact best ties";
}
