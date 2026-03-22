#include "core/organisms/brains/DuckNeuralNetRecurrentBrainV2.h"
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

bool hasLitAirCell(const WorldData& data)
{
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const Cell& cell = data.at(x, y);
            if (!cell.isEmpty()) {
                continue;
            }
            if (ColorNames::brightness(data.colors.at(x, y)) > 0.02f) {
                return true;
            }
        }
    }
    return false;
}

bool allAirCellsBlack(const WorldData& data)
{
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const Cell& cell = data.at(x, y);
            if (!cell.isEmpty()) {
                continue;
            }
            if (ColorNames::toRgba(data.colors.at(x, y)) != ColorNames::black()) {
                return false;
            }
        }
    }
    return true;
}

bool hasLitSolidCell(const WorldData& data)
{
    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            const Cell& cell = data.at(x, y);
            if (cell.isEmpty()) {
                continue;
            }
            if (ColorNames::brightness(data.colors.at(x, y)) > 0.02f) {
                return true;
            }
        }
    }
    return false;
}

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
                .brainKind = TrainingBrainKind::DuckNeuralNetRecurrentV2,
                .brainVariant = std::nullopt,
                .scenarioId = Scenario::EnumType::Clock,
                .genome = DuckNeuralNetRecurrentBrainV2::randomGenome(rng),
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

int completedCountDrain(EvaluationExecutor& executor)
{
    int completedCount = static_cast<int>(executor.completedDrain().size());
    completedCount += static_cast<int>(
        executor.visibleTick(std::chrono::steady_clock::now(), 0).completed.size());
    return completedCount;
}

bool waitForVisibleSimTime(EvaluationExecutor& executor, double minSimTime, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        executor.visibleTick(std::chrono::steady_clock::now(), 0);
        if (executor.visibleSimTimeGet() >= minSimTime) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

double waitForVisibleSimTimeStable(EvaluationExecutor& executor, int timeoutMs)
{
    double stableSimTime = executor.visibleSimTimeGet();
    auto stableSince = std::chrono::steady_clock::now();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        executor.visibleTick(std::chrono::steady_clock::now(), 0);
        const double currentSimTime = executor.visibleSimTimeGet();
        if (currentSimTime != stableSimTime) {
            stableSimTime = currentSimTime;
            stableSince = std::chrono::steady_clock::now();
        }
        else if (std::chrono::steady_clock::now() - stableSince >= std::chrono::milliseconds(20)) {
            return stableSimTime;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return stableSimTime;
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

    bool sawVisibleEvaluation = false;
    bool sawProgress = false;
    std::vector<CompletedEvaluation> completed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && completed.empty()) {
        const auto tick = executor.visibleTick(std::chrono::steady_clock::now(), 0);
        sawProgress = sawProgress || tick.progressed;
        sawVisibleEvaluation = sawVisibleEvaluation || executor.hasVisibleEvaluation();
        if (!tick.completed.empty()) {
            completed = std::move(tick.completed);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(sawVisibleEvaluation);
    EXPECT_TRUE(sawProgress);
    ASSERT_EQ(completed.size(), 1u);
    EXPECT_FALSE(executor.hasVisibleEvaluation());
    EXPECT_EQ(completed.front().index, 0);
}

TEST(EvaluationExecutorTest, TreeVisibleEvaluationUsesPropagatedLightingForAirCells)
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

    std::optional<VisibleRenderFrame> frame;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !frame.has_value()) {
        const auto tick = executor.visibleTick(std::chrono::steady_clock::now(), 0);
        if (tick.frame.has_value()) {
            frame = tick.frame;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(hasLitAirCell(frame->worldData));
}

TEST(EvaluationExecutorTest, GenerationBatchQueuesAllEvaluationsInWorkerPool)
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

    EXPECT_EQ(executor.backgroundWorkerCountGet(), 3);
    EXPECT_EQ(executor.allowedConcurrencyGet(), 3);
    EXPECT_EQ(executor.pendingVisibleCountGet(), 0u);

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

TEST(EvaluationExecutorTest, PreviewCoexistsWithBackgroundWork)
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

    bool sawBackgroundCompletion = false;
    bool sawVisibleEvaluation = false;
    for (int i = 0; i < 400; ++i) {
        executor.visibleTick(std::chrono::steady_clock::now(), 0);
        sawVisibleEvaluation = sawVisibleEvaluation || executor.hasVisibleEvaluation();
        const auto backgroundResults = executor.completedDrain();
        if (!backgroundResults.empty()) {
            sawBackgroundCompletion = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(sawVisibleEvaluation);
    EXPECT_TRUE(sawBackgroundCompletion);
}

TEST(EvaluationExecutorTest, DuckClockPreviewCompletesAsSingleMergedEvaluation)
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

    int completedCount = 0;
    int backgroundCount = 0;
    int previewCount = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && completedCount < 1) {
        const auto tick = executor.visibleTick(std::chrono::steady_clock::now(), 0);
        previewCount += static_cast<int>(tick.completed.size());
        backgroundCount += static_cast<int>(executor.completedDrain().size());
        completedCount = previewCount + backgroundCount;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(completedCount, 1);
    EXPECT_EQ(backgroundCount, 0);
    EXPECT_EQ(previewCount, 1);
}

TEST(EvaluationExecutorTest, DuckClockVisibleEvaluationUsesFlatBasicPreviewLighting)
{
    TestStateMachineFixture fixture;
    const TrainingSpec trainingSpec =
        makeTrainingSpec(Scenario::EnumType::Clock, OrganismType::DUCK, 1);
    EvaluationExecutor executor =
        makeExecutor(trainingSpec, fixture.stateMachine->getGenomeRepository());
    const EvolutionConfig evolutionConfig = makeEvolutionConfig(1, 0.1);
    const std::vector<EvaluationRequest> requests{
        makeDuckClockGenerationRequest(0),
    };

    executor.start(1);
    executor.generationBatchSubmit(requests, evolutionConfig, std::nullopt);

    std::optional<VisibleRenderFrame> frame;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline && !frame.has_value()) {
        const auto tick = executor.visibleTick(std::chrono::steady_clock::now(), 0);
        if (tick.frame.has_value()) {
            frame = tick.frame;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(allAirCellsBlack(frame->worldData));
    EXPECT_TRUE(hasLitSolidCell(frame->worldData));
}

TEST(EvaluationExecutorTest, VisibleEvaluationDoesNotAdvanceWhilePaused)
{
    TestStateMachineFixture fixture;
    const TrainingSpec trainingSpec =
        makeTrainingSpec(Scenario::EnumType::TreeGermination, OrganismType::TREE, 1);
    EvaluationExecutor executor =
        makeExecutor(trainingSpec, fixture.stateMachine->getGenomeRepository());
    const EvolutionConfig evolutionConfig = makeEvolutionConfig(1, 1.0);
    const std::vector<EvaluationRequest> requests{
        makeGenerationRequest(0, Scenario::EnumType::TreeGermination, 0.1f),
    };

    executor.start(1);
    executor.generationBatchSubmit(requests, evolutionConfig, std::nullopt);

    ASSERT_TRUE(waitForVisibleSimTime(executor, 0.01, 2000));

    executor.pauseSet(true);
    const double pausedSimTime = waitForVisibleSimTimeStable(executor, 100);
    const auto pauseDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    int completedWhilePaused = 0;
    while (std::chrono::steady_clock::now() < pauseDeadline) {
        completedWhilePaused += completedCountDrain(executor);
        EXPECT_NEAR(executor.visibleSimTimeGet(), pausedSimTime, 1e-9);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_EQ(completedWhilePaused, 0);

    executor.pauseSet(false);

    bool resumed = false;
    const auto resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < resumeDeadline) {
        const double visibleSimTime = executor.visibleSimTimeGet();
        const int completed = completedCountDrain(executor);
        if (visibleSimTime > pausedSimTime || completed > 0) {
            resumed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(resumed);
}

TEST(EvaluationExecutorTest, BackgroundEvaluationsDoNotCompleteWhilePaused)
{
    TestStateMachineFixture fixture;
    const TrainingSpec trainingSpec =
        makeTrainingSpec(Scenario::EnumType::TreeGermination, OrganismType::TREE, 2);
    EvaluationExecutor executor =
        makeExecutor(trainingSpec, fixture.stateMachine->getGenomeRepository());
    const EvolutionConfig evolutionConfig = makeEvolutionConfig(2, 2.0);
    const std::vector<EvaluationRequest> requests{
        makeGenerationRequest(0, Scenario::EnumType::TreeGermination, 0.1f),
        makeGenerationRequest(1, Scenario::EnumType::TreeGermination, 0.2f),
    };

    executor.start(2);
    executor.generationBatchSubmit(requests, evolutionConfig, std::nullopt);

    const auto activeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < activeDeadline) {
        executor.visibleTick(std::chrono::steady_clock::now(), 0);
        if (executor.activeEvaluationsGet() == 2 && executor.visibleSimTimeGet() > 0.0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_EQ(executor.activeEvaluationsGet(), 2);

    executor.pauseSet(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int completedWhilePaused = 0;
    const auto pauseDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (std::chrono::steady_clock::now() < pauseDeadline) {
        completedWhilePaused += completedCountDrain(executor);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_EQ(completedWhilePaused, 0);

    executor.pauseSet(false);

    int completedAfterResume = 0;
    const auto resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < resumeDeadline && completedAfterResume < 2) {
        completedAfterResume += completedCountDrain(executor);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(completedAfterResume, 2);
}
