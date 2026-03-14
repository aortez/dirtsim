#include "EvaluationExecutor.h"

#include "core/Assert.h"
#include "core/PhysicsSettings.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/organisms/evolution/FitnessResult.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingRunner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace DirtSim::Server::EvolutionSupport {

namespace {

constexpr size_t kTopCommandSignatureLimit = 20;

bool isDuckClockScenario(OrganismType organismType, Scenario::EnumType scenarioId)
{
    return organismType == OrganismType::DUCK && scenarioId == Scenario::EnumType::Clock;
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
        .duckStatsSnapshot = runner.getDuckStatsSnapshot(),
        .nesFitnessDetails = nesFitnessDetailsPtr,
        .organismTrackingHistory = &runner.getOrganismTrackingHistory(),
        .treeResources = treeResourcesPtr,
        .exitedThroughDoor = status.exitedThroughDoor,
        .exitDoorTime = status.exitDoorTime,
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

EvaluationPassResult runEvaluationPass(
    const TrainingSpec& trainingSpec,
    const EvaluationIndividual& individual,
    const EvolutionConfig& evolutionConfig,
    GenomeRepository& genomeRepository,
    const TrainingBrainRegistry& brainRegistry,
    const std::optional<ScenarioConfig>& scenarioConfigOverride,
    std::optional<bool> duckClockSpawnLeftFirst,
    const FitnessModelBundle& fitnessModel,
    bool includeGenerationDetails,
    std::atomic<bool>* stopRequested)
{
    const TrainingRunner::Config runnerConfig{
        .brainRegistry = brainRegistry,
        .duckClockSpawnLeftFirst = duckClockSpawnLeftFirst,
        .duckClockSpawnRngSeed = std::nullopt,
        .scenarioConfigOverride = scenarioConfigOverride,
    };
    TrainingRunner runner(
        trainingSpec,
        makeRunnerIndividual(individual),
        evolutionConfig,
        genomeRepository,
        runnerConfig);

    if (trainingSpec.scenarioId == Scenario::EnumType::Clock && runner.getWorld()) {
        runner.getWorld()->getPhysicsSettings().light.enabled = false;
    }

    TrainingRunner::Status status;
    while (status.state == TrainingRunner::State::Running
           && !(stopRequested && stopRequested->load())) {
        status = runner.step(1);
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

struct QueuedEvaluation {
    EvaluationRequest request;
    EvolutionConfig evolutionConfig;
    std::optional<ScenarioConfig> scenarioConfigOverride = std::nullopt;
};

struct VisibleEvaluationState {
    std::optional<QueuedEvaluation> activeRequest;
    std::optional<bool> duckPrimarySpawnLeftFirst = std::nullopt;
    std::vector<CompletedEvaluation> duckPassResults;
    std::deque<QueuedEvaluation> queue;
    std::unique_ptr<TrainingRunner> runner;

    void clearActive()
    {
        activeRequest.reset();
        duckPassResults.clear();
        duckPrimarySpawnLeftFirst.reset();
        runner.reset();
    }

    void reset()
    {
        clearActive();
        queue.clear();
    }
};

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
    std::atomic<bool> stopRequested{ false };
    std::atomic<int> allowedConcurrency{ 0 };
    std::atomic<int> activeEvaluations{ 0 };
    VisibleEvaluationState visible;
    std::chrono::steady_clock::time_point lastStreamBroadcastTime_{};
};

namespace {

void visibleRunnerStart(
    EvaluationExecutor::Impl& impl,
    const QueuedEvaluation& queued,
    std::optional<bool> duckClockSpawnLeftFirst)
{
    DIRTSIM_ASSERT(
        impl.config.genomeRepository != nullptr,
        "EvaluationExecutor: GenomeRepository missing for visible runner");

    const TrainingRunner::Config runnerConfig{
        .brainRegistry = impl.config.brainRegistry,
        .duckClockSpawnLeftFirst = duckClockSpawnLeftFirst,
        .duckClockSpawnRngSeed = std::nullopt,
        .scenarioConfigOverride = queued.scenarioConfigOverride,
    };
    impl.visible.activeRequest = queued;
    impl.visible.runner = std::make_unique<TrainingRunner>(
        impl.config.trainingSpec,
        makeRunnerIndividual(queued.request.individual),
        queued.evolutionConfig,
        *impl.config.genomeRepository,
        runnerConfig);
}

void visibleEvaluationStartNext(EvaluationExecutor::Impl& impl)
{
    if (impl.visible.runner || impl.visible.queue.empty()) {
        return;
    }

    const QueuedEvaluation queued = impl.visible.queue.front();
    impl.visible.queue.pop_front();
    impl.visible.duckPassResults.clear();

    const std::optional<bool> primarySpawnSide = resolvePrimaryDuckClockSpawnSide(
        queued.request.taskType,
        impl.config.trainingSpec.organismType,
        queued.request.individual.scenarioId,
        queued.request.robustSampleOrdinal);
    impl.visible.duckPrimarySpawnLeftFirst = primarySpawnSide;
    visibleRunnerStart(impl, queued, primarySpawnSide);
}

std::optional<VisibleRenderFrame> visibleFrameBuild(const EvaluationExecutor::Impl& impl)
{
    if (!impl.visible.runner || !impl.visible.activeRequest.has_value()) {
        return std::nullopt;
    }

    const WorldData* worldData = impl.visible.runner->getWorldData();
    const std::vector<OrganismId>* organismGrid = impl.visible.runner->getOrganismGrid();
    DIRTSIM_ASSERT(worldData != nullptr, "EvaluationExecutor: Visible runner missing WorldData");
    DIRTSIM_ASSERT(
        organismGrid != nullptr, "EvaluationExecutor: Visible runner missing organism grid");

    const auto& videoFrame = impl.visible.runner->getScenarioVideoFrame();
    if (impl.visible.runner->isNesScenario() && !videoFrame.has_value()) {
        return std::nullopt;
    }

    VisibleRenderFrame frame;
    frame.worldData = *worldData;
    frame.organismIds = *organismGrid;
    frame.scenarioId = impl.visible.activeRequest->request.individual.scenarioId;
    frame.scenarioConfig = impl.visible.runner->getScenarioConfig();
    frame.nesControllerTelemetry = impl.visible.runner->getNesLastControllerTelemetry();
    frame.scenarioVideoFrame = videoFrame;
    return frame;
}

std::vector<CompletedEvaluation> visibleEvaluationComplete(
    EvaluationExecutor::Impl& impl, const TrainingRunner::Status& status)
{
    DIRTSIM_ASSERT(
        impl.visible.runner && impl.visible.activeRequest.has_value(),
        "EvaluationExecutor: visible completion requires active request");

    const QueuedEvaluation request = impl.visible.activeRequest.value();
    const bool includeGenerationDetails =
        request.request.taskType == EvaluationTaskType::GenerationEval;
    EvaluationPassResult completedPass = buildEvaluationPassResult(
        *impl.visible.runner,
        status,
        impl.config.trainingSpec.organismType,
        request.evolutionConfig,
        impl.config.fitnessModel,
        includeGenerationDetails);
    CompletedEvaluation passResult = buildCompletedEvaluationFromPass(
        request.request, std::move(completedPass), includeGenerationDetails);

    const bool duckClockVisibleEval = isDuckClockScenario(
        impl.config.trainingSpec.organismType, request.request.individual.scenarioId);
    if (!duckClockVisibleEval) {
        impl.visible.clearActive();
        return { std::move(passResult) };
    }

    impl.visible.duckPassResults.push_back(std::move(passResult));
    const int passCount = duckClockPassCountForTask(request.request.taskType);
    if (static_cast<int>(impl.visible.duckPassResults.size()) < passCount) {
        const int nextPassOrdinal = static_cast<int>(impl.visible.duckPassResults.size());
        const std::optional<bool> spawnSide = resolveDuckClockSpawnSideForPass(
            impl.visible.duckPrimarySpawnLeftFirst, nextPassOrdinal);
        visibleRunnerStart(impl, request, spawnSide);
        return {};
    }

    DIRTSIM_ASSERT(
        passCount == 4 && impl.visible.duckPassResults.size() == 4,
        "EvaluationExecutor: duck clock evaluation must complete 4 passes");
    CompletedEvaluation result = mergeDuckClockGenerationPasses(
        impl.config.fitnessModel,
        impl.visible.duckPassResults[0],
        impl.visible.duckPassResults[1],
        impl.visible.duckPassResults[2],
        impl.visible.duckPassResults[3]);
    impl.visible.clearActive();
    return { std::move(result) };
}

CompletedEvaluation runEvaluationTask(
    EvaluationExecutor::Impl& impl, const QueuedEvaluation& queued)
{
    DIRTSIM_ASSERT(queued.request.index >= 0, "EvaluationExecutor: Invalid evaluation index");
    DIRTSIM_ASSERT(
        impl.config.genomeRepository != nullptr, "EvaluationExecutor: GenomeRepository missing");

    const bool includeGenerationDetails =
        queued.request.taskType == EvaluationTaskType::GenerationEval;
    const std::optional<bool> primarySpawnSide = resolvePrimaryDuckClockSpawnSide(
        queued.request.taskType,
        impl.config.trainingSpec.organismType,
        queued.request.individual.scenarioId,
        queued.request.robustSampleOrdinal);
    EvaluationPassResult primaryPass = runEvaluationPass(
        impl.config.trainingSpec,
        queued.request.individual,
        queued.evolutionConfig,
        *impl.config.genomeRepository,
        impl.config.brainRegistry,
        queued.scenarioConfigOverride,
        primarySpawnSide,
        impl.config.fitnessModel,
        includeGenerationDetails,
        &impl.stopRequested);

    CompletedEvaluation result = buildCompletedEvaluationFromPass(
        queued.request, std::move(primaryPass), includeGenerationDetails);
    if (!isDuckClockScenario(
            impl.config.trainingSpec.organismType, queued.request.individual.scenarioId)) {
        return result;
    }

    const int passCount = duckClockPassCountForTask(queued.request.taskType);
    std::vector<CompletedEvaluation> passResults;
    passResults.reserve(static_cast<size_t>(passCount));
    passResults.push_back(std::move(result));

    for (int passOrdinal = 1; passOrdinal < passCount; ++passOrdinal) {
        const std::optional<bool> spawnSide =
            resolveDuckClockSpawnSideForPass(primarySpawnSide, passOrdinal);
        EvaluationPassResult pass = runEvaluationPass(
            impl.config.trainingSpec,
            queued.request.individual,
            queued.evolutionConfig,
            *impl.config.genomeRepository,
            impl.config.brainRegistry,
            queued.scenarioConfigOverride,
            spawnSide,
            impl.config.fitnessModel,
            includeGenerationDetails,
            &impl.stopRequested);
        passResults.push_back(buildCompletedEvaluationFromPass(
            queued.request, std::move(pass), includeGenerationDetails));
    }

    DIRTSIM_ASSERT(passCount == 4, "EvaluationExecutor: duck clock pass count must be 4");
    return mergeDuckClockGenerationPasses(
        impl.config.fitnessModel, passResults[0], passResults[1], passResults[2], passResults[3]);
}

QueuedEvaluation queuedEvaluationMake(
    const EvaluationRequest& request,
    const EvolutionConfig& evolutionConfig,
    const std::optional<ScenarioConfig>& scenarioConfigOverride)
{
    return QueuedEvaluation{
        .request = request,
        .evolutionConfig = evolutionConfig,
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

void EvaluationExecutor::start(int maxParallelEvaluations)
{
    stop();

    const int resolvedParallelEvaluations = std::max(1, maxParallelEvaluations);
    impl_->maxParallelEvaluations = resolvedParallelEvaluations;
    impl_->backgroundWorkerCount = std::max(0, resolvedParallelEvaluations - 1);
    impl_->allowedConcurrency.store(impl_->backgroundWorkerCount);
    impl_->activeEvaluations.store(0);
    impl_->stopRequested.store(false);
    impl_->visible.reset();

    if (impl_->backgroundWorkerCount <= 0) {
        return;
    }

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

                CompletedEvaluation result = runEvaluationTask(*state, task);

                state->activeEvaluations.fetch_sub(1);
                state->taskCv.notify_one();

                if (state->stopRequested) {
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(state->resultMutex);
                    state->resultQueue.push_back(std::move(result));
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
    impl_->lastStreamBroadcastTime_ = std::chrono::steady_clock::time_point{};
    impl_->stopRequested.store(false);
}

void EvaluationExecutor::generationBatchSubmit(
    std::span<const EvaluationRequest> requests,
    const EvolutionConfig& evolutionConfig,
    const std::optional<ScenarioConfig>& scenarioConfigOverride)
{
    DIRTSIM_ASSERT(
        !impl_->visible.runner,
        "EvaluationExecutor: generation batch submission requires no active visible runner");
    impl_->visible.reset();

    {
        std::lock_guard<std::mutex> lock(impl_->taskMutex);
        impl_->taskQueue.clear();

        const int totalWorkers = std::max(1, impl_->maxParallelEvaluations);
        for (size_t i = 0; i < requests.size(); ++i) {
            const QueuedEvaluation queued =
                queuedEvaluationMake(requests[i], evolutionConfig, scenarioConfigOverride);
            if (totalWorkers == 1 || (static_cast<int>(i) % totalWorkers) == 0) {
                impl_->visible.queue.push_back(queued);
            }
            else {
                impl_->taskQueue.push_back(queued);
            }
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
    DIRTSIM_ASSERT(
        !impl_->visible.runner,
        "EvaluationExecutor: robustness submission requires no active visible runner");

    const int resolvedEvalCount = std::max(0, targetEvalCount);
    const int visibleSampleCount =
        resolvedEvalCount > 0 ? (impl_->backgroundWorkerCount > 0 ? 1 : resolvedEvalCount) : 0;

    for (int sampleOrdinal = 1; sampleOrdinal <= visibleSampleCount; ++sampleOrdinal) {
        EvaluationRequest sampleRequest = request;
        sampleRequest.robustSampleOrdinal = sampleOrdinal;
        impl_->visible.queue.push_back(
            queuedEvaluationMake(sampleRequest, evolutionConfig, scenarioConfigOverride));
    }

    {
        std::lock_guard<std::mutex> lock(impl_->taskMutex);
        for (int sampleOrdinal = visibleSampleCount + 1; sampleOrdinal <= resolvedEvalCount;
             ++sampleOrdinal) {
            EvaluationRequest sampleRequest = request;
            sampleRequest.robustSampleOrdinal = sampleOrdinal;
            impl_->taskQueue.push_back(
                queuedEvaluationMake(sampleRequest, evolutionConfig, scenarioConfigOverride));
        }
    }

    impl_->taskCv.notify_all();
}

void EvaluationExecutor::queuedVisibleExecutionConfigSet(
    const EvolutionConfig& evolutionConfig,
    const std::optional<ScenarioConfig>& scenarioConfigOverride)
{
    for (auto& queued : impl_->visible.queue) {
        queued.evolutionConfig = evolutionConfig;
        queued.scenarioConfigOverride = scenarioConfigOverride;
    }
}

std::optional<std::string> EvaluationExecutor::scenarioConfigOverrideSet(
    const std::optional<ScenarioConfig>& scenarioConfigOverride, Scenario::EnumType scenarioId)
{
    for (auto& queued : impl_->visible.queue) {
        if (queued.request.individual.scenarioId == scenarioId) {
            queued.scenarioConfigOverride = scenarioConfigOverride;
        }
    }

    {
        std::lock_guard<std::mutex> lock(impl_->taskMutex);
        for (auto& queued : impl_->taskQueue) {
            if (queued.request.individual.scenarioId == scenarioId) {
                queued.scenarioConfigOverride = scenarioConfigOverride;
            }
        }
    }

    if (!impl_->visible.runner || !impl_->visible.activeRequest.has_value()
        || impl_->visible.activeRequest->request.individual.scenarioId != scenarioId) {
        return std::nullopt;
    }

    impl_->visible.activeRequest->scenarioConfigOverride = scenarioConfigOverride;
    if (!scenarioConfigOverride.has_value()) {
        return std::nullopt;
    }

    const auto result = impl_->visible.runner->setScenarioConfig(*scenarioConfigOverride);
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
    if (!impl_->visible.runner) {
        return {};
    }

    const Timers* timers = impl_->visible.runner->getTimers();
    if (!timers) {
        return {};
    }

    return collectTimerStats(*timers);
}

VisibleTickResult EvaluationExecutor::visibleTick(
    std::chrono::steady_clock::time_point now, int streamIntervalMs)
{
    VisibleTickResult result;

    visibleEvaluationStartNext(*impl_);
    if (!impl_->visible.runner) {
        return result;
    }

    const TrainingRunner::Status status = impl_->visible.runner->step(1);
    result.progressed = true;

    bool shouldBroadcast = true;
    if (streamIntervalMs > 0) {
        const auto interval = std::chrono::milliseconds(streamIntervalMs);
        if (impl_->lastStreamBroadcastTime_ != std::chrono::steady_clock::time_point{}
            && now - impl_->lastStreamBroadcastTime_ < interval) {
            shouldBroadcast = false;
        }
        else {
            impl_->lastStreamBroadcastTime_ = now;
        }
    }
    else {
        impl_->lastStreamBroadcastTime_ = now;
    }

    if (shouldBroadcast) {
        result.frame = visibleFrameBuild(*impl_);
    }

    if (status.state == TrainingRunner::State::Running) {
        return result;
    }

    result.completed = visibleEvaluationComplete(*impl_, status);
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
    return impl_->visible.runner != nullptr;
}

size_t EvaluationExecutor::pendingVisibleCountGet() const
{
    return impl_->visible.queue.size();
}

double EvaluationExecutor::visibleSimTimeGet() const
{
    return impl_->visible.runner ? impl_->visible.runner->getSimTime() : 0.0;
}

} // namespace DirtSim::Server::EvolutionSupport
