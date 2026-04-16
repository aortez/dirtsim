#include "EvaluationExecutor.h"

#include "core/Assert.h"
#include "core/PhysicsSettings.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/organisms/evolution/FitnessResult.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingRunner.h"
#include "core/scenarios/nes/NesTileTokenizer.h"
#include "core/scenarios/nes/NesTileTokenizerBootstrapper.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace DirtSim::Server::EvolutionSupport {

namespace {

constexpr size_t kTopCommandSignatureLimit = 20;

bool isDuckClockScenario(OrganismType organismType, Scenario::EnumType scenarioId)
{
    return organismType == OrganismType::DUCK && scenarioId == Scenario::EnumType::Clock;
}

bool requiresNesTileTokenizer(const EvaluationIndividual& individual)
{
    return individual.brainKind == TrainingBrainKind::NesTileRecurrent;
}

std::shared_ptr<NesTileTokenizer> resolveNesTileTokenizerForQueuedRequest(
    const EvaluationRequest& request,
    const std::optional<ScenarioConfig>& scenarioConfigOverride,
    std::shared_ptr<NesTileTokenizer>& sharedNesTileTokenizer,
    std::optional<Scenario::EnumType>& sharedNesTileScenarioId)
{
    if (!requiresNesTileTokenizer(request.individual)) {
        return nullptr;
    }

    if (sharedNesTileTokenizer) {
        DIRTSIM_ASSERT(
            sharedNesTileScenarioId == request.individual.scenarioId,
            "EvaluationExecutor: NES tile tokenizer cannot be shared across scenarios");
        return sharedNesTileTokenizer;
    }

    auto tokenizerResult =
        NesTileTokenizerBootstrapper::build(request.individual.scenarioId, scenarioConfigOverride);
    DIRTSIM_ASSERT(tokenizerResult.isValue(), tokenizerResult.errorValue());
    sharedNesTileTokenizer = std::move(tokenizerResult).value();
    sharedNesTileScenarioId = request.individual.scenarioId;
    return sharedNesTileTokenizer;
}

LightMode resolveVisibleLightMode(OrganismType organismType)
{
    return organismType == OrganismType::TREE ? LightMode::Fast : LightMode::FlatBasic;
}

std::optional<bool> resolveDuckClockSpawnSideOverride(
    EvaluationTaskType taskType,
    OrganismType organismType,
    Scenario::EnumType scenarioId,
    int robustSampleOrdinal)
{
    if (taskType != EvaluationTaskType::RobustnessEval) {
        return std::nullopt;
    }
    if (!isDuckClockScenario(organismType, scenarioId)) {
        return std::nullopt;
    }

    DIRTSIM_ASSERT(
        robustSampleOrdinal > 0,
        "EvaluationExecutor: robust sample ordinal must be positive for duck clock alternation");
    return (robustSampleOrdinal % 2) != 0;
}

std::optional<bool> resolvePrimaryDuckClockSpawnSide(
    EvaluationTaskType taskType,
    OrganismType organismType,
    Scenario::EnumType scenarioId,
    int robustSampleOrdinal)
{
    std::optional<bool> side =
        resolveDuckClockSpawnSideOverride(taskType, organismType, scenarioId, robustSampleOrdinal);
    if (isDuckClockScenario(organismType, scenarioId) && !side.has_value()) {
        side = true;
    }
    return side;
}

int duckClockPassCountForTask(EvaluationTaskType /*taskType*/)
{
    return 4;
}

std::optional<bool> resolveDuckClockSpawnSideForPass(
    std::optional<bool> primarySpawnSide, int passOrdinal)
{
    DIRTSIM_ASSERT(
        passOrdinal >= 0, "EvaluationExecutor: duck clock pass ordinal must be non-negative");
    DIRTSIM_ASSERT(
        primarySpawnSide.has_value(),
        "EvaluationExecutor: duck clock pass requires an explicit primary spawn side");
    const bool sideLeftFirst =
        (passOrdinal % 2) == 0 ? primarySpawnSide.value() : !primarySpawnSide.value();
    return sideLeftFirst;
}

TrainingRunner::Individual makeRunnerIndividual(const EvaluationIndividual& individual)
{
    TrainingRunner::Individual runner;
    runner.brain.brainKind = individual.brainKind;
    runner.brain.brainVariant = individual.brainVariant;
    runner.scenarioId = individual.scenarioId;
    runner.genome = individual.genome;
    return runner;
}

FitnessEvaluation computeFitnessEvaluationForRunner(
    const TrainingRunner& runner,
    const TrainingRunner::Status& status,
    OrganismType organismType,
    const EvolutionConfig& evolutionConfig,
    const FitnessModelBundle& fitnessModel)
{
    const World* world = runner.getWorld();
    if (organismType != OrganismType::NES_DUCK) {
        DIRTSIM_ASSERT(world != nullptr, "EvaluationExecutor: TrainingRunner missing World");
    }

    const FitnessResult result{
        .lifespan = status.lifespan,
        .maxEnergy = status.maxEnergy,
        .commandsAccepted = status.commandsAccepted,
        .commandsRejected = status.commandsRejected,
        .idleCancels = status.idleCancels,
        .nesRewardTotal = status.nesRewardTotal,
        .organismDied = status.state == TrainingRunner::State::OrganismDied,
        .exitedThroughDoor = status.exitedThroughDoor,
        .exitDoorTime = status.exitDoorTime,
    };

    const auto& treeResources = runner.getTreeResourceTotals();
    const TreeResourceTotals* treeResourcesPtr =
        treeResources.has_value() ? &treeResources.value() : nullptr;
    const auto& nesFitnessDetails = runner.getNesFitnessDetails();
    const auto* nesFitnessDetailsPtr =
        std::holds_alternative<std::monostate>(nesFitnessDetails) ? nullptr : &nesFitnessDetails;

    const FitnessContext context{
        .result = result,
        .organismType = organismType,
        .worldWidth = world ? world->getData().width : 0,
        .worldHeight = world ? world->getData().height : 0,
        .evolutionConfig = evolutionConfig,
        .finalOrganism = runner.getOrganism(),
        .duckArtifacts = runner.getDuckEvaluationArtifacts(),
        .nesFitnessDetails = nesFitnessDetailsPtr,
        .organismTrackingHistory = &runner.getOrganismTrackingHistory(),
        .treeResources = treeResourcesPtr,
    };
    return fitnessModel.evaluate(context);
}

std::unordered_map<std::string, EvaluationTimerAggregate> collectTimerStats(const Timers& timers)
{
    std::unordered_map<std::string, EvaluationTimerAggregate> stats;
    const auto names = timers.getAllTimerNames();
    stats.reserve(names.size());
    for (const auto& name : names) {
        EvaluationTimerAggregate entry;
        entry.totalMs = timers.getAccumulatedTime(name);
        entry.calls = timers.getCallCount(name);
        stats.emplace(name, entry);
    }
    return stats;
}

struct EvaluationPassResult {
    int commandsAccepted = 0;
    int commandsRejected = 0;
    FitnessEvaluation fitnessEvaluation;
    double simTime = 0.0;
    std::vector<std::pair<std::string, int>> topCommandSignatures;
    std::vector<std::pair<std::string, int>> topCommandOutcomeSignatures;
    std::optional<EvaluationSnapshot> snapshot;
    std::unordered_map<std::string, EvaluationTimerAggregate> timerStats;
};

struct QueuedEvaluation {
    EvaluationRequest request;
    EvolutionConfig evolutionConfig;
    std::shared_ptr<NesTileTokenizer> nesTileTokenizer = nullptr;
    std::optional<ScenarioConfig> scenarioConfigOverride = std::nullopt;
};

struct VisibleEvaluationHandle {
    mutable std::mutex mutex;
    QueuedEvaluation queued;
    std::optional<VisibleRenderFrame> frame;
    std::unordered_map<std::string, EvaluationTimerAggregate> timerStats;
    double simTime = 0.0;
    uint64_t progressRevision = 0;
    uint64_t deliveredProgressRevision = 0;
    TrainingRunner* runner = nullptr;
};

struct VisibleEvaluationState {
    std::mutex mutex;
    std::shared_ptr<VisibleEvaluationHandle> activeHandle;
    std::deque<CompletedEvaluation> completed;
    std::chrono::steady_clock::time_point lastStreamBroadcastTime{};

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex);
        activeHandle.reset();
        completed.clear();
        lastStreamBroadcastTime = std::chrono::steady_clock::time_point{};
    }
};

std::optional<EvaluationSnapshot> buildEvaluationSnapshotForRunner(const TrainingRunner& runner)
{
    const WorldData* worldData = runner.getWorldData();
    const std::vector<OrganismId>* organismGrid = runner.getOrganismGrid();
    if (!worldData || !organismGrid) {
        return std::nullopt;
    }

    EvaluationSnapshot snapshot;
    snapshot.worldData = *worldData;
    snapshot.organismIds = *organismGrid;
    snapshot.scenarioVideoFrame = runner.getScenarioVideoFrame();
    return snapshot;
}

EvaluationPassResult buildEvaluationPassResult(
    TrainingRunner& runner,
    const TrainingRunner::Status& status,
    OrganismType organismType,
    const EvolutionConfig& evolutionConfig,
    const FitnessModelBundle& fitnessModel,
    bool includeGenerationDetails)
{
    EvaluationPassResult pass;
    pass.commandsAccepted = status.commandsAccepted;
    pass.commandsRejected = status.commandsRejected;
    pass.simTime = status.simTime;

    pass.fitnessEvaluation = computeFitnessEvaluationForRunner(
        runner, status, organismType, evolutionConfig, fitnessModel);
    if (!includeGenerationDetails) {
        return pass;
    }

    pass.topCommandSignatures = runner.getTopCommandSignatures(kTopCommandSignatureLimit);
    pass.topCommandOutcomeSignatures =
        runner.getTopCommandOutcomeSignatures(kTopCommandSignatureLimit);
    if (const Timers* timers = runner.getTimers()) {
        pass.timerStats = collectTimerStats(*timers);
    }
    pass.snapshot = buildEvaluationSnapshotForRunner(runner);
    return pass;
}

std::optional<EvaluationPassResult> runEvaluationPass(
    const TrainingSpec& trainingSpec,
    const EvaluationIndividual& individual,
    const EvolutionConfig& evolutionConfig,
    GenomeRepository& genomeRepository,
    const TrainingBrainRegistry& brainRegistry,
    const std::optional<ScenarioConfig>& scenarioConfigOverride,
    const std::shared_ptr<NesTileTokenizer>& nesTileTokenizer,
    std::optional<bool> duckClockSpawnLeftFirst,
    const FitnessModelBundle& fitnessModel,
    bool includeGenerationDetails,
    const std::shared_ptr<VisibleEvaluationHandle>& visibleHandle,
    std::atomic<bool>* stopRequested,
    std::condition_variable* pauseCv,
    std::mutex* pauseMutex,
    bool* paused)
{
    const TrainingRunner::Config runnerConfig{
        .brainRegistry = brainRegistry,
        .nesTileTokenizer = nesTileTokenizer,
        .duckClockSpawnLeftFirst = duckClockSpawnLeftFirst,
        .duckClockSpawnRngSeed = std::nullopt,
        .nesRgbaOutputEnabled = visibleHandle != nullptr,
        .scenarioConfigOverride = scenarioConfigOverride,
    };
    TrainingRunner runner(
        trainingSpec,
        makeRunnerIndividual(individual),
        evolutionConfig,
        genomeRepository,
        runnerConfig);

    if (visibleHandle) {
        std::lock_guard<std::mutex> lock(visibleHandle->mutex);
        visibleHandle->runner = &runner;
    }

    if (World* world = runner.getWorld()) {
        applyLightModePreset(
            world->getPhysicsSettings().light, resolveVisibleLightMode(trainingSpec.organismType));
    }

    TrainingRunner::Status status;
    while (status.state == TrainingRunner::State::Running
           && !(stopRequested && stopRequested->load())) {
        if (pauseCv && pauseMutex && paused) {
            std::unique_lock<std::mutex> lock(*pauseMutex);
            pauseCv->wait(lock, [stopRequested, paused]() {
                return !*paused || (stopRequested && stopRequested->load());
            });
        }
        if (stopRequested && stopRequested->load()) {
            break;
        }
        status = runner.step(1);
        if (visibleHandle) {
            const WorldData* worldData = runner.getWorldData();
            const std::vector<OrganismId>* organismGrid = runner.getOrganismGrid();
            const auto videoFrame = runner.getScenarioVideoFrame();

            std::optional<VisibleRenderFrame> frame;
            if (worldData && organismGrid && (!runner.isNesScenario() || videoFrame.has_value())) {
                frame = VisibleRenderFrame{
                    .worldData = *worldData,
                    .organismIds = *organismGrid,
                    .scenarioId = individual.scenarioId,
                    .scenarioConfig = runner.getScenarioConfig(),
                    .nesControllerTelemetry = runner.getNesLastControllerTelemetry(),
                    .scenarioVideoFrame = videoFrame,
                };
            }

            std::lock_guard<std::mutex> lock(visibleHandle->mutex);
            if (visibleHandle->runner == &runner) {
                visibleHandle->frame = std::move(frame);
                visibleHandle->simTime = status.simTime;
                visibleHandle->timerStats = {};
                if (const Timers* timers = runner.getTimers()) {
                    visibleHandle->timerStats = collectTimerStats(*timers);
                }
                visibleHandle->progressRevision++;
            }
        }
    }

    if (visibleHandle) {
        std::lock_guard<std::mutex> lock(visibleHandle->mutex);
        if (visibleHandle->runner == &runner) {
            visibleHandle->runner = nullptr;
        }
    }

    if (stopRequested && stopRequested->load()) {
        return std::nullopt;
    }

    return buildEvaluationPassResult(
        runner,
        status,
        trainingSpec.organismType,
        evolutionConfig,
        fitnessModel,
        includeGenerationDetails);
}

void mergeTimerStats(
    std::unordered_map<std::string, EvaluationTimerAggregate>& target,
    const std::unordered_map<std::string, EvaluationTimerAggregate>& source)
{
    for (const auto& [name, aggregate] : source) {
        auto& merged = target[name];
        merged.totalMs += aggregate.totalMs;
        merged.calls += aggregate.calls;
    }
}

CompletedEvaluation buildCompletedEvaluationFromPass(
    const EvaluationRequest& request, EvaluationPassResult pass, bool includeGenerationDetails)
{
    CompletedEvaluation result;
    result.taskType = request.taskType;
    result.index = request.index;
    result.robustGeneration = request.robustGeneration;
    result.robustSampleOrdinal = request.robustSampleOrdinal;
    result.simTime = pass.simTime;
    result.commandsAccepted = pass.commandsAccepted;
    result.commandsRejected = pass.commandsRejected;
    result.fitnessEvaluation = std::move(pass.fitnessEvaluation);

    if (!includeGenerationDetails) {
        return result;
    }

    result.topCommandSignatures = std::move(pass.topCommandSignatures);
    result.topCommandOutcomeSignatures = std::move(pass.topCommandOutcomeSignatures);
    result.snapshot = std::move(pass.snapshot);
    result.timerStats = std::move(pass.timerStats);
    return result;
}

std::vector<std::pair<std::string, int>> mergeCommandSignatures(
    const std::vector<std::pair<std::string, int>>& first,
    const std::vector<std::pair<std::string, int>>& second)
{
    std::unordered_map<std::string, int> counts;
    counts.reserve(first.size() + second.size());

    const auto accumulate = [&counts](const std::vector<std::pair<std::string, int>>& entries) {
        for (const auto& [signature, count] : entries) {
            if (count <= 0) {
                continue;
            }
            counts[signature] += count;
        }
    };
    accumulate(first);
    accumulate(second);

    std::vector<std::pair<std::string, int>> merged;
    merged.reserve(counts.size());
    for (const auto& [signature, count] : counts) {
        merged.emplace_back(signature, count);
    }

    std::sort(merged.begin(), merged.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) {
            return lhs.second > rhs.second;
        }
        return lhs.first < rhs.first;
    });
    if (merged.size() > kTopCommandSignatureLimit) {
        merged.resize(kTopCommandSignatureLimit);
    }
    return merged;
}

const CompletedEvaluation& selectRepresentativeDuckClockPass(
    const CompletedEvaluation& first, const CompletedEvaluation& second, double targetFitness)
{
    const double firstDistance = std::abs(first.fitnessEvaluation.totalFitness - targetFitness);
    const double secondDistance = std::abs(second.fitnessEvaluation.totalFitness - targetFitness);
    if (firstDistance < secondDistance) {
        return first;
    }
    if (secondDistance < firstDistance) {
        return second;
    }
    return first.fitnessEvaluation.totalFitness <= second.fitnessEvaluation.totalFitness ? first
                                                                                         : second;
}

CompletedEvaluation mergeDuckClockGenerationPasses(
    const FitnessModelBundle& fitnessModel,
    const CompletedEvaluation& primaryPassOne,
    const CompletedEvaluation& oppositePassOne,
    const CompletedEvaluation& primaryPassTwo,
    const CompletedEvaluation& oppositePassTwo)
{
    const std::array<FitnessEvaluation, 4> passEvaluations{
        primaryPassOne.fitnessEvaluation,
        oppositePassOne.fitnessEvaluation,
        primaryPassTwo.fitnessEvaluation,
        oppositePassTwo.fitnessEvaluation,
    };
    const FitnessEvaluation mergedEvaluation = fitnessModel.mergePasses(passEvaluations);
    const double finalFitness = mergedEvaluation.totalFitness;
    const double primarySideAverage = 0.5
        * (primaryPassOne.fitnessEvaluation.totalFitness
           + primaryPassTwo.fitnessEvaluation.totalFitness);
    const double oppositeSideAverage = 0.5
        * (oppositePassOne.fitnessEvaluation.totalFitness
           + oppositePassTwo.fitnessEvaluation.totalFitness);
    const bool usePrimarySide = primarySideAverage <= oppositeSideAverage;

    const CompletedEvaluation& chosenFirst = usePrimarySide ? primaryPassOne : oppositePassOne;
    const CompletedEvaluation& chosenSecond = usePrimarySide ? primaryPassTwo : oppositePassTwo;
    const CompletedEvaluation& representative =
        selectRepresentativeDuckClockPass(chosenFirst, chosenSecond, finalFitness);

    CompletedEvaluation merged;
    merged.taskType = primaryPassOne.taskType;
    merged.index = primaryPassOne.index;
    merged.robustGeneration = primaryPassOne.robustGeneration;
    merged.robustSampleOrdinal = primaryPassOne.robustSampleOrdinal;
    merged.fitnessEvaluation = mergedEvaluation;
    merged.simTime = primaryPassOne.simTime + oppositePassOne.simTime + primaryPassTwo.simTime
        + oppositePassTwo.simTime;
    merged.commandsAccepted = chosenFirst.commandsAccepted + chosenSecond.commandsAccepted;
    merged.commandsRejected = chosenFirst.commandsRejected + chosenSecond.commandsRejected;
    merged.topCommandSignatures =
        mergeCommandSignatures(chosenFirst.topCommandSignatures, chosenSecond.topCommandSignatures);
    merged.topCommandOutcomeSignatures = mergeCommandSignatures(
        chosenFirst.topCommandOutcomeSignatures, chosenSecond.topCommandOutcomeSignatures);
    merged.snapshot = representative.snapshot;

    mergeTimerStats(merged.timerStats, primaryPassOne.timerStats);
    mergeTimerStats(merged.timerStats, oppositePassOne.timerStats);
    mergeTimerStats(merged.timerStats, primaryPassTwo.timerStats);
    mergeTimerStats(merged.timerStats, oppositePassTwo.timerStats);
    return merged;
}

} // namespace

struct EvaluationExecutor::Impl {
    explicit Impl(Config configIn) : config(std::move(configIn)) {}

    Config config;
    int backgroundWorkerCount = 0;
    int maxParallelEvaluations = 1;
    std::vector<std::thread> workers;
    std::deque<QueuedEvaluation> taskQueue;
    std::mutex taskMutex;
    std::condition_variable taskCv;
    std::deque<CompletedEvaluation> resultQueue;
    std::mutex resultMutex;
    bool paused = false;
    std::condition_variable pauseCv;
    std::mutex pauseMutex;
    std::atomic<bool> stopRequested{ false };
    std::atomic<int> allowedConcurrency{ 0 };
    std::atomic<int> activeEvaluations{ 0 };
    VisibleEvaluationState visible;
};

namespace {

std::shared_ptr<VisibleEvaluationHandle> visibleEvaluationTryClaim(
    EvaluationExecutor::Impl& impl, const QueuedEvaluation& queued)
{
    std::lock_guard<std::mutex> lock(impl.visible.mutex);
    if (impl.visible.activeHandle) {
        return nullptr;
    }

    auto handle = std::make_shared<VisibleEvaluationHandle>();
    handle->queued = queued;
    impl.visible.activeHandle = handle;
    impl.visible.lastStreamBroadcastTime = std::chrono::steady_clock::time_point{};
    return handle;
}

void visibleEvaluationComplete(
    EvaluationExecutor::Impl& impl,
    const std::shared_ptr<VisibleEvaluationHandle>& handle,
    CompletedEvaluation result)
{
    if (!handle) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        handle->runner = nullptr;
        handle->simTime = result.simTime;
        handle->timerStats = result.timerStats;
    }

    std::lock_guard<std::mutex> lock(impl.visible.mutex);
    if (impl.visible.activeHandle == handle) {
        impl.visible.activeHandle.reset();
    }
    impl.visible.completed.push_back(std::move(result));
    impl.visible.lastStreamBroadcastTime = std::chrono::steady_clock::time_point{};
}

void visibleEvaluationRelease(
    EvaluationExecutor::Impl& impl, const std::shared_ptr<VisibleEvaluationHandle>& handle)
{
    if (!handle) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(handle->mutex);
        handle->runner = nullptr;
    }

    std::lock_guard<std::mutex> lock(impl.visible.mutex);
    if (impl.visible.activeHandle == handle) {
        impl.visible.activeHandle.reset();
    }
}

QueuedEvaluation visibleQueuedSnapshot(
    const std::shared_ptr<VisibleEvaluationHandle>& handle, const QueuedEvaluation& fallback)
{
    if (!handle) {
        return fallback;
    }

    std::lock_guard<std::mutex> lock(handle->mutex);
    return handle->queued;
}

std::optional<CompletedEvaluation> runEvaluationTask(
    EvaluationExecutor::Impl& impl, QueuedEvaluation queued)
{
    DIRTSIM_ASSERT(queued.request.index >= 0, "EvaluationExecutor: Invalid evaluation index");
    DIRTSIM_ASSERT(
        impl.config.genomeRepository != nullptr, "EvaluationExecutor: GenomeRepository missing");

    const auto visibleHandle = visibleEvaluationTryClaim(impl, queued);
    const auto finishResult =
        [&](CompletedEvaluation result) -> std::optional<CompletedEvaluation> {
        if (!visibleHandle) {
            return std::optional<CompletedEvaluation>(std::move(result));
        }

        if (impl.stopRequested.load()) {
            visibleEvaluationRelease(impl, visibleHandle);
            return std::nullopt;
        }

        visibleEvaluationComplete(impl, visibleHandle, std::move(result));
        return std::nullopt;
    };

    const QueuedEvaluation initialQueued = visibleQueuedSnapshot(visibleHandle, queued);
    const bool includeGenerationDetails =
        initialQueued.request.taskType == EvaluationTaskType::GenerationEval;
    const std::optional<bool> primarySpawnSide = resolvePrimaryDuckClockSpawnSide(
        initialQueued.request.taskType,
        impl.config.trainingSpec.organismType,
        initialQueued.request.individual.scenarioId,
        initialQueued.request.robustSampleOrdinal);
    std::optional<EvaluationPassResult> primaryPass = runEvaluationPass(
        impl.config.trainingSpec,
        initialQueued.request.individual,
        initialQueued.evolutionConfig,
        *impl.config.genomeRepository,
        impl.config.brainRegistry,
        initialQueued.scenarioConfigOverride,
        initialQueued.nesTileTokenizer,
        primarySpawnSide,
        impl.config.fitnessModel,
        includeGenerationDetails,
        visibleHandle,
        &impl.stopRequested,
        &impl.pauseCv,
        &impl.pauseMutex,
        &impl.paused);
    if (!primaryPass.has_value()) {
        visibleEvaluationRelease(impl, visibleHandle);
        return std::nullopt;
    }

    CompletedEvaluation result = buildCompletedEvaluationFromPass(
        initialQueued.request, std::move(*primaryPass), includeGenerationDetails);
    if (!isDuckClockScenario(
            impl.config.trainingSpec.organismType, initialQueued.request.individual.scenarioId)) {
        return finishResult(std::move(result));
    }

    const int passCount = duckClockPassCountForTask(initialQueued.request.taskType);
    std::vector<CompletedEvaluation> passResults;
    passResults.reserve(static_cast<size_t>(passCount));
    passResults.push_back(std::move(result));

    for (int passOrdinal = 1; passOrdinal < passCount; ++passOrdinal) {
        const QueuedEvaluation passQueued = visibleQueuedSnapshot(visibleHandle, queued);
        const std::optional<bool> spawnSide =
            resolveDuckClockSpawnSideForPass(primarySpawnSide, passOrdinal);
        std::optional<EvaluationPassResult> pass = runEvaluationPass(
            impl.config.trainingSpec,
            passQueued.request.individual,
            passQueued.evolutionConfig,
            *impl.config.genomeRepository,
            impl.config.brainRegistry,
            passQueued.scenarioConfigOverride,
            passQueued.nesTileTokenizer,
            spawnSide,
            impl.config.fitnessModel,
            includeGenerationDetails,
            visibleHandle,
            &impl.stopRequested,
            &impl.pauseCv,
            &impl.pauseMutex,
            &impl.paused);
        if (!pass.has_value()) {
            visibleEvaluationRelease(impl, visibleHandle);
            return std::nullopt;
        }
        passResults.push_back(buildCompletedEvaluationFromPass(
            passQueued.request, std::move(*pass), includeGenerationDetails));
    }

    DIRTSIM_ASSERT(passCount == 4, "EvaluationExecutor: duck clock pass count must be 4");
    CompletedEvaluation merged = mergeDuckClockGenerationPasses(
        impl.config.fitnessModel, passResults[0], passResults[1], passResults[2], passResults[3]);
    return finishResult(std::move(merged));
}

QueuedEvaluation queuedEvaluationMake(
    const EvaluationRequest& request,
    const EvolutionConfig& evolutionConfig,
    const std::optional<ScenarioConfig>& scenarioConfigOverride,
    std::shared_ptr<NesTileTokenizer> nesTileTokenizer = nullptr)
{
    return QueuedEvaluation{
        .request = request,
        .evolutionConfig = evolutionConfig,
        .nesTileTokenizer = std::move(nesTileTokenizer),
        .scenarioConfigOverride = scenarioConfigOverride,
    };
}

} // namespace

EvaluationExecutor::EvaluationExecutor(Config config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{}

EvaluationExecutor::~EvaluationExecutor()
{
    stop();
}

bool EvaluationExecutor::isPaused() const
{
    std::lock_guard<std::mutex> lock(impl_->pauseMutex);
    return impl_->paused;
}

void EvaluationExecutor::pauseSet(bool paused)
{
    {
        std::lock_guard<std::mutex> lock(impl_->pauseMutex);
        impl_->paused = paused;
    }
    impl_->pauseCv.notify_all();
}

void EvaluationExecutor::start(int maxParallelEvaluations)
{
    stop();

    const int resolvedParallelEvaluations = std::max(1, maxParallelEvaluations);
    impl_->maxParallelEvaluations = resolvedParallelEvaluations;
    impl_->backgroundWorkerCount = resolvedParallelEvaluations;
    impl_->allowedConcurrency.store(impl_->backgroundWorkerCount);
    impl_->activeEvaluations.store(0);
    impl_->paused = false;
    impl_->stopRequested.store(false);
    impl_->visible.reset();

    impl_->workers.reserve(impl_->backgroundWorkerCount);
    Impl* state = impl_.get();
    for (int i = 0; i < impl_->backgroundWorkerCount; ++i) {
        impl_->workers.emplace_back([state]() {
            while (true) {
                QueuedEvaluation task;
                {
                    std::unique_lock<std::mutex> lock(state->taskMutex);
                    state->taskCv.wait(lock, [state]() {
                        return state->stopRequested
                            || (!state->taskQueue.empty()
                                && state->activeEvaluations.load()
                                    < state->allowedConcurrency.load());
                    });
                    if (state->stopRequested) {
                        return;
                    }
                    task = std::move(state->taskQueue.front());
                    state->taskQueue.pop_front();
                    state->activeEvaluations.fetch_add(1);
                }

                std::optional<CompletedEvaluation> result =
                    runEvaluationTask(*state, std::move(task));

                state->activeEvaluations.fetch_sub(1);
                state->taskCv.notify_one();

                if (state->stopRequested) {
                    return;
                }

                if (result.has_value()) {
                    std::lock_guard<std::mutex> lock(state->resultMutex);
                    state->resultQueue.push_back(std::move(*result));
                }
            }
        });
    }
}

void EvaluationExecutor::stop()
{
    if (!impl_) {
        return;
    }

    impl_->stopRequested.store(true);
    {
        std::lock_guard<std::mutex> lock(impl_->pauseMutex);
        impl_->paused = false;
    }
    impl_->pauseCv.notify_all();
    impl_->taskCv.notify_all();

    for (auto& worker : impl_->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    impl_->workers.clear();
    impl_->backgroundWorkerCount = 0;
    impl_->maxParallelEvaluations = 1;
    impl_->allowedConcurrency.store(0);
    impl_->activeEvaluations.store(0);

    {
        std::lock_guard<std::mutex> lock(impl_->taskMutex);
        impl_->taskQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->resultMutex);
        impl_->resultQueue.clear();
    }

    impl_->visible.reset();
    impl_->stopRequested.store(false);
}

void EvaluationExecutor::generationBatchSubmit(
    std::span<const EvaluationRequest> requests,
    const EvolutionConfig& evolutionConfig,
    const std::optional<ScenarioConfig>& scenarioConfigOverride)
{
    {
        std::lock_guard<std::mutex> lock(impl_->visible.mutex);
        DIRTSIM_ASSERT(
            !impl_->visible.activeHandle,
            "EvaluationExecutor: generation batch submission requires no active preview task");
    }

    std::vector<QueuedEvaluation> queuedEvaluations;
    queuedEvaluations.reserve(requests.size());
    std::shared_ptr<NesTileTokenizer> sharedNesTileTokenizer;
    std::optional<Scenario::EnumType> sharedNesTileScenarioId = std::nullopt;
    for (const auto& request : requests) {
        auto nesTileTokenizer = resolveNesTileTokenizerForQueuedRequest(
            request, scenarioConfigOverride, sharedNesTileTokenizer, sharedNesTileScenarioId);
        queuedEvaluations.push_back(queuedEvaluationMake(
            request, evolutionConfig, scenarioConfigOverride, std::move(nesTileTokenizer)));
    }

    {
        std::lock_guard<std::mutex> lock(impl_->taskMutex);
        impl_->taskQueue.clear();

        for (auto& queued : queuedEvaluations) {
            impl_->taskQueue.push_back(std::move(queued));
        }
    }

    impl_->taskCv.notify_all();
}

void EvaluationExecutor::robustnessPassSubmit(
    const EvaluationRequest& request,
    int targetEvalCount,
    const EvolutionConfig& evolutionConfig,
    const std::optional<ScenarioConfig>& scenarioConfigOverride)
{
    {
        std::lock_guard<std::mutex> lock(impl_->visible.mutex);
        DIRTSIM_ASSERT(
            !impl_->visible.activeHandle,
            "EvaluationExecutor: robustness submission requires no active preview task");
    }

    const int resolvedEvalCount = std::max(0, targetEvalCount);

    std::vector<QueuedEvaluation> queuedEvaluations;
    queuedEvaluations.reserve(static_cast<size_t>(resolvedEvalCount));
    std::shared_ptr<NesTileTokenizer> sharedNesTileTokenizer;
    std::optional<Scenario::EnumType> sharedNesTileScenarioId = std::nullopt;
    for (int sampleOrdinal = 1; sampleOrdinal <= resolvedEvalCount; ++sampleOrdinal) {
        EvaluationRequest sampleRequest = request;
        sampleRequest.robustSampleOrdinal = sampleOrdinal;
        auto nesTileTokenizer = resolveNesTileTokenizerForQueuedRequest(
            sampleRequest, scenarioConfigOverride, sharedNesTileTokenizer, sharedNesTileScenarioId);
        queuedEvaluations.push_back(queuedEvaluationMake(
            sampleRequest, evolutionConfig, scenarioConfigOverride, std::move(nesTileTokenizer)));
    }

    {
        std::lock_guard<std::mutex> lock(impl_->taskMutex);
        for (auto& queued : queuedEvaluations) {
            impl_->taskQueue.push_back(std::move(queued));
        }
    }

    impl_->taskCv.notify_all();
}

void EvaluationExecutor::queuedVisibleExecutionConfigSet(
    const EvolutionConfig& /*evolutionConfig*/,
    const std::optional<ScenarioConfig>& /*scenarioConfigOverride*/)
{}

std::optional<std::string> EvaluationExecutor::scenarioConfigOverrideSet(
    const std::optional<ScenarioConfig>& scenarioConfigOverride, Scenario::EnumType scenarioId)
{
    {
        std::lock_guard<std::mutex> lock(impl_->taskMutex);
        std::shared_ptr<NesTileTokenizer> sharedNesTileTokenizer = nullptr;
        const bool needsNesTileTokenizer = std::any_of(
            impl_->taskQueue.begin(), impl_->taskQueue.end(), [scenarioId](const auto& queued) {
                return queued.request.individual.scenarioId == scenarioId
                    && requiresNesTileTokenizer(queued.request.individual);
            });
        if (needsNesTileTokenizer) {
            auto tokenizerResult =
                NesTileTokenizerBootstrapper::build(scenarioId, scenarioConfigOverride);
            if (tokenizerResult.isError()) {
                return tokenizerResult.errorValue();
            }
            sharedNesTileTokenizer = std::move(tokenizerResult).value();
        }

        for (auto& queued : impl_->taskQueue) {
            if (queued.request.individual.scenarioId == scenarioId) {
                queued.scenarioConfigOverride = scenarioConfigOverride;
                queued.nesTileTokenizer = requiresNesTileTokenizer(queued.request.individual)
                    ? sharedNesTileTokenizer
                    : nullptr;
            }
        }
    }

    std::shared_ptr<VisibleEvaluationHandle> activeHandle;
    {
        std::lock_guard<std::mutex> lock(impl_->visible.mutex);
        activeHandle = impl_->visible.activeHandle;
    }

    if (!activeHandle) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(activeHandle->mutex);
    if (activeHandle->queued.request.individual.scenarioId != scenarioId) {
        return std::nullopt;
    }
    if (requiresNesTileTokenizer(activeHandle->queued.request.individual)) {
        return "EvaluationExecutor: Active NES tile evaluations cannot change scenario config";
    }

    activeHandle->queued.scenarioConfigOverride = scenarioConfigOverride;
    if (!scenarioConfigOverride.has_value() || !activeHandle->runner) {
        return std::nullopt;
    }

    const auto result = activeHandle->runner->setScenarioConfig(*scenarioConfigOverride);
    if (result.isError()) {
        return result.errorValue();
    }

    return std::nullopt;
}

std::vector<CompletedEvaluation> EvaluationExecutor::completedDrain()
{
    std::deque<CompletedEvaluation> results;
    {
        std::lock_guard<std::mutex> lock(impl_->resultMutex);
        results.swap(impl_->resultQueue);
    }

    std::vector<CompletedEvaluation> drained;
    drained.reserve(results.size());
    while (!results.empty()) {
        drained.push_back(std::move(results.front()));
        results.pop_front();
    }
    return drained;
}

std::unordered_map<std::string, EvaluationTimerAggregate> EvaluationExecutor::
    visibleTimerStatsCollect() const
{
    std::shared_ptr<VisibleEvaluationHandle> activeHandle;
    {
        std::lock_guard<std::mutex> lock(impl_->visible.mutex);
        activeHandle = impl_->visible.activeHandle;
    }

    if (!activeHandle) {
        return {};
    }

    std::lock_guard<std::mutex> lock(activeHandle->mutex);
    return activeHandle->timerStats;
}

VisibleTickResult EvaluationExecutor::visibleTick(
    std::chrono::steady_clock::time_point now, int streamIntervalMs)
{
    VisibleTickResult result;
    std::shared_ptr<VisibleEvaluationHandle> activeHandle;
    bool shouldBroadcast = false;
    {
        std::lock_guard<std::mutex> lock(impl_->visible.mutex);
        while (!impl_->visible.completed.empty()) {
            result.completed.push_back(std::move(impl_->visible.completed.front()));
            impl_->visible.completed.pop_front();
        }

        activeHandle = impl_->visible.activeHandle;
        if (activeHandle) {
            shouldBroadcast = true;
            if (streamIntervalMs > 0) {
                const auto interval = std::chrono::milliseconds(streamIntervalMs);
                if (impl_->visible.lastStreamBroadcastTime
                        != std::chrono::steady_clock::time_point{}
                    && now - impl_->visible.lastStreamBroadcastTime < interval) {
                    shouldBroadcast = false;
                }
                else {
                    impl_->visible.lastStreamBroadcastTime = now;
                }
            }
            else {
                impl_->visible.lastStreamBroadcastTime = now;
            }
        }
    }

    if (!activeHandle) {
        return result;
    }

    std::lock_guard<std::mutex> lock(activeHandle->mutex);
    result.progressed = activeHandle->progressRevision != activeHandle->deliveredProgressRevision;
    activeHandle->deliveredProgressRevision = activeHandle->progressRevision;
    if (shouldBroadcast && activeHandle->frame.has_value()) {
        result.frame = activeHandle->frame;
    }
    return result;
}

int EvaluationExecutor::activeEvaluationsGet() const
{
    return impl_->activeEvaluations.load();
}

int EvaluationExecutor::allowedConcurrencyGet() const
{
    return impl_->allowedConcurrency.load();
}

void EvaluationExecutor::allowedConcurrencySet(int allowedConcurrency)
{
    const int clamped = std::clamp(allowedConcurrency, 0, impl_->backgroundWorkerCount);
    impl_->allowedConcurrency.store(clamped);
    impl_->taskCv.notify_all();
}

int EvaluationExecutor::backgroundWorkerCountGet() const
{
    return impl_->backgroundWorkerCount;
}

bool EvaluationExecutor::hasVisibleEvaluation() const
{
    std::lock_guard<std::mutex> lock(impl_->visible.mutex);
    return impl_->visible.activeHandle != nullptr;
}

size_t EvaluationExecutor::pendingVisibleCountGet() const
{
    return 0;
}

double EvaluationExecutor::visibleSimTimeGet() const
{
    std::shared_ptr<VisibleEvaluationHandle> activeHandle;
    {
        std::lock_guard<std::mutex> lock(impl_->visible.mutex);
        activeHandle = impl_->visible.activeHandle;
    }

    if (!activeHandle) {
        return 0.0;
    }

    std::lock_guard<std::mutex> lock(activeHandle->mutex);
    return activeHandle->simTime;
}

} // namespace DirtSim::Server::EvolutionSupport
