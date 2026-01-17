#include "core/UUID.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "server/StateMachine.h"
#include "server/api/TrainingResultDiscard.h"
#include "server/api/TrainingResultSave.h"
#include "server/states/Idle.h"
#include "server/states/State.h"
#include "server/states/UnsavedTrainingResult.h"
#include <filesystem>
#include <gtest/gtest.h>

using namespace DirtSim;
using namespace DirtSim::Server;
using namespace DirtSim::Server::State;

class StateUnsavedTrainingResultTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        testDataDir_ = std::filesystem::temp_directory_path() / "dirtsim-test-unsaved";
        stateMachine = std::make_unique<StateMachine>(testDataDir_);
    }

    void TearDown() override
    {
        stateMachine.reset();
        std::filesystem::remove_all(testDataDir_);
    }

    UnsavedTrainingResult::Candidate makeCandidate(double fitness, double weightValue)
    {
        UnsavedTrainingResult::Candidate candidate;
        candidate.id = UUID::generate();
        candidate.genome = Genome::constant(weightValue);
        candidate.metadata = GenomeMetadata{
            .name = "candidate",
            .fitness = fitness,
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

    std::filesystem::path testDataDir_;
    std::unique_ptr<StateMachine> stateMachine;
};

TEST_F(StateUnsavedTrainingResultTest, TrainingResultSaveStoresRequestedGenomes)
{
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

    State::Any newState = state.onEvent(cwc, *stateMachine);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(capturedResponse.isValue());
    const auto& value = capturedResponse.value();
    EXPECT_EQ(value.savedCount, 2);
    EXPECT_EQ(value.discardedCount, 1);

    auto& repo = stateMachine->getGenomeRepository();
    EXPECT_TRUE(repo.exists(candidateA.id));
    EXPECT_TRUE(repo.exists(candidateC.id));
    EXPECT_FALSE(repo.exists(candidateB.id));
}

TEST_F(StateUnsavedTrainingResultTest, TrainingResultDiscardTransitionsToIdle)
{
    UnsavedTrainingResult state;

    Api::TrainingResultDiscard::Response capturedResponse;
    Api::TrainingResultDiscard::Command cmd;
    Api::TrainingResultDiscard::Cwc cwc(cmd, [&](Api::TrainingResultDiscard::Response&& response) {
        capturedResponse = std::move(response);
    });

    State::Any newState = state.onEvent(cwc, *stateMachine);

    ASSERT_TRUE(std::holds_alternative<Idle>(newState.getVariant()));
    ASSERT_TRUE(capturedResponse.isValue());
    EXPECT_TRUE(capturedResponse.value().discarded);
}
