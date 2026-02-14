#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/SystemMetrics.h"
#include "core/Timers.h"
#include "core/UUID.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/evolution/FitnessCalculator.h"
#include "core/organisms/evolution/FitnessResult.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/Mutation.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/StateMachine.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/EvolutionStop.h"
#include "server/api/TrainingBestSnapshot.h"
#include "server/api/TrainingResult.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <limits>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Server {
namespace State {

namespace {
constexpr auto kProgressBroadcastInterval = std::chrono::milliseconds(100);

int tournamentSelectIndex(const std::vector<double>& fitness, int tournamentSize, std::mt19937& rng)
{
    DIRTSIM_ASSERT(!fitness.empty(), "Tournament selection requires non-empty fitness list");
    DIRTSIM_ASSERT(tournamentSize > 0, "Tournament size must be positive");

    std::uniform_int_distribution<int> dist(0, static_cast<int>(fitness.size()) - 1);

    int bestIdx = dist(rng);
    double bestFitness = fitness[bestIdx];

    for (int i = 1; i < tournamentSize; ++i) {
        const int idx = dist(rng);
        if (fitness[idx] > bestFitness) {
            bestIdx = idx;
            bestFitness = fitness[idx];
        }
    }

    return bestIdx;
}

int resolveParallelEvaluations(int requested, int populationSize)
{
    int resolved = requested;
    if (resolved <= 0) {
        const unsigned int cores = std::thread::hardware_concurrency();
        resolved = cores > 0 ? static_cast<int>(cores) : 1;
    }

    if (resolved < 1) {
        resolved = 1;
    }
    if (populationSize > 0 && resolved > populationSize) {
        resolved = populationSize;
    }
    return resolved;
}

TrainingRunner::Individual makeRunnerIndividual(const Evolution::Individual& individual)
{
    TrainingRunner::Individual runner;
    runner.brain.brainKind = individual.brainKind;
    runner.brain.brainVariant = individual.brainVariant;
    runner.scenarioId = individual.scenarioId;
    runner.genome = individual.genome;
    return runner;
}

double computeFitnessForRunner(
    const TrainingRunner& runner,
    const TrainingRunner::Status& status,
    OrganismType organismType,
    const EvolutionConfig& evolutionConfig,
    std::optional<TreeFitnessBreakdown>* breakdownOut)
{
    const World* world = runner.getWorld();
    DIRTSIM_ASSERT(world != nullptr, "Evolution: TrainingRunner missing World");

    const FitnessResult result{
        .lifespan = status.lifespan,
        .distanceTraveled = status.distanceTraveled,
        .maxEnergy = status.maxEnergy,
        .commandsAccepted = status.commandsAccepted,
        .commandsRejected = status.commandsRejected,
    };

    const auto& treeResources = runner.getTreeResourceTotals();
    const TreeResourceTotals* treeResourcesPtr =
        treeResources.has_value() ? &treeResources.value() : nullptr;

    const FitnessContext context{
        .result = result,
        .organismType = organismType,
        .worldWidth = world->getData().width,
        .worldHeight = world->getData().height,
        .evolutionConfig = evolutionConfig,
        .finalOrganism = runner.getOrganism(),
        .treeResources = treeResourcesPtr,
    };

    if (organismType == OrganismType::TREE) {
        TreeFitnessBreakdown breakdown = TreeEvaluator::evaluateWithBreakdown(context);
        if (breakdownOut) {
            *breakdownOut = breakdown;
        }
        return breakdown.totalFitness;
    }

    if (breakdownOut) {
        breakdownOut->reset();
    }

    return computeFitnessForOrganism(context);
}

Scenario::EnumType getPrimaryScenarioId(const TrainingSpec& spec)
{
    if (!spec.population.empty()) {
        return spec.population.front().scenarioId;
    }
    return spec.scenarioId;
}

std::unordered_map<std::string, Evolution::TimerAggregate> collectTimerStats(const Timers& timers)
{
    std::unordered_map<std::string, Evolution::TimerAggregate> stats;
    const auto names = timers.getAllTimerNames();
    stats.reserve(names.size());
    for (const auto& name : names) {
        Evolution::TimerAggregate entry;
        entry.totalMs = timers.getAccumulatedTime(name);
        entry.calls = timers.getCallCount(name);
        stats.emplace(name, entry);
    }
    return stats;
}
} // namespace

std::optional<Evolution::EvaluationSnapshot> Evolution::buildEvaluationSnapshot(
    const TrainingRunner& runner)
{
    const World* world = runner.getWorld();
    if (!world) {
        return std::nullopt;
    }

    EvaluationSnapshot snapshot;
    snapshot.worldData = world->getData();
    snapshot.organismIds = world->getOrganismManager().getGrid();
    return snapshot;
}

void Evolution::onEnter(StateMachine& dsm)
{
    LOG_INFO(
        State,
        "Evolution: Starting with population={}, generations={}, scenario={}, organism_type={}",
        evolutionConfig.populationSize,
        evolutionConfig.maxGenerations,
        toString(getPrimaryScenarioId(trainingSpec)),
        static_cast<int>(trainingSpec.organismType));

    // Record training start time.
    trainingStartTime_ = std::chrono::steady_clock::now();
    trainingComplete_ = false;
    finalAverageFitness_ = 0.0;
    finalTrainingSeconds_ = 0.0;
    streamIntervalMs_ = 16;
    lastStreamBroadcastTime_ = std::chrono::steady_clock::time_point{};
    lastProgressBroadcastTime_ = std::chrono::steady_clock::time_point{};
    trainingSessionId_ = UUID::generate();
    pendingTrainingResult_.reset();
    cumulativeSimTime_ = 0.0;
    sumFitnessThisGen_ = 0.0;
    timerStatsAggregate_.clear();
    visibleRunner_.reset();
    visibleQueue_.clear();
    visibleEvalIndex_ = -1;
    workerState_ = std::make_unique<WorkerState>();

    // Seed RNG.
    rng.seed(std::random_device{}());

    brainRegistry_ = TrainingBrainRegistry::createDefault();

    // Initialize population.
    initializePopulation(dsm);

    evolutionConfig.maxParallelEvaluations = resolveParallelEvaluations(
        evolutionConfig.maxParallelEvaluations, static_cast<int>(population.size()));

    // Initialize CPU auto-tuning.
    if (evolutionConfig.targetCpuPercent > 0) {
        cpuMetrics_ = std::make_unique<SystemMetrics>();
        cpuSamples_.clear();
        lastCpuSampleTime_ = {};
        cpuMetrics_->get(); // Prime the delta with an initial reading.
    }

    startWorkers(dsm);
    queueGenerationTasks();
}

void Evolution::onExit(StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Exiting at generation {}, eval {}", generation, currentEval);
    stopWorkers();
    cpuMetrics_.reset();
    cpuSamples_.clear();
    storeBestGenome(dsm);
}

std::optional<Any> Evolution::tick(StateMachine& dsm)
{
    if (trainingComplete_) {
        auto nextState = broadcastTrainingResult(dsm);
        if (nextState.has_value()) {
            return nextState;
        }
        return std::nullopt;
    }

    // Sample CPU periodically for auto-tuning.
    if (cpuMetrics_) {
        constexpr auto kCpuSampleInterval = std::chrono::seconds(2);
        const auto now = std::chrono::steady_clock::now();
        if (lastCpuSampleTime_ == std::chrono::steady_clock::time_point{}
            || now - lastCpuSampleTime_ >= kCpuSampleInterval) {
            lastCpuSampleTime_ = now;
            cpuSamples_.push_back(cpuMetrics_->get().cpu_percent);
        }
    }

    drainResults(dsm);
    if (!trainingComplete_) {
        startNextVisibleEvaluation(dsm);
        stepVisibleEvaluation(dsm);
    }

    if (trainingComplete_) {
        auto nextState = broadcastTrainingResult(dsm);
        if (nextState.has_value()) {
            return nextState;
        }
    }
    return std::nullopt;
}

Any Evolution::onEvent(const Api::EvolutionStop::Cwc& cwc, StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Stopping at generation {}, eval {}", generation, currentEval);
    stopWorkers();
    storeBestGenome(dsm);
    cwc.sendResponse(Api::EvolutionStop::Response::okay(std::monostate{}));
    return Idle{};
}

Any Evolution::onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::TimerStatsGet::Response;

    Api::TimerStatsGet::Okay okay;
    for (const auto& [name, aggregate] : timerStatsAggregate_) {
        Api::TimerStatsGet::TimerEntry entry;
        entry.total_ms = aggregate.totalMs;
        entry.calls = aggregate.calls;
        entry.avg_ms = entry.calls > 0 ? entry.total_ms / entry.calls : 0.0;
        okay.timers[name] = entry;
    }

    LOG_INFO(State, "Evolution: TimerStatsGet returning {} timers", okay.timers.size());
    cwc.sendResponse(Response::okay(std::move(okay)));
    return std::move(*this);
}

Any Evolution::onEvent(const Api::TrainingStreamConfigSet::Cwc& cwc, StateMachine& /*dsm*/)
{
    if (cwc.command.intervalMs < 0) {
        cwc.sendResponse(
            Api::TrainingStreamConfigSet::Response::error(
                ApiError("TrainingStreamConfigSet intervalMs must be >= 0")));
        return std::move(*this);
    }

    streamIntervalMs_ = cwc.command.intervalMs;
    lastStreamBroadcastTime_ = std::chrono::steady_clock::time_point{};

    LOG_INFO(State, "Evolution: Training stream interval set to {}ms", streamIntervalMs_);

    Api::TrainingStreamConfigSet::Okay response{
        .intervalMs = streamIntervalMs_,
        .message = "Training stream interval updated",
    };
    cwc.sendResponse(Api::TrainingStreamConfigSet::Response::okay(std::move(response)));
    return std::move(*this);
}

Any Evolution::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Evolution: Exit received, shutting down");
    stopWorkers();
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));
    return Shutdown{};
}

void Evolution::initializePopulation(StateMachine& dsm)
{
    population.clear();
    fitnessScores.clear();

    DIRTSIM_ASSERT(!trainingSpec.population.empty(), "Training population must not be empty");

    auto& repo = dsm.getGenomeRepository();
    for (const auto& spec : trainingSpec.population) {
        const std::string variant = spec.brainVariant.value_or("");
        const BrainRegistryEntry* entry =
            brainRegistry_.find(trainingSpec.organismType, spec.brainKind, variant);
        DIRTSIM_ASSERT(entry != nullptr, "Training population brain kind not registered");

        if (entry->requiresGenome) {
            const int seedCount = static_cast<int>(spec.seedGenomes.size());
            DIRTSIM_ASSERT(
                spec.count == seedCount + spec.randomCount,
                "Training population count must match seedGenomes + randomCount");

            for (const auto& id : spec.seedGenomes) {
                auto genome = repo.get(id);
                DIRTSIM_ASSERT(genome.has_value(), "Training population seed genome missing");
                population.push_back(
                    Individual{ .brainKind = spec.brainKind,
                                .brainVariant = spec.brainVariant,
                                .scenarioId = spec.scenarioId,
                                .genome = genome.value(),
                                .allowsMutation = entry->allowsMutation });
            }

            for (int i = 0; i < spec.randomCount; ++i) {
                population.push_back(
                    Individual{ .brainKind = spec.brainKind,
                                .brainVariant = spec.brainVariant,
                                .scenarioId = spec.scenarioId,
                                .genome = Genome::random(rng),
                                .allowsMutation = entry->allowsMutation });
            }
        }
        else {
            DIRTSIM_ASSERT(
                spec.seedGenomes.empty(),
                "Training population seedGenomes must be empty for non-genome brains");
            DIRTSIM_ASSERT(
                spec.randomCount == 0,
                "Training population randomCount must be 0 for non-genome brains");

            for (int i = 0; i < spec.count; ++i) {
                population.push_back(
                    Individual{ .brainKind = spec.brainKind,
                                .brainVariant = spec.brainVariant,
                                .scenarioId = spec.scenarioId,
                                .genome = std::nullopt,
                                .allowsMutation = entry->allowsMutation });
            }
        }
    }

    evolutionConfig.populationSize = static_cast<int>(population.size());
    fitnessScores.resize(population.size(), 0.0);

    generation = 0;
    currentEval = 0;
    bestFitnessThisGen = 0.0;
    bestFitnessAllTime = std::numeric_limits<double>::lowest();
    bestGenomeId = INVALID_GENOME_ID;
    sumFitnessThisGen_ = 0.0;

    visibleRunner_.reset();
    visibleQueue_.clear();
    visibleEvalIndex_ = -1;
    visibleScenarioConfig_ = Config::Empty{};
    visibleScenarioId_ = getPrimaryScenarioId(trainingSpec);
}

void Evolution::startWorkers(StateMachine& dsm)
{
    if (!workerState_) {
        workerState_ = std::make_unique<WorkerState>();
    }

    workerState_->trainingSpec = trainingSpec;
    workerState_->evolutionConfig = evolutionConfig;
    workerState_->brainRegistry = brainRegistry_;
    workerState_->genomeRepository = &dsm.getGenomeRepository();

    workerState_->backgroundWorkerCount = std::max(0, evolutionConfig.maxParallelEvaluations - 1);
    workerState_->allowedConcurrency.store(workerState_->backgroundWorkerCount);
    workerState_->activeEvaluations.store(0);
    if (workerState_->backgroundWorkerCount <= 0) {
        return;
    }

    workerState_->workers.reserve(workerState_->backgroundWorkerCount);
    WorkerState* state = workerState_.get();
    for (int i = 0; i < workerState_->backgroundWorkerCount; ++i) {
        workerState_->workers.emplace_back([state]() {
            while (true) {
                WorkerTask task;
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

                WorkerResult result = runEvaluationTask(task, *state);

                state->activeEvaluations.fetch_sub(1);
                state->taskCv.notify_one(); // Wake a worker waiting for a concurrency slot.

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

void Evolution::stopWorkers()
{
    if (!workerState_) {
        return;
    }

    workerState_->stopRequested = true;
    workerState_->taskCv.notify_all();

    for (auto& worker : workerState_->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workerState_->workers.clear();
    workerState_->backgroundWorkerCount = 0;

    {
        std::lock_guard<std::mutex> lock(workerState_->taskMutex);
        workerState_->taskQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(workerState_->resultMutex);
        workerState_->resultQueue.clear();
    }

    visibleQueue_.clear();
    visibleRunner_.reset();
    visibleEvalIndex_ = -1;
}

void Evolution::queueGenerationTasks()
{
    visibleQueue_.clear();

    if (!workerState_) {
        workerState_ = std::make_unique<WorkerState>();
    }

    {
        std::lock_guard<std::mutex> lock(workerState_->taskMutex);
        workerState_->taskQueue.clear();

        const int totalWorkers = std::max(1, evolutionConfig.maxParallelEvaluations);
        for (int i = 0; i < static_cast<int>(population.size()); ++i) {
            if (totalWorkers == 1 || (i % totalWorkers) == 0) {
                visibleQueue_.push_back(i);
            }
            else {
                workerState_->taskQueue.push_back(
                    WorkerTask{ .index = i, .individual = population[i] });
            }
        }
    }

    workerState_->taskCv.notify_all();
}

void Evolution::drainResults(StateMachine& dsm)
{
    std::deque<WorkerResult> results;
    if (!workerState_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(workerState_->resultMutex);
        results.swap(workerState_->resultQueue);
    }

    for (auto& result : results) {
        processResult(dsm, std::move(result));
    }

    if (!results.empty()) {
        broadcastProgress(dsm);
    }
}

void Evolution::startNextVisibleEvaluation(StateMachine& dsm)
{
    if (visibleRunner_ || visibleQueue_.empty()) {
        return;
    }

    visibleEvalIndex_ = visibleQueue_.front();
    visibleQueue_.pop_front();

    const Individual& individual = population[visibleEvalIndex_];
    const TrainingRunner::Config runnerConfig{ .brainRegistry = brainRegistry_ };

    visibleRunner_ = std::make_unique<TrainingRunner>(
        trainingSpec,
        makeRunnerIndividual(individual),
        evolutionConfig,
        dsm.getGenomeRepository(),
        runnerConfig);

    visibleScenarioConfig_ = visibleRunner_->getScenarioConfig();
    visibleScenarioId_ = individual.scenarioId;
}

void Evolution::stepVisibleEvaluation(StateMachine& dsm)
{
    if (!visibleRunner_) {
        return;
    }

    const TrainingRunner::Status status = visibleRunner_->step(1);

    const auto now = std::chrono::steady_clock::now();
    bool shouldBroadcast = true;
    if (streamIntervalMs_ > 0) {
        const auto interval = std::chrono::milliseconds(streamIntervalMs_);
        if (now - lastStreamBroadcastTime_ < interval) {
            shouldBroadcast = false;
        }
        else {
            lastStreamBroadcastTime_ = now;
        }
    }
    else {
        lastStreamBroadcastTime_ = now;
    }

    if (shouldBroadcast) {
        World* world = visibleRunner_->getWorld();
        DIRTSIM_ASSERT(world != nullptr, "Evolution: Visible runner missing World");
        dsm.broadcastRenderMessage(
            world->getData(),
            world->getOrganismManager().getGrid(),
            visibleScenarioId_,
            visibleScenarioConfig_);
    }

    const bool evalComplete = status.state != TrainingRunner::State::Running;
    if (status.state != TrainingRunner::State::Running) {
        WorkerResult result;
        result.index = visibleEvalIndex_;
        result.simTime = status.simTime;
        std::optional<TreeFitnessBreakdown> breakdown;
        result.fitness = computeFitnessForRunner(
            *visibleRunner_, status, trainingSpec.organismType, evolutionConfig, &breakdown);
        result.treeFitnessBreakdown = breakdown;
        if (const World* world = visibleRunner_->getWorld()) {
            result.timerStats = collectTimerStats(world->getTimers());
        }
        result.snapshot = buildEvaluationSnapshot(*visibleRunner_);
        processResult(dsm, std::move(result));
        visibleRunner_.reset();
        visibleEvalIndex_ = -1;
    }

    if (shouldBroadcast || evalComplete) {
        broadcastProgress(dsm);
    }
}

Evolution::WorkerResult Evolution::runEvaluationTask(WorkerTask const& task, WorkerState& state)
{
    DIRTSIM_ASSERT(task.index >= 0, "Evolution: Invalid evaluation index");
    DIRTSIM_ASSERT(state.genomeRepository != nullptr, "Evolution: GenomeRepository missing");

    const TrainingRunner::Config runnerConfig{ .brainRegistry = state.brainRegistry };
    TrainingRunner runner(
        state.trainingSpec,
        makeRunnerIndividual(task.individual),
        state.evolutionConfig,
        *state.genomeRepository,
        runnerConfig);

    TrainingRunner::Status status;
    while (status.state == TrainingRunner::State::Running && !state.stopRequested) {
        status = runner.step(1);
    }

    WorkerResult result;
    result.index = task.index;
    result.simTime = status.simTime;
    std::optional<TreeFitnessBreakdown> breakdown;
    result.fitness = computeFitnessForRunner(
        runner, status, state.trainingSpec.organismType, state.evolutionConfig, &breakdown);
    result.treeFitnessBreakdown = breakdown;
    if (const World* world = runner.getWorld()) {
        result.timerStats = collectTimerStats(world->getTimers());
    }
    result.snapshot = buildEvaluationSnapshot(runner);
    return result;
}

void Evolution::processResult(StateMachine& dsm, WorkerResult result)
{
    if (result.index < 0 || result.index >= static_cast<int>(population.size())) {
        return;
    }

    fitnessScores[result.index] = result.fitness;
    sumFitnessThisGen_ += result.fitness;
    currentEval++;
    cumulativeSimTime_ += result.simTime;
    for (const auto& [name, entry] : result.timerStats) {
        auto& aggregate = timerStatsAggregate_[name];
        aggregate.totalMs += entry.totalMs;
        aggregate.calls += entry.calls;
    }
    const auto totalSimulation = result.timerStats.find("total_simulation");
    if (totalSimulation != result.timerStats.end()) {
        auto& aggregate = timerStatsAggregate_["training_total"];
        aggregate.totalMs += totalSimulation->second.totalMs;
        aggregate.calls += totalSimulation->second.calls;
    }

    if (result.fitness > bestFitnessThisGen) {
        bestFitnessThisGen = result.fitness;
    }
    if (result.fitness > bestFitnessAllTime) {
        bestFitnessAllTime = result.fitness;
        if (result.snapshot.has_value()) {
            Api::TrainingBestSnapshot bestSnapshot;
            bestSnapshot.worldData = std::move(result.snapshot->worldData);
            bestSnapshot.organismIds = std::move(result.snapshot->organismIds);
            bestSnapshot.fitness = result.fitness;
            bestSnapshot.generation = generation;
            dsm.broadcastEventData(
                Api::TrainingBestSnapshot::name(), Network::serialize_payload(bestSnapshot));
        }
        else {
            LOG_WARN(
                State,
                "Evolution: Missing snapshot for new best (gen={} eval={})",
                generation,
                result.index);
        }

        const Individual& individual = population[result.index];
        if (individual.genome.has_value()) {
            auto& repo = dsm.getGenomeRepository();
            const GenomeMetadata meta{
                .name =
                    "gen_" + std::to_string(generation) + "_eval_" + std::to_string(result.index),
                .fitness = result.fitness,
                .generation = generation,
                .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
                .scenarioId = individual.scenarioId,
                .notes = "",
                .organismType = trainingSpec.organismType,
                .brainKind = individual.brainKind,
                .brainVariant = individual.brainVariant,
                .trainingSessionId = trainingSessionId_,
            };
            bestGenomeId = UUID::generate();
            repo.store(bestGenomeId, individual.genome.value(), meta);
            repo.markAsBest(bestGenomeId);
        }
        else {
            bestGenomeId = INVALID_GENOME_ID;
        }

        LOG_INFO(
            State,
            "Evolution: New best fitness {:.4f} at gen {} eval {}",
            result.fitness,
            generation,
            result.index);
    }

    if (result.treeFitnessBreakdown.has_value()) {
        const auto& breakdown = result.treeFitnessBreakdown.value();
        LOG_INFO(
            State,
            "Evolution: gen={} eval={}/{} fitness={:.4f} (surv={:.3f} energy={:.3f} res={:.3f} "
            "stage={:.3f} struct={:.3f} milestone={:.3f} cmd={:.3f})",
            generation,
            currentEval,
            evolutionConfig.populationSize,
            result.fitness,
            breakdown.survivalScore,
            breakdown.energyScore,
            breakdown.resourceScore,
            breakdown.stageBonus,
            breakdown.structureBonus,
            breakdown.milestoneBonus,
            breakdown.commandScore);
    }
    else {
        LOG_INFO(
            State,
            "Evolution: gen={} eval={}/{} fitness={:.4f}",
            generation,
            currentEval,
            evolutionConfig.populationSize,
            result.fitness);
    }

    maybeCompleteGeneration(dsm);
}

void Evolution::maybeCompleteGeneration(StateMachine& dsm)
{
    if (currentEval < evolutionConfig.populationSize) {
        return;
    }

    if (evolutionConfig.maxGenerations > 0 && generation + 1 >= evolutionConfig.maxGenerations) {
        finalAverageFitness_ = evolutionConfig.populationSize > 0
            ? (sumFitnessThisGen_ / evolutionConfig.populationSize)
            : 0.0;
        auto now = std::chrono::steady_clock::now();
        finalTrainingSeconds_ = std::chrono::duration<double>(now - trainingStartTime_).count();
        trainingComplete_ = true;
        generation = evolutionConfig.maxGenerations;
        return;
    }

    advanceGeneration(dsm);
    queueGenerationTasks();
}

void Evolution::advanceGeneration(StateMachine& dsm)
{
    LOG_INFO(
        State,
        "Evolution: Generation {} complete. Best={:.4f}, All-time={:.4f}",
        generation,
        bestFitnessThisGen,
        bestFitnessAllTime);

    // Store best genome periodically.
    if (generation % saveInterval == 0) {
        storeBestGenome(dsm);
    }

    // Selection and mutation: create offspring.
    std::vector<Individual> offspring;
    std::vector<double> offspringFitness;
    offspring.reserve(evolutionConfig.populationSize);
    offspringFitness.reserve(evolutionConfig.populationSize);

    for (int i = 0; i < evolutionConfig.populationSize; ++i) {
        const int parentIdx =
            tournamentSelectIndex(fitnessScores, evolutionConfig.tournamentSize, rng);
        const Individual& parent = population[parentIdx];

        Individual child = parent;
        if (parent.genome.has_value() && parent.allowsMutation) {
            child.genome = mutate(parent.genome.value(), mutationConfig, rng);
        }
        offspring.push_back(std::move(child));
        offspringFitness.push_back(0.0); // Will be evaluated next generation.
    }

    // Elitist replacement: keep best from parents + offspring.
    struct PoolEntry {
        double fitness = 0.0;
        Individual individual;
        bool isOffspring = false;
        int order = 0;
    };

    std::vector<PoolEntry> pool;
    pool.reserve(population.size() + offspring.size());
    for (size_t i = 0; i < population.size(); ++i) {
        pool.push_back(
            PoolEntry{
                .fitness = fitnessScores[i],
                .individual = population[i],
                .isOffspring = false,
                .order = static_cast<int>(i),
            });
    }
    const int parentCount = static_cast<int>(population.size());
    for (size_t i = 0; i < offspring.size(); ++i) {
        pool.push_back(
            PoolEntry{
                .fitness = offspringFitness[i],
                .individual = offspring[i],
                .isOffspring = true,
                .order = parentCount + static_cast<int>(i),
            });
    }

    std::sort(pool.begin(), pool.end(), [](const PoolEntry& a, const PoolEntry& b) {
        if (a.fitness != b.fitness) {
            return a.fitness > b.fitness;
        }
        if (a.isOffspring != b.isOffspring) {
            return a.isOffspring;
        }
        return a.order < b.order;
    });

    population.clear();
    population.reserve(evolutionConfig.populationSize);
    const int count = std::min(evolutionConfig.populationSize, static_cast<int>(pool.size()));
    for (int i = 0; i < count; ++i) {
        population.push_back(pool[i].individual);
    }

    // Advance to next generation.
    generation++;

    // Only reset for next generation if we're not at the end.
    // This preserves currentEval = populationSize in the final broadcast,
    // giving the UI a clean "all evals complete" signal.
    if (evolutionConfig.maxGenerations <= 0 || generation < evolutionConfig.maxGenerations) {
        currentEval = 0;
        bestFitnessThisGen = 0.0;
        sumFitnessThisGen_ = 0.0;
        std::fill(fitnessScores.begin(), fitnessScores.end(), 0.0);
    }

    adjustConcurrency();
}

void Evolution::adjustConcurrency()
{
    if (evolutionConfig.targetCpuPercent <= 0 || !workerState_ || cpuSamples_.empty()) {
        return;
    }

    double sum = 0.0;
    for (const double sample : cpuSamples_) {
        sum += sample;
    }
    const double avgCpu = sum / static_cast<double>(cpuSamples_.size());
    cpuSamples_.clear();

    const double target = static_cast<double>(evolutionConfig.targetCpuPercent);
    constexpr double kTolerance = 5.0;

    const int current = workerState_->allowedConcurrency.load();
    int adjusted = current;

    if (avgCpu > target + kTolerance && current > 1) {
        adjusted = current - 1;
    }
    else if (avgCpu < target - kTolerance && current < workerState_->backgroundWorkerCount) {
        adjusted = current + 1;
    }

    if (adjusted != current) {
        LOG_INFO(
            State,
            "Evolution: CPU auto-tune avg={:.1f}% target={}% concurrency {} -> {}",
            avgCpu,
            evolutionConfig.targetCpuPercent,
            current,
            adjusted);
        workerState_->allowedConcurrency.store(adjusted);
        workerState_->taskCv.notify_all(); // Wake workers to re-evaluate concurrency predicate.
    }
}

void Evolution::broadcastProgress(StateMachine& dsm)
{
    const auto now = std::chrono::steady_clock::now();
    if (lastProgressBroadcastTime_ != std::chrono::steady_clock::time_point{}
        && now - lastProgressBroadcastTime_ < kProgressBroadcastInterval) {
        return;
    }
    lastProgressBroadcastTime_ = now;

    // Calculate average fitness of evaluated individuals.
    double avgFitness = 0.0;
    if (currentEval > 0) {
        avgFitness = sumFitnessThisGen_ / currentEval;
    }

    // Calculate total training time.
    double totalSeconds = std::chrono::duration<double>(now - trainingStartTime_).count();

    const double visibleSimTime = visibleRunner_ ? visibleRunner_->getSimTime() : 0.0;
    double cumulative = cumulativeSimTime_ + visibleSimTime;

    // Speedup factor = how much faster than real-time.
    double speedup = (totalSeconds > 0.0) ? (cumulative / totalSeconds) : 0.0;

    // ETA calculation based on throughput.
    int completedIndividuals = generation * evolutionConfig.populationSize + currentEval;
    int totalIndividuals = evolutionConfig.maxGenerations * evolutionConfig.populationSize;
    int remainingIndividuals = totalIndividuals - completedIndividuals;
    double eta = 0.0;
    if (completedIndividuals > 0 && remainingIndividuals > 0) {
        double avgRealTimePerIndividual = totalSeconds / completedIndividuals;
        eta = remainingIndividuals * avgRealTimePerIndividual;
    }

    double bestAllTime = bestFitnessAllTime;
    if (generation == 0 && currentEval == 0
        && bestAllTime == std::numeric_limits<double>::lowest()) {
        bestAllTime = 0.0;
    }

    // Compute CPU auto-tune fields.
    int activeParallelism = evolutionConfig.maxParallelEvaluations;
    double latestCpu = 0.0;
    if (workerState_) {
        activeParallelism = workerState_->allowedConcurrency.load() + 1; // +1 for main thread.
    }
    if (!cpuSamples_.empty()) {
        latestCpu = cpuSamples_.back();
    }

    const Api::EvolutionProgress progress{
        .generation = generation,
        .maxGenerations = evolutionConfig.maxGenerations,
        .currentEval = currentEval,
        .populationSize = evolutionConfig.populationSize,
        .bestFitnessThisGen = bestFitnessThisGen,
        .bestFitnessAllTime = bestAllTime,
        .averageFitness = avgFitness,
        .bestGenomeId = bestGenomeId,
        .totalTrainingSeconds = totalSeconds,
        .currentSimTime = visibleSimTime,
        .cumulativeSimTime = cumulative,
        .speedupFactor = speedup,
        .etaSeconds = eta,
        .activeParallelism = activeParallelism,
        .cpuPercent = latestCpu,
    };

    dsm.broadcastEventData(Api::EvolutionProgress::name(), Network::serialize_payload(progress));
}

std::optional<Any> Evolution::broadcastTrainingResult(StateMachine& dsm)
{
    if (!pendingTrainingResult_.has_value()) {
        pendingTrainingResult_ = buildUnsavedTrainingResult();
    }

    Api::TrainingResult trainingResult;
    trainingResult.summary = pendingTrainingResult_->summary;
    trainingResult.candidates.reserve(pendingTrainingResult_->candidates.size());
    for (const auto& candidate : pendingTrainingResult_->candidates) {
        trainingResult.candidates.push_back(
            Api::TrainingResult::Candidate{
                .id = candidate.id,
                .fitness = candidate.fitness,
                .brainKind = candidate.brainKind,
                .brainVariant = candidate.brainVariant,
                .generation = candidate.generation,
            });
    }

    auto* wsService = dsm.getWebSocketService();
    if (!wsService) {
        LOG_WARN(State, "No WebSocketService available for TrainingResult");
    }
    else {
        const auto response = wsService->sendCommandAndGetResponse<Api::TrainingResult::OkayType>(
            trainingResult, 5000);
        DIRTSIM_ASSERT(
            !response.isError(),
            std::string("TrainingResult send failed: ") + response.errorValue());
        DIRTSIM_ASSERT(
            !response.value().isError(),
            std::string("TrainingResult response error: ") + response.value().errorValue().message);
    }

    UnsavedTrainingResult result = std::move(pendingTrainingResult_.value());
    pendingTrainingResult_.reset();
    return Any{ result };
}

void Evolution::storeBestGenome(StateMachine& dsm)
{
    if (population.empty() || fitnessScores.empty()) {
        return;
    }

    // Find best in current population.
    int bestIdx = -1;
    double bestFit = 0.0;
    for (int i = 0; i < static_cast<int>(fitnessScores.size()); ++i) {
        if (!population[i].genome.has_value()) {
            continue;
        }
        if (bestIdx < 0 || fitnessScores[i] > bestFit) {
            bestFit = fitnessScores[i];
            bestIdx = i;
        }
    }

    if (bestIdx < 0) {
        return;
    }

    auto& repo = dsm.getGenomeRepository();
    const GenomeMetadata meta{
        .name = "checkpoint_gen_" + std::to_string(generation),
        .fitness = bestFit,
        .generation = generation,
        .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
        .scenarioId = population[bestIdx].scenarioId,
        .notes = "",
        .organismType = trainingSpec.organismType,
        .brainKind = population[bestIdx].brainKind,
        .brainVariant = population[bestIdx].brainVariant,
        .trainingSessionId = trainingSessionId_,
    };
    const GenomeId id = UUID::generate();
    repo.store(id, population[bestIdx].genome.value(), meta);

    if (bestFit >= bestFitnessAllTime) {
        repo.markAsBest(id);
        bestGenomeId = id;
    }

    LOG_INFO(
        State, "Evolution: Stored checkpoint genome (gen {}, fitness {:.4f})", generation, bestFit);
}

UnsavedTrainingResult Evolution::buildUnsavedTrainingResult()
{
    UnsavedTrainingResult result;
    result.evolutionConfig = evolutionConfig;
    result.mutationConfig = mutationConfig;
    result.trainingSpec = trainingSpec;
    result.summary.scenarioId = getPrimaryScenarioId(trainingSpec);
    result.summary.organismType = trainingSpec.organismType;
    result.summary.populationSize = evolutionConfig.populationSize;
    result.summary.maxGenerations = evolutionConfig.maxGenerations;
    result.summary.completedGenerations = evolutionConfig.maxGenerations;
    result.summary.bestFitness = bestFitnessAllTime;
    result.summary.averageFitness = finalAverageFitness_;
    result.summary.totalTrainingSeconds = finalTrainingSeconds_;
    result.summary.trainingSessionId = trainingSessionId_;
    result.timerStats.reserve(timerStatsAggregate_.size());
    for (const auto& [name, aggregate] : timerStatsAggregate_) {
        Api::TimerStatsGet::TimerEntry entry;
        entry.total_ms = aggregate.totalMs;
        entry.calls = aggregate.calls;
        entry.avg_ms = entry.calls > 0 ? entry.total_ms / entry.calls : 0.0;
        result.timerStats.emplace(name, entry);
    }

    if (!trainingSpec.population.empty()) {
        result.summary.primaryBrainKind = trainingSpec.population.front().brainKind;
        result.summary.primaryBrainVariant = trainingSpec.population.front().brainVariant;
        result.summary.primaryPopulationCount = trainingSpec.population.front().count;
    }

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const int generationIndex = std::max(0, evolutionConfig.maxGenerations - 1);

    result.candidates.reserve(population.size());
    for (size_t i = 0; i < population.size(); ++i) {
        if (!population[i].genome.has_value()) {
            continue;
        }

        UnsavedTrainingResult::Candidate candidate;
        candidate.id = UUID::generate();
        candidate.genome = population[i].genome.value();
        candidate.fitness = fitnessScores[i];
        candidate.brainKind = population[i].brainKind;
        candidate.brainVariant = population[i].brainVariant;
        candidate.generation = generationIndex;
        candidate.metadata = GenomeMetadata{
            .name = "",
            .fitness = candidate.fitness,
            .generation = generationIndex,
            .createdTimestamp = now,
            .scenarioId = population[i].scenarioId,
            .notes = "",
            .organismType = trainingSpec.organismType,
            .brainKind = candidate.brainKind,
            .brainVariant = candidate.brainVariant,
            .trainingSessionId = trainingSessionId_,
        };
        result.candidates.push_back(candidate);
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) {
        return a.fitness > b.fitness;
    });

    for (size_t i = 0; i < result.candidates.size(); ++i) {
        result.candidates[i].metadata.name =
            "training_" + trainingSessionId_.toShortString() + "_rank_" + std::to_string(i + 1);
    }

    LOG_INFO(State, "Evolution: Training complete, {} saveable genomes", result.candidates.size());

    return result;
}

} // namespace State
} // namespace Server
} // namespace DirtSim
