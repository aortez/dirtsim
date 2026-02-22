#include "core/UUID.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "server/api/TrainingResultDiscard.h"
#include "server/api/TrainingResultSave.h"
#include "server/states/Idle.h"
#include "server/states/State.h"
#include "server/states/UnsavedTrainingResult.h"
#include "server/tests/TestStateMachineFixture.h"
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::State;
using namespace DirtSim::Server::Tests;

namespace {

UnsavedTrainingResult::Candidate makeCandidate(double fitness, double weightValue)
{
    UnsavedTrainingResult::Candidate candidate;
    candidate.id = UUID::generate();
    candidate.genome = Genome::constant(weightValue);
    candidate.metadata = GenomeMetadata{
        .name = "candidate",
        .fitness = fitness,
        .robustFitness = fitness,
        .robustEvalCount = 1,
        .robustFitnessSamples = { fitness },
        .generation = 1,
        .createdTimestamp = 1234567890,
        .scenarioId = Scenario::EnumType::TreeGermination,
        .notes = "",
        .organismType = OrganismType::TREE,
        .brainKind = TrainingBrainKind::NeuralNet,
        .brainVariant = std::nullopt,
        .trainingSessionId = UUID::generate(),
    };
    candidate.brainKind = TrainingBrainKind::NeuralNet;
    candidate.fitness = fitness;
    candidate.generation = 1;
    return candidate;
}

} // namespace

TEST(StateUnsavedTrainingResultTest, TrainingResultSaveStoresRequestedGenomes)
{
    TestStateMachineFixture fixture("dirtsim-test-unsaved");

    UnsavedTrainingResult state;
    state.summary.scenarioId = Scenario::EnumType::TreeGermination;
    state.summary.organismType = OrganismType::TREE;
    state.summary.populationSize = 3;
    state.summary.maxGenerations = 1;
    state.summary.completedGenerations = 1;
    state.summary.primaryBrainKind = TrainingBrainKind::NeuralNet;
    state.summary.primaryPopulationCount = 3;
    state.summary.trainingSessionId = UUID::generate();
    auto candidateA = makeCandidate(1.0, 0.1);
    auto candidateB = makeCandidate(2.0, 0.2);
    auto candidateC = makeCandidate(3.0, 0.3);
    state.candidates = { candidateA, candidateB, candidateC };

    Api::TrainingResultSave::Response capturedResponse;
    Api::TrainingResultSave::Command cmd;
    cmd.ids = { candidateA.id, candidateC.id };
    Api::TrainingResultSave::Cwc cwc(cmd, [&](Api::TrainingResultSave::Response&& response) {
        capturedResponse = std::move(response);
    });

    State::Any newState = state.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(capturedResponse.isValue());
    const auto& value = capturedResponse.value();
    EXPECT_EQ(value.savedCount, 2);
    EXPECT_EQ(value.discardedCount, 1);

    auto& repo = fixture.stateMachine->getGenomeRepository();
    EXPECT_TRUE(repo.exists(candidateA.id));
    EXPECT_TRUE(repo.exists(candidateC.id));
    EXPECT_FALSE(repo.exists(candidateB.id));
}

TEST(StateUnsavedTrainingResultTest, TrainingResultSaveRestartsEvolutionWhenRequested)
{
    TestStateMachineFixture fixture("dirtsim-test-unsaved-restart");

    UnsavedTrainingResult state;
    state.summary.scenarioId = Scenario::EnumType::TreeGermination;
    state.summary.organismType = OrganismType::TREE;
    state.summary.populationSize = 2;
    state.summary.maxGenerations = 1;
    state.summary.completedGenerations = 1;
    state.summary.primaryBrainKind = TrainingBrainKind::NeuralNet;
    state.summary.primaryPopulationCount = 2;
    state.summary.trainingSessionId = UUID::generate();
    state.evolutionConfig.populationSize = 2;
    state.mutationConfig = MutationConfig{};

    TrainingSpec spec;
    spec.scenarioId = Scenario::EnumType::TreeGermination;
    spec.organismType = OrganismType::TREE;
    PopulationSpec populationSpec;
    populationSpec.brainKind = TrainingBrainKind::NeuralNet;
    populationSpec.count = 2;
    populationSpec.randomCount = 2;
    spec.population.push_back(populationSpec);
    state.trainingSpec = spec;

    auto candidateA = makeCandidate(1.0, 0.1);
    auto candidateB = makeCandidate(2.0, 0.2);
    state.candidates = { candidateA, candidateB };

    Api::TrainingResultSave::Response capturedResponse;
    Api::TrainingResultSave::Command cmd;
    cmd.ids = { candidateA.id };
    cmd.restart = true;
    Api::TrainingResultSave::Cwc cwc(cmd, [&](Api::TrainingResultSave::Response&& response) {
        capturedResponse = std::move(response);
    });

    State::Any newState = state.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Evolution>(newState.getVariant()));
    ASSERT_TRUE(capturedResponse.isValue());
}

TEST(StateUnsavedTrainingResultTest, TrainingResultDiscardTransitionsToIdle)
{
    TestStateMachineFixture fixture("dirtsim-test-unsaved");

    UnsavedTrainingResult state;

    Api::TrainingResultDiscard::Response capturedResponse;
    Api::TrainingResultDiscard::Command cmd;
    Api::TrainingResultDiscard::Cwc cwc(cmd, [&](Api::TrainingResultDiscard::Response&& response) {
        capturedResponse = std::move(response);
    });

    State::Any newState = state.onEvent(cwc, *fixture.stateMachine);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(capturedResponse.isValue());
    EXPECT_TRUE(capturedResponse.value().discarded);
}
