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
#include <cmath>
#include <ctime>
#include <limits>
#include <spdlog/spdlog.h>
#include <string_view>

namespace DirtSim {
namespace Server {
namespace State {

namespace {
constexpr auto kProgressBroadcastInterval = std::chrono::milliseconds(100);
constexpr size_t kTopCommandSignatureLimit = 20;
constexpr size_t kTelemetrySignatureLimit = 6;
constexpr size_t kFitnessDistributionBinCount = 16;
constexpr auto kBestSnapshotTieBroadcastMinInterval = std::chrono::seconds(1);
constexpr size_t kBestSnapshotVariantFingerprintMax = 64;
constexpr int kBestSnapshotVariantsMaxPerGeneration = 4;
constexpr double kBestFitnessTieRelativeEpsilon = 1e-12;

uint64_t fnv1aAppendBytes(uint64_t hash, const std::byte* data, size_t len)
{
    constexpr uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;

    if (hash == 0) {
        hash = kOffsetBasis;
    }

    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= kPrime;
    }

    return hash;
}

uint64_t fnv1aAppendString(uint64_t hash, std::string_view text)
{
    return fnv1aAppendBytes(
        hash, reinterpret_cast<const std::byte*>(text.data()), text.size() * sizeof(char));
}

bool fitnessTiesBest(double fitness, double bestFitness)
{
    if (fitness == bestFitness) {
        return true;
    }

    const double scale = std::max(1.0, std::abs(bestFitness));
    return std::abs(fitness - bestFitness) <= (kBestFitnessTieRelativeEpsilon * scale);
}

uint64_t computePhenotypeHash(const Evolution::WorkerResult& result)
{
    uint64_t hash = 0;

    const auto appendTop = [&](const auto& entries, const char* label) {
        hash = fnv1aAppendString(hash, label);
        hash = fnv1aAppendString(hash, ":");
        const size_t limit = std::min(entries.size(), kTelemetrySignatureLimit);
        for (size_t i = 0; i < limit; ++i) {
            hash = fnv1aAppendString(hash, entries[i].first);
            hash = fnv1aAppendString(hash, "|");
        }
        hash = fnv1aAppendString(hash, ";");
    };

    appendTop(result.topCommandOutcomeSignatures, "out");
    appendTop(result.topCommandSignatures, "cmd");

    return hash;
}

uint64_t computeBestSnapshotFingerprint(const Evolution::WorkerResult& result)
{
    uint64_t hash = 0;

    const auto appendInt = [&](int value) {
        hash = fnv1aAppendBytes(hash, reinterpret_cast<const std::byte*>(&value), sizeof(value));
    };

    appendInt(result.commandsAccepted);
    appendInt(result.commandsRejected);

    const auto appendTopWithCounts = [&](const auto& entries, const char* label) {
        hash = fnv1aAppendString(hash, label);
        hash = fnv1aAppendString(hash, ":");
        const size_t limit = std::min(entries.size(), kTopCommandSignatureLimit);
        for (size_t i = 0; i < limit; ++i) {
            hash = fnv1aAppendString(hash, entries[i].first);
            hash = fnv1aAppendString(hash, "#");
            appendInt(entries[i].second);
            hash = fnv1aAppendString(hash, "|");
        }
        hash = fnv1aAppendString(hash, ";");
    };

    appendTopWithCounts(result.topCommandOutcomeSignatures, "out");
    appendTopWithCounts(result.topCommandSignatures, "cmd");

    return hash;
}

int estimateTotalEvaluations(const EvolutionConfig& evolutionConfig)
{
    if (evolutionConfig.maxGenerations <= 0 || evolutionConfig.populationSize <= 0) {
        return 0;
    }

    const int64_t basePopulation = evolutionConfig.populationSize;
    int64_t total = basePopulation;
    if (evolutionConfig.maxGenerations > 1) {
        total += static_cast<int64_t>(evolutionConfig.maxGenerations - 1) * (basePopulation * 2);
    }

    if (total > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(total);
}

bool isOffspringOrigin(Evolution::IndividualOrigin origin)
{
    return origin == Evolution::IndividualOrigin::OffspringMutated
        || origin == Evolution::IndividualOrigin::OffspringClone;
}

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
        .idleCancels = status.idleCancels,
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

const char* toProgressSource(Evolution::IndividualOrigin origin)
{
    switch (origin) {
        case Evolution::IndividualOrigin::Unknown:
            return "none";
        case Evolution::IndividualOrigin::Seed:
            return "seed";
        case Evolution::IndividualOrigin::EliteCarryover:
            return "elite_carryover";
        case Evolution::IndividualOrigin::OffspringMutated:
            return "offspring_mutated";
        case Evolution::IndividualOrigin::OffspringClone:
            return "offspring_clone";
    }
    return "none";
}

GenomeId storeManagedGenome(
    StateMachine& dsm,
    const Genome& genome,
    const GenomeMetadata& metadata,
    int archiveMaxSize,
    const char* reason)
{
    auto& repo = dsm.getGenomeRepository();
    const auto storeResult = repo.storeOrUpdateByHash(genome, metadata);
    if (storeResult.deduplicated) {
        LOG_INFO(
            State,
            "Evolution: Reused existing genome {} for {}",
            storeResult.id.toShortString(),
            reason);
    }
    else {
        LOG_INFO(
            State,
            "Evolution: Stored new genome {} for {}",
            storeResult.id.toShortString(),
            reason);
    }

    if (archiveMaxSize > 0) {
        const size_t pruned = repo.pruneManagedByFitness(static_cast<size_t>(archiveMaxSize));
        if (pruned > 0) {
            LOG_INFO(
                State,
                "Evolution: Pruned {} managed genomes (max_archive={})",
                pruned,
                archiveMaxSize);
        }
    }

    return storeResult.id;
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
    generationTelemetry_.reset();
    lastGenerationEliteCarryoverCount_ = 0;
    lastGenerationSeedCount_ = 0;
    lastGenerationOffspringCloneCount_ = 0;
    lastGenerationOffspringMutatedCount_ = 0;
    lastGenerationOffspringCloneBeatsParentCount_ = 0;
    lastGenerationOffspringCloneAvgDeltaFitness_ = 0.0;
    lastGenerationOffspringMutatedBeatsParentCount_ = 0;
    lastGenerationOffspringMutatedAvgDeltaFitness_ = 0.0;
    lastGenerationPhenotypeUniqueCount_ = 0;
    lastGenerationPhenotypeUniqueEliteCarryoverCount_ = 0;
    lastGenerationPhenotypeUniqueOffspringMutatedCount_ = 0;
    lastGenerationPhenotypeNovelOffspringMutatedCount_ = 0;
    lastBreedingPerturbationsAvg_ = 0.0;
    lastBreedingResetsAvg_ = 0.0;
    lastBreedingWeightChangesAvg_ = 0.0;
    lastBreedingWeightChangesMin_ = 0;
    lastBreedingWeightChangesMax_ = 0;
    bestSnapshotVariantFingerprints_.clear();
    bestSnapshotVariantFingerprintOrder_.clear();
    lastBestSnapshotBroadcastTime_ = std::chrono::steady_clock::time_point{};
    bestSnapshotVariantBroadcastCountThisGen_ = 0;
    timerStatsAggregate_.clear();
    dsm.clearCachedTrainingBestSnapshot();
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
    populationOrigins.clear();
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
                                .allowsMutation = entry->allowsMutation,
                                .parentFitness = std::nullopt });
                populationOrigins.push_back(IndividualOrigin::Seed);
            }

            for (int i = 0; i < spec.randomCount; ++i) {
                DIRTSIM_ASSERT(
                    entry->createRandomGenome,
                    "Evolution: createRandomGenome must be set for genome brains");
                population.push_back(
                    Individual{ .brainKind = spec.brainKind,
                                .brainVariant = spec.brainVariant,
                                .scenarioId = spec.scenarioId,
                                .genome = entry->createRandomGenome(rng),
                                .allowsMutation = entry->allowsMutation,
                                .parentFitness = std::nullopt });
                populationOrigins.push_back(IndividualOrigin::Seed);
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
                                .allowsMutation = entry->allowsMutation,
                                .parentFitness = std::nullopt });
                populationOrigins.push_back(IndividualOrigin::Seed);
            }
        }
    }

    DIRTSIM_ASSERT(
        populationOrigins.size() == population.size(),
        "Evolution: population origins must align with population");

    evolutionConfig.populationSize = static_cast<int>(population.size());
    fitnessScores.resize(population.size(), 0.0);

    generation = 0;
    currentEval = 0;
    bestFitnessThisGen = 0.0;
    bestFitnessAllTime = std::numeric_limits<double>::lowest();
    bestGenomeId = INVALID_GENOME_ID;
    bestThisGenOrigin_ = IndividualOrigin::Unknown;
    lastCompletedGeneration_ = -1;
    lastGenerationFitnessMin_ = 0.0;
    lastGenerationFitnessMax_ = 0.0;
    lastGenerationFitnessHistogram_.clear();
    pruneBeforeBreeding_ = false;
    completedEvaluations_ = 0;
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
        result.commandsAccepted = status.commandsAccepted;
        result.commandsRejected = status.commandsRejected;
        result.topCommandSignatures =
            visibleRunner_->getTopCommandSignatures(kTopCommandSignatureLimit);
        result.topCommandOutcomeSignatures =
            visibleRunner_->getTopCommandOutcomeSignatures(kTopCommandSignatureLimit);
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
    result.commandsAccepted = status.commandsAccepted;
    result.commandsRejected = status.commandsRejected;
    result.topCommandSignatures = runner.getTopCommandSignatures(kTopCommandSignatureLimit);
    result.topCommandOutcomeSignatures =
        runner.getTopCommandOutcomeSignatures(kTopCommandSignatureLimit);
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

    IndividualOrigin origin = IndividualOrigin::Unknown;
    if (result.index >= 0 && result.index < static_cast<int>(populationOrigins.size())) {
        origin = populationOrigins[result.index];
    }

    const uint64_t phenotypeHash = computePhenotypeHash(result);
    generationTelemetry_.phenotypeAll.insert(phenotypeHash);
    switch (origin) {
        case IndividualOrigin::EliteCarryover:
            generationTelemetry_.eliteCarryoverCount++;
            generationTelemetry_.phenotypeEliteCarryover.insert(phenotypeHash);
            break;
        case IndividualOrigin::OffspringClone: {
            generationTelemetry_.offspringCloneCount++;
            const Individual& individual = population[result.index];
            if (individual.parentFitness.has_value()) {
                generationTelemetry_.offspringCloneComparedCount++;
                const double delta = result.fitness - individual.parentFitness.value();
                generationTelemetry_.offspringCloneDeltaFitnessSum += delta;
                if (delta > 0.0) {
                    generationTelemetry_.offspringCloneBeatsParentCount++;
                }
            }
            break;
        }
        case IndividualOrigin::OffspringMutated: {
            generationTelemetry_.offspringMutatedCount++;
            generationTelemetry_.phenotypeOffspringMutated.insert(phenotypeHash);
            const Individual& individual = population[result.index];
            if (individual.parentFitness.has_value()) {
                generationTelemetry_.offspringMutatedComparedCount++;
                const double delta = result.fitness - individual.parentFitness.value();
                generationTelemetry_.offspringMutatedDeltaFitnessSum += delta;
                if (delta > 0.0) {
                    generationTelemetry_.offspringMutatedBeatsParentCount++;
                }
            }
            break;
        }
        case IndividualOrigin::Seed:
            generationTelemetry_.seedCount++;
            generationTelemetry_.phenotypeSeed.insert(phenotypeHash);
            break;
        case IndividualOrigin::Unknown:
            break;
    }

    fitnessScores[result.index] = result.fitness;
    sumFitnessThisGen_ += result.fitness;
    completedEvaluations_++;
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
        if (result.index >= 0 && result.index < static_cast<int>(populationOrigins.size())) {
            bestThisGenOrigin_ = populationOrigins[result.index];
        }
        else {
            bestThisGenOrigin_ = IndividualOrigin::Unknown;
        }
    }
    const uint64_t bestSnapshotFingerprint = computeBestSnapshotFingerprint(result);
    const auto now = std::chrono::steady_clock::now();
    const auto broadcastBestSnapshot = [&](const char* reason) -> bool {
        if (!result.snapshot.has_value()) {
            LOG_WARN(
                State,
                "Evolution: Missing snapshot for best snapshot broadcast (reason={} gen={} "
                "eval={})",
                reason,
                generation,
                result.index);
            return false;
        }

        Api::TrainingBestSnapshot bestSnapshot;
        bestSnapshot.worldData = std::move(result.snapshot->worldData);
        bestSnapshot.organismIds = std::move(result.snapshot->organismIds);
        bestSnapshot.fitness = result.fitness;
        bestSnapshot.generation = generation;
        bestSnapshot.commandsAccepted = result.commandsAccepted;
        bestSnapshot.commandsRejected = result.commandsRejected;
        bestSnapshot.topCommandSignatures.reserve(result.topCommandSignatures.size());
        for (const auto& [signature, count] : result.topCommandSignatures) {
            bestSnapshot.topCommandSignatures.push_back(
                Api::TrainingBestSnapshot::CommandSignatureCount{
                    .signature = signature,
                    .count = count,
                });
        }
        bestSnapshot.topCommandOutcomeSignatures.reserve(result.topCommandOutcomeSignatures.size());
        for (const auto& [signature, count] : result.topCommandOutcomeSignatures) {
            bestSnapshot.topCommandOutcomeSignatures.push_back(
                Api::TrainingBestSnapshot::CommandSignatureCount{
                    .signature = signature,
                    .count = count,
                });
        }
        dsm.updateCachedTrainingBestSnapshot(bestSnapshot);
        dsm.broadcastEventData(
            Api::TrainingBestSnapshot::name(), Network::serialize_payload(bestSnapshot));
        return true;
    };

    if (result.fitness > bestFitnessAllTime) {
        bestFitnessAllTime = result.fitness;
        bestSnapshotVariantFingerprints_.clear();
        bestSnapshotVariantFingerprintOrder_.clear();
        bestSnapshotVariantBroadcastCountThisGen_ = 0;
        lastBestSnapshotBroadcastTime_ = std::chrono::steady_clock::time_point{};

        if (broadcastBestSnapshot("new-record")) {
            lastBestSnapshotBroadcastTime_ = now;
            bestSnapshotVariantFingerprints_.insert(bestSnapshotFingerprint);
            bestSnapshotVariantFingerprintOrder_.push_back(bestSnapshotFingerprint);
        }

        const Individual& individual = population[result.index];
        if (individual.genome.has_value()) {
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
            bestGenomeId = storeManagedGenome(
                dsm,
                individual.genome.value(),
                meta,
                evolutionConfig.genomeArchiveMaxSize,
                "all-time best");
            auto& repo = dsm.getGenomeRepository();
            repo.markAsBest(bestGenomeId);
        }
        else {
            bestGenomeId = INVALID_GENOME_ID;
        }

        LOG_INFO(
            State,
            "Evolution: Best fitness updated {:.4f} at gen {} eval {}",
            result.fitness,
            generation,
            result.index);
    }
    else if (fitnessTiesBest(result.fitness, bestFitnessAllTime)) {
        const bool reachedVariantLimit =
            bestSnapshotVariantBroadcastCountThisGen_ >= kBestSnapshotVariantsMaxPerGeneration;
        const bool fingerprintSeen = bestSnapshotVariantFingerprints_.find(bestSnapshotFingerprint)
            != bestSnapshotVariantFingerprints_.end();
        const bool withinCooldown =
            lastBestSnapshotBroadcastTime_ != std::chrono::steady_clock::time_point{}
            && now - lastBestSnapshotBroadcastTime_ < kBestSnapshotTieBroadcastMinInterval;
        if (!reachedVariantLimit && !fingerprintSeen && !withinCooldown
            && broadcastBestSnapshot("tied-best")) {
            lastBestSnapshotBroadcastTime_ = now;
            bestSnapshotVariantBroadcastCountThisGen_++;
            bestSnapshotVariantFingerprints_.insert(bestSnapshotFingerprint);
            bestSnapshotVariantFingerprintOrder_.push_back(bestSnapshotFingerprint);
            while (bestSnapshotVariantFingerprintOrder_.size()
                   > kBestSnapshotVariantFingerprintMax) {
                const uint64_t oldFingerprint = bestSnapshotVariantFingerprintOrder_.front();
                bestSnapshotVariantFingerprintOrder_.pop_front();
                bestSnapshotVariantFingerprints_.erase(oldFingerprint);
            }
        }
    }

    const int generationPopulationSize = static_cast<int>(population.size());

    if (result.treeFitnessBreakdown.has_value()) {
        const auto& breakdown = result.treeFitnessBreakdown.value();
        LOG_INFO(
            State,
            "Evolution: gen={} eval={}/{} fitness={:.4f} (surv={:.3f} energy={:.3f} res={:.3f} "
            "partial={:.3f} stage={:.3f} struct={:.3f} milestone={:.3f} cmd={:.3f})",
            generation,
            currentEval,
            generationPopulationSize,
            result.fitness,
            breakdown.survivalScore,
            breakdown.energyScore,
            breakdown.resourceScore,
            breakdown.partialStructureBonus,
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
            generationPopulationSize,
            result.fitness);
    }

    maybeCompleteGeneration(dsm);
}

void Evolution::maybeCompleteGeneration(StateMachine& dsm)
{
    const int generationPopulationSize = static_cast<int>(population.size());
    if (currentEval < generationPopulationSize) {
        return;
    }

    captureLastGenerationFitnessDistribution();
    captureLastGenerationTelemetry();

    if (evolutionConfig.maxGenerations > 0 && generation + 1 >= evolutionConfig.maxGenerations) {
        finalAverageFitness_ =
            generationPopulationSize > 0 ? (sumFitnessThisGen_ / generationPopulationSize) : 0.0;
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

    const int survivorPopulationSize = evolutionConfig.populationSize;
    DIRTSIM_ASSERT(survivorPopulationSize > 0, "Evolution: survivor population must be positive");

    if (pruneBeforeBreeding_) {
        // Prune only after the expanded population has been fully evaluated.
        struct RankedIndividual {
            double fitness = 0.0;
            Individual individual;
            IndividualOrigin origin = IndividualOrigin::Unknown;
            int order = 0;
        };

        std::vector<RankedIndividual> ranked;
        ranked.reserve(population.size());
        for (size_t i = 0; i < population.size(); ++i) {
            const IndividualOrigin origin =
                i < populationOrigins.size() ? populationOrigins[i] : IndividualOrigin::Unknown;
            ranked.push_back(
                RankedIndividual{
                    .fitness = fitnessScores[i],
                    .individual = population[i],
                    .origin = origin,
                    .order = static_cast<int>(i),
                });
        }

        std::sort(
            ranked.begin(), ranked.end(), [](const RankedIndividual& a, const RankedIndividual& b) {
                if (a.fitness != b.fitness) {
                    return a.fitness > b.fitness;
                }
                const bool aIsOffspring = isOffspringOrigin(a.origin);
                const bool bIsOffspring = isOffspringOrigin(b.origin);
                if (aIsOffspring != bIsOffspring) {
                    return aIsOffspring;
                }
                return a.order < b.order;
            });

        const int keepCount = std::min(survivorPopulationSize, static_cast<int>(ranked.size()));
        DIRTSIM_ASSERT(keepCount > 0, "Evolution: pruning would remove entire population");

        std::vector<Individual> survivors;
        std::vector<double> survivorFitness;
        std::vector<IndividualOrigin> survivorOrigins;
        survivors.reserve(keepCount);
        survivorFitness.reserve(keepCount);
        survivorOrigins.reserve(keepCount);
        for (int i = 0; i < keepCount; ++i) {
            Individual survivor = ranked[i].individual;
            survivor.parentFitness.reset();
            survivors.push_back(std::move(survivor));
            survivorFitness.push_back(ranked[i].fitness);
            survivorOrigins.push_back(IndividualOrigin::EliteCarryover);
        }

        population = std::move(survivors);
        fitnessScores = std::move(survivorFitness);
        populationOrigins = std::move(survivorOrigins);
    }

    DIRTSIM_ASSERT(!population.empty(), "Evolution: population must not be empty when breeding");
    DIRTSIM_ASSERT(
        population.size() == fitnessScores.size(),
        "Evolution: fitness scores must align with population");

    // Selection and mutation: create offspring and append them.
    std::vector<Individual> offspring;
    std::vector<IndividualOrigin> offspringOrigins;
    offspring.reserve(survivorPopulationSize);
    offspringOrigins.reserve(survivorPopulationSize);
    MutationOutcomeStats mutationStats;
    mutationStats.totalOffspring = survivorPopulationSize;

    int perturbationsTotal = 0;
    int resetsTotal = 0;
    int weightChangesMin = std::numeric_limits<int>::max();
    int weightChangesMax = 0;

    for (int i = 0; i < survivorPopulationSize; ++i) {
        const int parentIdx =
            tournamentSelectIndex(fitnessScores, evolutionConfig.tournamentSize, rng);
        const Individual& parent = population[parentIdx];
        const double parentFitness = fitnessScores[parentIdx];

        Individual child = parent;
        child.parentFitness = parentFitness;
        bool offspringMutated = false;
        int weightChanges = 0;
        if (!parent.genome.has_value()) {
            mutationStats.cloneNoGenome++;
        }
        else if (!parent.allowsMutation) {
            mutationStats.cloneMutationDisabled++;
        }
        else {
            MutationStats stats;
            Genome mutatedGenome = mutate(parent.genome.value(), mutationConfig, rng, &stats);
            perturbationsTotal += stats.perturbations;
            resetsTotal += stats.resets;
            weightChanges = stats.totalChanges();
            offspringMutated = weightChanges > 0;
            if (offspringMutated) {
                mutationStats.mutated++;
            }
            else {
                mutationStats.cloneNoMutationDelta++;
            }
            child.genome = std::move(mutatedGenome);
        }
        weightChangesMin = std::min(weightChangesMin, weightChanges);
        weightChangesMax = std::max(weightChangesMax, weightChanges);
        offspring.push_back(std::move(child));
        offspringOrigins.push_back(
            offspringMutated ? IndividualOrigin::OffspringMutated
                             : IndividualOrigin::OffspringClone);
    }

    lastBreedingPerturbationsAvg_ = survivorPopulationSize > 0
        ? static_cast<double>(perturbationsTotal) / static_cast<double>(survivorPopulationSize)
        : 0.0;
    lastBreedingResetsAvg_ = survivorPopulationSize > 0
        ? static_cast<double>(resetsTotal) / static_cast<double>(survivorPopulationSize)
        : 0.0;
    lastBreedingWeightChangesAvg_ = survivorPopulationSize > 0
        ? static_cast<double>(perturbationsTotal + resetsTotal)
            / static_cast<double>(survivorPopulationSize)
        : 0.0;
    lastBreedingWeightChangesMin_ =
        weightChangesMin == std::numeric_limits<int>::max() ? 0 : weightChangesMin;
    lastBreedingWeightChangesMax_ = weightChangesMax;

    LOG_INFO(
        State,
        "Evolution: offspring cycle gen={} total={} mutated={} clones={} (no_genome={} "
        "mutation_disabled={} no_delta={})",
        generation,
        mutationStats.totalOffspring,
        mutationStats.mutated,
        mutationStats.cloneCount(),
        mutationStats.cloneNoGenome,
        mutationStats.cloneMutationDisabled,
        mutationStats.cloneNoMutationDelta);

    population.reserve(population.size() + offspring.size());
    populationOrigins.reserve(populationOrigins.size() + offspringOrigins.size());
    for (size_t i = 0; i < offspring.size(); ++i) {
        population.push_back(offspring[i]);
        populationOrigins.push_back(offspringOrigins[i]);
    }

    pruneBeforeBreeding_ = !offspring.empty();

    // Advance to next generation.
    generation++;
    bestSnapshotVariantBroadcastCountThisGen_ = 0;

    // Only reset for next generation if we're not at the end.
    // This preserves currentEval at the generation-complete value in the final broadcast,
    // giving the UI a clean "all evals complete" signal.
    if (evolutionConfig.maxGenerations <= 0 || generation < evolutionConfig.maxGenerations) {
        currentEval = 0;
        bestFitnessThisGen = 0.0;
        bestThisGenOrigin_ = IndividualOrigin::Unknown;
        generationTelemetry_.reset();
        sumFitnessThisGen_ = 0.0;
        fitnessScores.assign(population.size(), 0.0);
    }

    adjustConcurrency();
}

void Evolution::captureLastGenerationFitnessDistribution()
{
    const int evaluatedCount = std::min(currentEval, static_cast<int>(fitnessScores.size()));
    if (evaluatedCount <= 0) {
        lastCompletedGeneration_ = -1;
        lastGenerationFitnessMin_ = 0.0;
        lastGenerationFitnessMax_ = 0.0;
        lastGenerationFitnessHistogram_.clear();
        return;
    }

    double minFitness = fitnessScores[0];
    double maxFitness = fitnessScores[0];
    for (int i = 1; i < evaluatedCount; ++i) {
        minFitness = std::min(minFitness, fitnessScores[i]);
        maxFitness = std::max(maxFitness, fitnessScores[i]);
    }

    std::vector<uint32_t> bins(kFitnessDistributionBinCount, 0);
    const double range = maxFitness - minFitness;
    if (range <= std::numeric_limits<double>::epsilon()) {
        bins[kFitnessDistributionBinCount / 2] = static_cast<uint32_t>(evaluatedCount);
    }
    else {
        for (int i = 0; i < evaluatedCount; ++i) {
            const double normalized = std::clamp((fitnessScores[i] - minFitness) / range, 0.0, 1.0);
            const size_t bin = std::min(
                kFitnessDistributionBinCount - 1,
                static_cast<size_t>(
                    normalized * static_cast<double>(kFitnessDistributionBinCount)));
            bins[bin]++;
        }
    }

    lastCompletedGeneration_ = generation;
    lastGenerationFitnessMin_ = minFitness;
    lastGenerationFitnessMax_ = maxFitness;
    lastGenerationFitnessHistogram_ = std::move(bins);
}

void Evolution::captureLastGenerationTelemetry()
{
    lastGenerationEliteCarryoverCount_ = generationTelemetry_.eliteCarryoverCount;
    lastGenerationSeedCount_ = generationTelemetry_.seedCount;
    lastGenerationOffspringCloneCount_ = generationTelemetry_.offspringCloneCount;
    lastGenerationOffspringMutatedCount_ = generationTelemetry_.offspringMutatedCount;

    lastGenerationOffspringCloneBeatsParentCount_ =
        generationTelemetry_.offspringCloneBeatsParentCount;
    lastGenerationOffspringCloneAvgDeltaFitness_ =
        generationTelemetry_.offspringCloneComparedCount > 0
        ? generationTelemetry_.offspringCloneDeltaFitnessSum
            / static_cast<double>(generationTelemetry_.offspringCloneComparedCount)
        : 0.0;

    lastGenerationOffspringMutatedBeatsParentCount_ =
        generationTelemetry_.offspringMutatedBeatsParentCount;
    lastGenerationOffspringMutatedAvgDeltaFitness_ =
        generationTelemetry_.offspringMutatedComparedCount > 0
        ? generationTelemetry_.offspringMutatedDeltaFitnessSum
            / static_cast<double>(generationTelemetry_.offspringMutatedComparedCount)
        : 0.0;

    lastGenerationPhenotypeUniqueCount_ =
        static_cast<int>(generationTelemetry_.phenotypeAll.size());
    lastGenerationPhenotypeUniqueEliteCarryoverCount_ =
        static_cast<int>(generationTelemetry_.phenotypeEliteCarryover.size());
    lastGenerationPhenotypeUniqueOffspringMutatedCount_ =
        static_cast<int>(generationTelemetry_.phenotypeOffspringMutated.size());

    int novelOffspringMutated = 0;
    for (const uint64_t hash : generationTelemetry_.phenotypeOffspringMutated) {
        if (generationTelemetry_.phenotypeEliteCarryover.find(hash)
            == generationTelemetry_.phenotypeEliteCarryover.end()) {
            novelOffspringMutated++;
        }
    }
    lastGenerationPhenotypeNovelOffspringMutatedCount_ = novelOffspringMutated;
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
    if (!trainingComplete_) {
        if (lastProgressBroadcastTime_ != std::chrono::steady_clock::time_point{}
            && now - lastProgressBroadcastTime_ < kProgressBroadcastInterval) {
            return;
        }
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
    int completedIndividuals = completedEvaluations_;
    int totalIndividuals = estimateTotalEvaluations(evolutionConfig);
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

    const size_t totalGenomeCount = dsm.getGenomeRepository().count();
    const int totalGenomeCountForProgress =
        totalGenomeCount > static_cast<size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(totalGenomeCount);

    const Api::EvolutionProgress progress{
        .generation = generation,
        .maxGenerations = evolutionConfig.maxGenerations,
        .currentEval = currentEval,
        .populationSize = static_cast<int>(population.size()),
        .totalGenomeCount = totalGenomeCountForProgress,
        .genomeArchiveMaxSize = evolutionConfig.genomeArchiveMaxSize,
        .bestFitnessThisGen = bestFitnessThisGen,
        .bestFitnessAllTime = bestAllTime,
        .averageFitness = avgFitness,
        .lastCompletedGeneration = lastCompletedGeneration_,
        .lastGenerationFitnessMin = lastGenerationFitnessMin_,
        .lastGenerationFitnessMax = lastGenerationFitnessMax_,
        .lastGenerationFitnessHistogram = lastGenerationFitnessHistogram_,
        .bestThisGenSource = toProgressSource(bestThisGenOrigin_),
        .bestGenomeId = bestGenomeId,
        .totalTrainingSeconds = totalSeconds,
        .currentSimTime = visibleSimTime,
        .cumulativeSimTime = cumulative,
        .speedupFactor = speedup,
        .etaSeconds = eta,
        .activeParallelism = activeParallelism,
        .cpuPercent = latestCpu,
        .lastBreedingPerturbationsAvg = lastBreedingPerturbationsAvg_,
        .lastBreedingResetsAvg = lastBreedingResetsAvg_,
        .lastBreedingWeightChangesAvg = lastBreedingWeightChangesAvg_,
        .lastBreedingWeightChangesMin = lastBreedingWeightChangesMin_,
        .lastBreedingWeightChangesMax = lastBreedingWeightChangesMax_,
        .lastGenerationEliteCarryoverCount = lastGenerationEliteCarryoverCount_,
        .lastGenerationSeedCount = lastGenerationSeedCount_,
        .lastGenerationOffspringCloneCount = lastGenerationOffspringCloneCount_,
        .lastGenerationOffspringMutatedCount = lastGenerationOffspringMutatedCount_,
        .lastGenerationOffspringCloneBeatsParentCount =
            lastGenerationOffspringCloneBeatsParentCount_,
        .lastGenerationOffspringCloneAvgDeltaFitness = lastGenerationOffspringCloneAvgDeltaFitness_,
        .lastGenerationOffspringMutatedBeatsParentCount =
            lastGenerationOffspringMutatedBeatsParentCount_,
        .lastGenerationOffspringMutatedAvgDeltaFitness =
            lastGenerationOffspringMutatedAvgDeltaFitness_,
        .lastGenerationPhenotypeUniqueCount = lastGenerationPhenotypeUniqueCount_,
        .lastGenerationPhenotypeUniqueEliteCarryoverCount =
            lastGenerationPhenotypeUniqueEliteCarryoverCount_,
        .lastGenerationPhenotypeUniqueOffspringMutatedCount =
            lastGenerationPhenotypeUniqueOffspringMutatedCount_,
        .lastGenerationPhenotypeNovelOffspringMutatedCount =
            lastGenerationPhenotypeNovelOffspringMutatedCount_,
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
        if (response.isError()) {
            LOG_WARN(State, "TrainingResult send failed: {}", response.errorValue());
        }
        else if (response.value().isError()) {
            LOG_WARN(
                State, "TrainingResult response error: {}", response.value().errorValue().message);
        }
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
    const GenomeId id = storeManagedGenome(
        dsm,
        population[bestIdx].genome.value(),
        meta,
        evolutionConfig.genomeArchiveMaxSize,
        "checkpoint");

    if (bestFit >= bestFitnessAllTime) {
        auto& repo = dsm.getGenomeRepository();
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
