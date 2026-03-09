#include "core/organisms/brains/DuckNeuralNetRecurrentBrain.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "server/evolution/EvaluationExecutor.h"
#include "server/evolution/FitnessModelBundle.h"
#include "server/tests/TestStateMachineFixture.h"

#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace DirtSim;
using namespace DirtSim::Server::EvolutionSupport;
using namespace DirtSim::Server::Tests;

namespace {

Genome makeNeuralNetGenome(WeightType value)
{
    return Genome(static_cast<size_t>(NeuralNetBrain::getGenomeLayout().totalSize()), value);
}

TrainingSpec makeTrainingSpec(
    Scenario::EnumType scenarioId, OrganismType organismType, int populationSize)
{
    PopulationSpec population;
    population.brainKind = TrainingBrainKind::NeuralNet;
    population.count = populationSize;
    population.randomCount = populationSize;

    TrainingSpec spec;
    spec.scenarioId = scenarioId;
    spec.organismType = organismType;
    spec.population.push_back(population);
    return spec;
}

EvaluationRequest makeGenerationRequest(
    int index, Scenario::EnumType scenarioId, WeightType genomeWeight)
{
    return EvaluationRequest{
        .taskType = EvaluationTaskType::GenerationEval,
        .index = index,
        .robustGeneration = -1,
        .robustSampleOrdinal = 0,
        .individual =
            EvaluationIndividual{
                .brainKind = TrainingBrainKind::NeuralNet,
                .brainVariant = std::nullopt,
                .scenarioId = scenarioId,
                .genome = makeNeuralNetGenome(genomeWeight),
            },
    };
}

EvaluationRequest makeDuckClockGenerationRequest(int index)
{
    std::mt19937 rng(1234u + static_cast<uint32_t>(index));
    return EvaluationRequest{
        .taskType = EvaluationTaskType::GenerationEval,
        .index = index,
        .robustGeneration = -1,
        .robustSampleOrdinal = 0,
        .individual =
            EvaluationIndividual{
                .brainKind = TrainingBrainKind::DuckNeuralNetRecurrent,
                .brainVariant = std::nullopt,
                .scenarioId = Scenario::EnumType::Clock,
                .genome = DuckNeuralNetRecurrentBrain::randomGenome(rng),
            },
    };
}

EvaluationExecutor makeExecutor(
    const TrainingSpec& trainingSpec, GenomeRepository& genomeRepository)
{
    return EvaluationExecutor(
        EvaluationExecutor::Config{
            .trainingSpec = trainingSpec,
            .brainRegistry = TrainingBrainRegistry::createDefault(),
            .genomeRepository = &genomeRepository,
            .fitnessModel = fitnessModelResolve(trainingSpec.organismType, trainingSpec.scenarioId),
        });
}

EvolutionConfig makeEvolutionConfig(int populationSize, double maxSimulationTime)
{
    EvolutionConfig config;
    config.populationSize = populationSize;
    config.maxSimulationTime = maxSimulationTime;
    return config;
}

} // namespace

TEST(EvaluationExecutorTest, VisibleEvaluationAdvancesIncrementally)
{
    TestStateMachineFixture fixture;
    const TrainingSpec trainingSpec =
        makeTrainingSpec(Scenario::EnumType::TreeGermination, OrganismType::TREE, 1);
    EvaluationExecutor executor =
        makeExecutor(trainingSpec, fixture.stateMachine->getGenomeRepository());
    const EvolutionConfig evolutionConfig = makeEvolutionConfig(1, 0.1);
    const std::vector<EvaluationRequest> requests{
        makeGenerationRequest(0, Scenario::EnumType::TreeGermination, 0.1f),
    };

    executor.start(1);
    executor.generationBatchSubmit(requests, evolutionConfig, std::nullopt);

    EXPECT_FALSE(executor.hasVisibleEvaluation());

    const auto firstTick = executor.visibleTick(std::chrono::steady_clock::now(), 0);
    EXPECT_TRUE(firstTick.progressed);
    EXPECT_TRUE(firstTick.completed.empty());
    EXPECT_TRUE(executor.hasVisibleEvaluation());
    EXPECT_GT(executor.visibleSimTimeGet(), 0.0);
    EXPECT_LT(executor.visibleSimTimeGet(), 0.1);

    std::vector<CompletedEvaluation> completed;
    int tickCount = 1;
    while (completed.empty() && tickCount < 20) {
        auto tick = executor.visibleTick(std::chrono::steady_clock::now(), 0);
        if (!tick.completed.empty()) {
            completed = std::move(tick.completed);
        }
        tickCount++;
    }

    EXPECT_GT(tickCount, 1);
    ASSERT_EQ(completed.size(), 1u);
    EXPECT_FALSE(executor.hasVisibleEvaluation());
    EXPECT_EQ(completed.front().index, 0);
}

TEST(EvaluationExecutorTest, GenerationBatchSplitsVisibleAndBackgroundEvaluations)
{
    TestStateMachineFixture fixture;
    const TrainingSpec trainingSpec =
        makeTrainingSpec(Scenario::EnumType::TreeGermination, OrganismType::TREE, 5);
    EvaluationExecutor executor =
        makeExecutor(trainingSpec, fixture.stateMachine->getGenomeRepository());
    const EvolutionConfig evolutionConfig = makeEvolutionConfig(5, 0.016);

    std::vector<EvaluationRequest> requests;
    for (int i = 0; i < 5; ++i) {
        requests.push_back(makeGenerationRequest(
            i, Scenario::EnumType::TreeGermination, static_cast<WeightType>(0.1f * (i + 1))));
    }

    executor.start(3);
    executor.generationBatchSubmit(requests, evolutionConfig, std::nullopt);

    EXPECT_EQ(executor.backgroundWorkerCountGet(), 2);
    EXPECT_EQ(executor.allowedConcurrencyGet(), 2);
    EXPECT_GT(executor.pendingVisibleCountGet(), 0u);
    EXPECT_LT(executor.pendingVisibleCountGet(), requests.size());

    int completedCount = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && completedCount < 5) {
        completedCount += static_cast<int>(executor.completedDrain().size());
        completedCount += static_cast<int>(
            executor.visibleTick(std::chrono::steady_clock::now(), 0).completed.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(completedCount, 5);
}

TEST(EvaluationExecutorTest, BackgroundResultsArriveWhileVisibleEvaluationRunning)
{
    TestStateMachineFixture fixture;
    const TrainingSpec trainingSpec =
        makeTrainingSpec(Scenario::EnumType::TreeGermination, OrganismType::TREE, 5);
    EvaluationExecutor executor =
        makeExecutor(trainingSpec, fixture.stateMachine->getGenomeRepository());
    const EvolutionConfig evolutionConfig = makeEvolutionConfig(5, 0.5);

    std::vector<EvaluationRequest> requests;
    for (int i = 0; i < 5; ++i) {
        requests.push_back(makeGenerationRequest(
            i, Scenario::EnumType::TreeGermination, static_cast<WeightType>(0.1f * (i + 1))));
    }

    executor.start(3);
    executor.generationBatchSubmit(requests, evolutionConfig, std::nullopt);
    executor.visibleTick(std::chrono::steady_clock::now(), 0);

    bool sawBackgroundCompletion = false;
    for (int i = 0; i < 200; ++i) {
        const bool visibleActive = executor.hasVisibleEvaluation();
        const auto backgroundResults = executor.completedDrain();
        if (!backgroundResults.empty() && visibleActive
            && executor.visibleSimTimeGet() < evolutionConfig.maxSimulationTime) {
            sawBackgroundCompletion = true;
            break;
        }

        executor.visibleTick(std::chrono::steady_clock::now(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(sawBackgroundCompletion);
}

TEST(EvaluationExecutorTest, DuckClockVisibleEvaluationWaitsForFourPassesBeforeCompletion)
{
    TestStateMachineFixture fixture;
    const TrainingSpec trainingSpec =
        makeTrainingSpec(Scenario::EnumType::Clock, OrganismType::DUCK, 1);
    EvaluationExecutor executor =
        makeExecutor(trainingSpec, fixture.stateMachine->getGenomeRepository());
    const EvolutionConfig evolutionConfig = makeEvolutionConfig(1, 0.0);
    const std::vector<EvaluationRequest> requests{
        makeDuckClockGenerationRequest(0),
    };

    executor.start(1);
    executor.generationBatchSubmit(requests, evolutionConfig, std::nullopt);

    for (int i = 0; i < 3; ++i) {
        const auto tick = executor.visibleTick(std::chrono::steady_clock::now(), 0);
        EXPECT_TRUE(tick.progressed);
        EXPECT_TRUE(tick.completed.empty());
    }

    const auto finalTick = executor.visibleTick(std::chrono::steady_clock::now(), 0);
    ASSERT_EQ(finalTick.completed.size(), 1u);
    EXPECT_EQ(finalTick.completed.front().index, 0);
}
