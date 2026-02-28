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
#include "core/organisms/evolution/GenomeMetadataUtils.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/Mutation.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/StateMachine.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/EvolutionStop.h"
#include "server/api/TrainingBestPlaybackFrame.h"
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
constexpr double kBestFitnessTieRelativeEpsilon = 1e-12;
constexpr size_t kRobustFitnessSampleWindow = 7;

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

bool isDuckClockScenario(OrganismType organismType, Scenario::EnumType scenarioId)
{
    return organismType == OrganismType::DUCK && scenarioId == Scenario::EnumType::Clock;
}

int resolveRobustnessEvalCount(int configuredCount)
{
    return std::max(1, configuredCount);
}

std::optional<bool> resolveDuckClockSpawnSideOverride(
    Evolution::WorkerResult::TaskType taskType,
    OrganismType organismType,
    Scenario::EnumType scenarioId,
    int robustSampleOrdinal)
{
    if (taskType != Evolution::WorkerResult::TaskType::RobustnessEval) {
        return std::nullopt;
    }
    if (!isDuckClockScenario(organismType, scenarioId)) {
        return std::nullopt;
    }

    DIRTSIM_ASSERT(
        robustSampleOrdinal > 0,
        "Evolution: robust sample ordinal must be positive for duck clock alternation");
    return (robustSampleOrdinal % 2) != 0;
}

std::optional<bool> resolvePrimaryDuckClockSpawnSide(
    Evolution::WorkerResult::TaskType taskType,
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

struct RankedIndividual {
    double fitness = 0.0;
    Evolution::Individual individual;
    Evolution::IndividualOrigin origin = Evolution::IndividualOrigin::Unknown;
    int order = 0;
};

bool canComputeGenomeWeightDistance(
    const Evolution::Individual& left, const Evolution::Individual& right)
{
    if (!left.genome.has_value() || !right.genome.has_value()) {
        return false;
    }

    const auto& leftWeights = left.genome.value().weights;
    const auto& rightWeights = right.genome.value().weights;
    return !leftWeights.empty() && leftWeights.size() == rightWeights.size();
}

double computeGenomeWeightDistance(
    const Evolution::Individual& left, const Evolution::Individual& right)
{
    DIRTSIM_ASSERT(
        canComputeGenomeWeightDistance(left, right),
        "Evolution: comparable genomes required for distance calculation");

    const auto& leftWeights = left.genome.value().weights;
    const auto& rightWeights = right.genome.value().weights;
    DIRTSIM_ASSERT(
        leftWeights.size() == rightWeights.size(),
        "Evolution: comparable genomes must have equal weight count");
    DIRTSIM_ASSERT(!leftWeights.empty(), "Evolution: genome distance requires non-empty weights");

    double squaredDistance = 0.0;
    for (size_t i = 0; i < leftWeights.size(); ++i) {
        const double delta =
            static_cast<double>(leftWeights[i]) - static_cast<double>(rightWeights[i]);
        squaredDistance += delta * delta;
    }

    return std::sqrt(squaredDistance / static_cast<double>(leftWeights.size()));
}

bool isNearBestFitness(double fitness, double bestFitness, double fitnessEpsilon)
{
    if (fitnessTiesBest(fitness, bestFitness)) {
        return true;
    }
    return fitness + fitnessEpsilon >= bestFitness;
}

std::vector<int> selectDiversityElitePositions(
    const std::vector<RankedIndividual>& ranked,
    int keepCount,
    int diversityEliteCount,
    double diversityFitnessEpsilon)
{
    if (ranked.empty() || keepCount <= 1 || diversityEliteCount <= 0) {
        return {};
    }

    const int diverseSlots = std::min(diversityEliteCount, keepCount - 1);
    if (diverseSlots <= 0) {
        return {};
    }

    const double bestFitness = ranked.front().fitness;

    std::vector<int> candidates;
    candidates.reserve(ranked.size());
    for (int i = 1; i < static_cast<int>(ranked.size()); ++i) {
        if (!isNearBestFitness(ranked[i].fitness, bestFitness, diversityFitnessEpsilon)) {
            continue;
        }
        if (!ranked[i].individual.genome.has_value()) {
            continue;
        }
        candidates.push_back(i);
    }

    if (candidates.empty()) {
        return {};
    }

    std::vector<int> selected;
    selected.reserve(diverseSlots);
    std::vector<int> references{ 0 };
    std::vector<bool> selectedMask(ranked.size(), false);
    constexpr double kDistanceTieEpsilon = 1e-12;

    while (static_cast<int>(selected.size()) < diverseSlots) {
        int bestCandidate = -1;
        double bestMinDistance = -1.0;

        for (int candidatePos : candidates) {
            if (selectedMask[candidatePos]) {
                continue;
            }

            const Evolution::Individual& candidate = ranked[candidatePos].individual;
            bool hasComparableReference = false;
            double minDistance = std::numeric_limits<double>::infinity();

            for (int referencePos : references) {
                const Evolution::Individual& reference = ranked[referencePos].individual;
                if (!canComputeGenomeWeightDistance(candidate, reference)) {
                    continue;
                }

                hasComparableReference = true;
                const double distance = computeGenomeWeightDistance(candidate, reference);
                minDistance = std::min(minDistance, distance);
            }

            if (!hasComparableReference) {
                continue;
            }

            if (bestCandidate < 0 || minDistance > bestMinDistance + kDistanceTieEpsilon
                || (std::abs(minDistance - bestMinDistance) <= kDistanceTieEpsilon
                    && candidatePos < bestCandidate)) {
                bestCandidate = candidatePos;
                bestMinDistance = minDistance;
            }
        }

        if (bestCandidate < 0) {
            break;
        }

        selectedMask[bestCandidate] = true;
        selected.push_back(bestCandidate);
        references.push_back(bestCandidate);
    }

    return selected;
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

double computeFitnessForRunner(
    const TrainingRunner& runner,
    const TrainingRunner::Status& status,
    const std::string& brainKind,
    OrganismType organismType,
    const EvolutionConfig& evolutionConfig,
    std::optional<TreeFitnessBreakdown>* breakdownOut);

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
    const std::string& brainKind,
    OrganismType organismType,
    const EvolutionConfig& evolutionConfig,
    std::optional<TreeFitnessBreakdown>* breakdownOut)
{
    (void)brainKind;
    if (organismType == OrganismType::NES_DUCK) {
        if (breakdownOut) {
            breakdownOut->reset();
        }
        return status.nesRewardTotal;
    }

    const World* world = runner.getWorld();
    DIRTSIM_ASSERT(world != nullptr, "Evolution: TrainingRunner missing World");

    const FitnessResult result{
        .lifespan = status.lifespan,
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
        .organismTrackingHistory = &runner.getOrganismTrackingHistory(),
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

struct EvaluationPassResult {
    int commandsAccepted = 0;
    int commandsRejected = 0;
    double fitness = 0.0;
    double simTime = 0.0;
    std::vector<std::pair<std::string, int>> topCommandSignatures;
    std::vector<std::pair<std::string, int>> topCommandOutcomeSignatures;
    std::optional<Evolution::EvaluationSnapshot> snapshot;
    std::unordered_map<std::string, Evolution::TimerAggregate> timerStats;
    std::optional<TreeFitnessBreakdown> treeFitnessBreakdown;
};

std::optional<Evolution::EvaluationSnapshot> buildEvaluationSnapshotForRunner(
    const TrainingRunner& runner)
{
    const World* world = runner.getWorld();
    if (!world) {
        return std::nullopt;
    }

    Evolution::EvaluationSnapshot snapshot;
    snapshot.worldData = world->getData();
    snapshot.organismIds = world->getOrganismManager().getGrid();
    return snapshot;
}

EvaluationPassResult buildEvaluationPassResult(
    TrainingRunner& runner,
    const TrainingRunner::Status& status,
    const std::string& brainKind,
    OrganismType organismType,
    const EvolutionConfig& evolutionConfig,
    bool includeGenerationDetails)
{
    EvaluationPassResult pass;
    pass.commandsAccepted = status.commandsAccepted;
    pass.commandsRejected = status.commandsRejected;
    pass.simTime = status.simTime;

    if (!includeGenerationDetails) {
        pass.fitness = computeFitnessForRunner(
            runner, status, brainKind, organismType, evolutionConfig, nullptr);
        return pass;
    }

    pass.topCommandSignatures = runner.getTopCommandSignatures(kTopCommandSignatureLimit);
    pass.topCommandOutcomeSignatures =
        runner.getTopCommandOutcomeSignatures(kTopCommandSignatureLimit);

    std::optional<TreeFitnessBreakdown> breakdown;
    pass.fitness = computeFitnessForRunner(
        runner, status, brainKind, organismType, evolutionConfig, &breakdown);
    pass.treeFitnessBreakdown = breakdown;
    if (const World* world = runner.getWorld()) {
        pass.timerStats = collectTimerStats(world->getTimers());
    }
    pass.snapshot = buildEvaluationSnapshotForRunner(runner);
    return pass;
}

EvaluationPassResult runEvaluationPass(
    const TrainingSpec& trainingSpec,
    const TrainingRunner::Individual& individual,
    const EvolutionConfig& evolutionConfig,
    GenomeRepository& genomeRepository,
    const TrainingBrainRegistry& brainRegistry,
    std::optional<bool> duckClockSpawnLeftFirst,
    bool includeGenerationDetails,
    std::atomic<bool>* stopRequested)
{
    const TrainingRunner::Config runnerConfig{
        .brainRegistry = brainRegistry,
        .duckClockSpawnLeftFirst = duckClockSpawnLeftFirst,
        .duckClockSpawnRngSeed = std::nullopt,
    };
    TrainingRunner runner(
        trainingSpec, individual, evolutionConfig, genomeRepository, runnerConfig);

    TrainingRunner::Status status;
    while (status.state == TrainingRunner::State::Running
           && !(stopRequested && stopRequested->load())) {
        status = runner.step(1);
    }

    return buildEvaluationPassResult(
        runner,
        status,
        individual.brain.brainKind,
        trainingSpec.organismType,
        evolutionConfig,
        includeGenerationDetails);
}

void mergeTimerStats(
    std::unordered_map<std::string, Evolution::TimerAggregate>& target,
    const std::unordered_map<std::string, Evolution::TimerAggregate>& source)
{
    for (const auto& [name, aggregate] : source) {
        auto& merged = target[name];
        merged.totalMs += aggregate.totalMs;
        merged.calls += aggregate.calls;
    }
}

Evolution::WorkerResult buildWorkerResultFromPass(
    Evolution::WorkerResult::TaskType taskType,
    int index,
    int robustGeneration,
    int robustSampleOrdinal,
    EvaluationPassResult pass,
    bool includeGenerationDetails)
{
    Evolution::WorkerResult result;
    result.taskType = taskType;
    result.index = index;
    result.robustGeneration = robustGeneration;
    result.robustSampleOrdinal = robustSampleOrdinal;
    result.simTime = pass.simTime;
    result.commandsAccepted = pass.commandsAccepted;
    result.commandsRejected = pass.commandsRejected;
    result.fitness = pass.fitness;

    if (!includeGenerationDetails) {
        return result;
    }

    result.topCommandSignatures = std::move(pass.topCommandSignatures);
    result.topCommandOutcomeSignatures = std::move(pass.topCommandOutcomeSignatures);
    result.snapshot = std::move(pass.snapshot);
    result.timerStats = std::move(pass.timerStats);
    result.treeFitnessBreakdown = std::move(pass.treeFitnessBreakdown);
    return result;
}

void mergeDuckClockEvaluationPasses(
    Evolution::WorkerResult& primaryResult,
    Evolution::WorkerResult secondaryResult,
    bool includeGenerationDetails)
{
    const double primaryFitness = primaryResult.fitness;
    const bool secondaryIsWorse = secondaryResult.fitness < primaryFitness;

    primaryResult.simTime += secondaryResult.simTime;
    primaryResult.fitness = std::min(primaryFitness, secondaryResult.fitness);

    if (!includeGenerationDetails) {
        primaryResult.commandsAccepted += secondaryResult.commandsAccepted;
        primaryResult.commandsRejected += secondaryResult.commandsRejected;
        return;
    }

    if (secondaryIsWorse) {
        primaryResult.commandsAccepted = secondaryResult.commandsAccepted;
        primaryResult.commandsRejected = secondaryResult.commandsRejected;
        primaryResult.topCommandSignatures = std::move(secondaryResult.topCommandSignatures);
        primaryResult.topCommandOutcomeSignatures =
            std::move(secondaryResult.topCommandOutcomeSignatures);
        primaryResult.snapshot = std::move(secondaryResult.snapshot);
        primaryResult.treeFitnessBreakdown = std::move(secondaryResult.treeFitnessBreakdown);
        auto primaryTimerStats = std::move(primaryResult.timerStats);
        primaryResult.timerStats = std::move(secondaryResult.timerStats);
        mergeTimerStats(primaryResult.timerStats, primaryTimerStats);
        return;
    }

    mergeTimerStats(primaryResult.timerStats, secondaryResult.timerStats);
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

void broadcastTrainingBestSnapshot(
    StateMachine& dsm,
    Evolution::EvaluationSnapshot snapshot,
    double fitness,
    int generation,
    int commandsAccepted,
    int commandsRejected,
    const std::vector<std::pair<std::string, int>>& topCommandSignatures,
    const std::vector<std::pair<std::string, int>>& topCommandOutcomeSignatures)
{
    Api::TrainingBestSnapshot bestSnapshot;
    bestSnapshot.worldData = std::move(snapshot.worldData);
    bestSnapshot.organismIds = std::move(snapshot.organismIds);
    bestSnapshot.fitness = fitness;
    bestSnapshot.generation = generation;
    bestSnapshot.commandsAccepted = commandsAccepted;
    bestSnapshot.commandsRejected = commandsRejected;
    bestSnapshot.topCommandSignatures.reserve(topCommandSignatures.size());
    for (const auto& [signature, count] : topCommandSignatures) {
        bestSnapshot.topCommandSignatures.push_back(
            Api::TrainingBestSnapshot::CommandSignatureCount{
                .signature = signature,
                .count = count,
            });
    }
    bestSnapshot.topCommandOutcomeSignatures.reserve(topCommandOutcomeSignatures.size());
    for (const auto& [signature, count] : topCommandOutcomeSignatures) {
        bestSnapshot.topCommandOutcomeSignatures.push_back(
            Api::TrainingBestSnapshot::CommandSignatureCount{
                .signature = signature,
                .count = count,
            });
    }
    bestSnapshot.scenarioVideoFrame = bestSnapshot.worldData.scenario_video_frame;

    dsm.updateCachedTrainingBestSnapshot(bestSnapshot);
    dsm.broadcastEventData(
        Api::TrainingBestSnapshot::name(), Network::serialize_payload(bestSnapshot));
}

void broadcastTrainingBestPlaybackFrame(
    StateMachine& dsm,
    WorldData worldData,
    std::vector<OrganismId> organismIds,
    double fitness,
    int generation)
{
    Api::TrainingBestPlaybackFrame frame;
    frame.worldData = std::move(worldData);
    frame.organismIds = std::move(organismIds);
    frame.fitness = fitness;
    frame.generation = generation;
    frame.scenarioVideoFrame = frame.worldData.scenario_video_frame;

    dsm.broadcastEventData(
        Api::TrainingBestPlaybackFrame::name(), Network::serialize_payload(frame));
}

GenomeRepository::StoreByHashResult storeManagedGenome(
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
                "Evolution: Pruned {} managed genomes (max_per_organism_brain={})",
                pruned,
                archiveMaxSize);
        }
    }

    return storeResult;
}
} // namespace

void Evolution::onEnter(StateMachine& dsm)
{
    LOG_INFO(
        State,
        "Evolution: Starting with population={}, generations={}, scenario={}, organism_type={}",
        evolutionConfig.populationSize,
        evolutionConfig.maxGenerations,
        toString(trainingSpec.scenarioId),
        static_cast<int>(trainingSpec.organismType));

    // Record training start time.
    trainingStartTime_ = std::chrono::steady_clock::now();
    trainingComplete_ = false;
    finalAverageFitness_ = 0.0;
    finalTrainingSeconds_ = 0.0;
    streamIntervalMs_ = 16;
    bestPlaybackEnabled_ = false;
    bestPlaybackIntervalMs_ = 16;
    lastStreamBroadcastTime_ = std::chrono::steady_clock::time_point{};
    lastBestPlaybackBroadcastTime_ = std::chrono::steady_clock::time_point{};
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
    pendingBestSnapshot_.reset();
    pendingBestSnapshotCommandsAccepted_ = 0;
    pendingBestSnapshotCommandsRejected_ = 0;
    pendingBestSnapshotTopCommandSignatures_.clear();
    pendingBestSnapshotTopCommandOutcomeSignatures_.clear();
    timerStatsAggregate_.clear();
    dsm.clearCachedTrainingBestSnapshot();
    visibleRunner_.reset();
    visibleQueue_.clear();
    visibleEvalIndex_ = -1;
    visibleEvalIsRobustness_ = false;
    visibleDuckSecondPassActive_ = false;
    visibleDuckPrimaryPassResult_.reset();
    visibleRobustSampleOrdinal_ = 0;
    bestPlaybackIndividual_.reset();
    clearBestPlaybackRunner();
    bestPlaybackFitness_ = 0.0;
    bestPlaybackGeneration_ = 0;
    bestPlaybackDuckNextPrimarySpawnLeftFirst_ = true;
    workerState_ = std::make_unique<WorkerState>();

    // Seed RNG.
    rng.seed(std::random_device{}());

    brainRegistry_ = TrainingBrainRegistry::createDefault();

    // Initialize population.
    initializePopulation(dsm);

    evolutionConfig.maxParallelEvaluations = resolveParallelEvaluations(
        evolutionConfig.maxParallelEvaluations, static_cast<int>(population.size()));

    // Initialize CPU telemetry.
    cpuMetrics_ = std::make_unique<SystemMetrics>();
    cpuSamples_.clear();
    lastCpuPercent_ = 0.0;
    lastCpuPercentPerCore_.clear();
    lastCpuSampleTime_ = {};
    cpuMetrics_->get(); // Prime the delta with an initial reading.

    startWorkers(dsm);
    queueGenerationTasks();
}

void Evolution::onExit(StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Exiting at generation {}, eval {}", generation, currentEval);
    stopWorkers();
    clearBestPlaybackRunner();
    bestPlaybackIndividual_.reset();
    cpuMetrics_.reset();
    cpuSamples_.clear();
    lastCpuPercent_ = 0.0;
    lastCpuPercentPerCore_.clear();
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
            const auto metrics = cpuMetrics_->get();
            lastCpuPercent_ = metrics.cpu_percent;
            lastCpuPercentPerCore_ = metrics.cpu_percent_per_core;
            if (evolutionConfig.targetCpuPercent > 0) {
                cpuSamples_.push_back(metrics.cpu_percent);
            }
        }
    }

    drainResults(dsm);
    if (!trainingComplete_) {
        startNextVisibleEvaluation(dsm);
        stepVisibleEvaluation(dsm);
        stepBestPlayback(dsm);
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

    // Include in-flight visible runner timers so callers can profile active evaluations.
    std::unordered_map<std::string, TimerAggregate> mergedStats = timerStatsAggregate_;
    if (visibleRunner_) {
        if (const World* world = visibleRunner_->getWorld()) {
            mergeTimerStats(mergedStats, collectTimerStats(world->getTimers()));
        }
    }

    Api::TimerStatsGet::Okay okay;
    for (const auto& [name, aggregate] : mergedStats) {
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
    if (cwc.command.bestPlaybackIntervalMs < 1) {
        cwc.sendResponse(
            Api::TrainingStreamConfigSet::Response::error(
                ApiError("TrainingStreamConfigSet bestPlaybackIntervalMs must be >= 1")));
        return std::move(*this);
    }

    streamIntervalMs_ = cwc.command.intervalMs;
    bestPlaybackEnabled_ = cwc.command.bestPlaybackEnabled;
    bestPlaybackIntervalMs_ = cwc.command.bestPlaybackIntervalMs;
    lastStreamBroadcastTime_ = std::chrono::steady_clock::time_point{};
    lastBestPlaybackBroadcastTime_ = std::chrono::steady_clock::time_point{};
    if (!bestPlaybackEnabled_) {
        clearBestPlaybackRunner();
    }

    LOG_INFO(
        State,
        "Evolution: Training stream config updated (interval={}ms, best_playback={}, "
        "best_playback_interval={}ms)",
        streamIntervalMs_,
        bestPlaybackEnabled_,
        bestPlaybackIntervalMs_);

    Api::TrainingStreamConfigSet::Okay response{
        .intervalMs = streamIntervalMs_,
        .bestPlaybackEnabled = bestPlaybackEnabled_,
        .bestPlaybackIntervalMs = bestPlaybackIntervalMs_,
        .message = "Training stream config updated",
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
                                .scenarioId = trainingSpec.scenarioId,
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
                                .scenarioId = trainingSpec.scenarioId,
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
                                .scenarioId = trainingSpec.scenarioId,
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
    robustEvaluationCount_ = 0;
    bestThisGenOrigin_ = IndividualOrigin::Unknown;
    lastCompletedGeneration_ = -1;
    lastGenerationFitnessMin_ = 0.0;
    lastGenerationFitnessMax_ = 0.0;
    lastGenerationFitnessHistogram_.clear();
    pruneBeforeBreeding_ = false;
    completedEvaluations_ = 0;
    sumFitnessThisGen_ = 0.0;
    pendingBestRobustness_ = false;
    pendingBestRobustnessGeneration_ = -1;
    pendingBestRobustnessIndex_ = -1;
    pendingBestRobustnessFirstSample_ = 0.0;
    pendingBestSnapshot_.reset();
    pendingBestSnapshotCommandsAccepted_ = 0;
    pendingBestSnapshotCommandsRejected_ = 0;
    pendingBestSnapshotTopCommandSignatures_.clear();
    pendingBestSnapshotTopCommandOutcomeSignatures_.clear();
    robustnessPassActive_ = false;
    robustnessPassGeneration_ = -1;
    robustnessPassIndex_ = -1;
    robustnessPassTargetEvalCount_ = 0;
    robustnessPassPendingSamples_ = 0;
    robustnessPassCompletedSamples_ = 0;
    robustnessPassVisibleSamplesRemaining_ = 0;
    robustnessPassNextVisibleSampleOrdinal_ = 1;
    robustnessPassSamples_.clear();

    visibleRunner_.reset();
    visibleQueue_.clear();
    visibleEvalIndex_ = -1;
    visibleEvalIsRobustness_ = false;
    visibleDuckSecondPassActive_ = false;
    visibleDuckPrimaryPassResult_.reset();
    visibleRobustSampleOrdinal_ = 0;
    visibleScenarioConfig_ = Config::Empty{};
    visibleScenarioId_ = trainingSpec.scenarioId;
    bestPlaybackIndividual_.reset();
    clearBestPlaybackRunner();
    bestPlaybackFitness_ = 0.0;
    bestPlaybackGeneration_ = 0;
    bestPlaybackDuckNextPrimarySpawnLeftFirst_ = true;
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
    visibleEvalIsRobustness_ = false;
    visibleDuckSecondPassActive_ = false;
    visibleDuckPrimaryPassResult_.reset();
    visibleRobustSampleOrdinal_ = 0;
    clearBestPlaybackRunner();
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
    if (visibleRunner_) {
        return;
    }

    if (robustnessPassActive_ && robustnessPassVisibleSamplesRemaining_ > 0) {
        if (robustnessPassIndex_ < 0
            || robustnessPassIndex_ >= static_cast<int>(population.size())) {
            return;
        }

        visibleEvalIndex_ = robustnessPassIndex_;
        visibleEvalIsRobustness_ = true;
        visibleDuckSecondPassActive_ = false;
        visibleDuckPrimaryPassResult_.reset();
        visibleRobustSampleOrdinal_ = robustnessPassNextVisibleSampleOrdinal_++;
        robustnessPassVisibleSamplesRemaining_--;

        const Individual& individual = population[visibleEvalIndex_];
        const std::optional<bool> spawnSideOverride = resolvePrimaryDuckClockSpawnSide(
            WorkerResult::TaskType::RobustnessEval,
            trainingSpec.organismType,
            individual.scenarioId,
            visibleRobustSampleOrdinal_);
        const TrainingRunner::Config runnerConfig{
            .brainRegistry = brainRegistry_,
            .duckClockSpawnLeftFirst = spawnSideOverride,
            .duckClockSpawnRngSeed = std::nullopt,
        };
        visibleRunner_ = std::make_unique<TrainingRunner>(
            trainingSpec,
            makeRunnerIndividual(individual),
            evolutionConfig,
            dsm.getGenomeRepository(),
            runnerConfig);
        visibleScenarioConfig_ = visibleRunner_->getScenarioConfig();
        visibleScenarioId_ = individual.scenarioId;
        return;
    }

    if (robustnessPassActive_ || visibleQueue_.empty()) {
        return;
    }

    visibleEvalIndex_ = visibleQueue_.front();
    visibleQueue_.pop_front();
    visibleEvalIsRobustness_ = false;
    visibleDuckSecondPassActive_ = false;
    visibleDuckPrimaryPassResult_.reset();
    visibleRobustSampleOrdinal_ = 0;

    const Individual& individual = population[visibleEvalIndex_];
    const std::optional<bool> spawnSideOverride = resolvePrimaryDuckClockSpawnSide(
        WorkerResult::TaskType::GenerationEval,
        trainingSpec.organismType,
        individual.scenarioId,
        visibleRobustSampleOrdinal_);
    const TrainingRunner::Config runnerConfig{
        .brainRegistry = brainRegistry_,
        .duckClockSpawnLeftFirst = spawnSideOverride,
        .duckClockSpawnRngSeed = std::nullopt,
    };

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
        const WorkerResult::TaskType taskType = visibleEvalIsRobustness_
            ? WorkerResult::TaskType::RobustnessEval
            : WorkerResult::TaskType::GenerationEval;
        const bool includeGenerationDetails = !visibleEvalIsRobustness_;
        EvaluationPassResult completedPass = buildEvaluationPassResult(
            *visibleRunner_,
            status,
            population[visibleEvalIndex_].brainKind,
            trainingSpec.organismType,
            evolutionConfig,
            includeGenerationDetails);
        WorkerResult passResult = buildWorkerResultFromPass(
            taskType,
            visibleEvalIndex_,
            visibleEvalIsRobustness_ ? robustnessPassGeneration_ : -1,
            visibleEvalIsRobustness_ ? visibleRobustSampleOrdinal_ : 0,
            std::move(completedPass),
            includeGenerationDetails);

        const Individual& individual = population[visibleEvalIndex_];
        const bool duckClockVisibleEval =
            isDuckClockScenario(trainingSpec.organismType, individual.scenarioId);
        if (duckClockVisibleEval && !visibleDuckSecondPassActive_) {
            const std::optional<bool> primarySpawnSide = resolvePrimaryDuckClockSpawnSide(
                taskType,
                trainingSpec.organismType,
                individual.scenarioId,
                visibleRobustSampleOrdinal_);
            const std::optional<bool> secondarySpawnSide = primarySpawnSide.has_value()
                ? std::optional<bool>(!primarySpawnSide.value())
                : std::optional<bool>(false);
            const TrainingRunner::Config runnerConfig{
                .brainRegistry = brainRegistry_,
                .duckClockSpawnLeftFirst = secondarySpawnSide,
                .duckClockSpawnRngSeed = std::nullopt,
            };
            visibleRunner_ = std::make_unique<TrainingRunner>(
                trainingSpec,
                makeRunnerIndividual(individual),
                evolutionConfig,
                dsm.getGenomeRepository(),
                runnerConfig);
            visibleScenarioConfig_ = visibleRunner_->getScenarioConfig();
            visibleScenarioId_ = individual.scenarioId;
            visibleDuckPrimaryPassResult_ = std::move(passResult);
            visibleDuckSecondPassActive_ = true;
        }
        else {
            WorkerResult result;
            if (duckClockVisibleEval && visibleDuckSecondPassActive_) {
                DIRTSIM_ASSERT(
                    visibleDuckPrimaryPassResult_.has_value(),
                    "Evolution: visible duck second pass missing stored primary result");
                result = std::move(visibleDuckPrimaryPassResult_.value());
                mergeDuckClockEvaluationPasses(
                    result, std::move(passResult), includeGenerationDetails);
            }
            else {
                result = std::move(passResult);
            }

            processResult(dsm, std::move(result));
            visibleRunner_.reset();
            visibleEvalIndex_ = -1;
            visibleEvalIsRobustness_ = false;
            visibleDuckSecondPassActive_ = false;
            visibleDuckPrimaryPassResult_.reset();
            visibleRobustSampleOrdinal_ = 0;
        }
    }

    if (shouldBroadcast || evalComplete) {
        broadcastProgress(dsm);
    }
}

Evolution::WorkerResult Evolution::runEvaluationTask(WorkerTask const& task, WorkerState& state)
{
    DIRTSIM_ASSERT(task.index >= 0, "Evolution: Invalid evaluation index");
    DIRTSIM_ASSERT(state.genomeRepository != nullptr, "Evolution: GenomeRepository missing");

    const bool includeGenerationDetails = task.taskType == WorkerResult::TaskType::GenerationEval;
    const std::optional<bool> primarySpawnSide = resolvePrimaryDuckClockSpawnSide(
        task.taskType,
        state.trainingSpec.organismType,
        task.individual.scenarioId,
        task.robustSampleOrdinal);
    EvaluationPassResult primaryPass = runEvaluationPass(
        state.trainingSpec,
        makeRunnerIndividual(task.individual),
        state.evolutionConfig,
        *state.genomeRepository,
        state.brainRegistry,
        primarySpawnSide,
        includeGenerationDetails,
        &state.stopRequested);

    WorkerResult result = buildWorkerResultFromPass(
        task.taskType,
        task.index,
        task.robustGeneration,
        task.robustSampleOrdinal,
        std::move(primaryPass),
        includeGenerationDetails);

    if (isDuckClockScenario(state.trainingSpec.organismType, task.individual.scenarioId)) {
        const std::optional<bool> secondarySpawnSide = primarySpawnSide.has_value()
            ? std::optional<bool>(!primarySpawnSide.value())
            : std::optional<bool>(false);
        EvaluationPassResult secondaryPass = runEvaluationPass(
            state.trainingSpec,
            makeRunnerIndividual(task.individual),
            state.evolutionConfig,
            *state.genomeRepository,
            state.brainRegistry,
            secondarySpawnSide,
            includeGenerationDetails,
            &state.stopRequested);

        WorkerResult secondaryResult = buildWorkerResultFromPass(
            task.taskType,
            task.index,
            task.robustGeneration,
            task.robustSampleOrdinal,
            std::move(secondaryPass),
            includeGenerationDetails);
        mergeDuckClockEvaluationPasses(
            result, std::move(secondaryResult), includeGenerationDetails);
        return result;
    }
    return result;
}

void Evolution::processResult(StateMachine& dsm, WorkerResult result)
{
    if (result.index < 0 || result.index >= static_cast<int>(population.size())) {
        return;
    }

    if (result.taskType == WorkerResult::TaskType::RobustnessEval) {
        handleRobustnessSampleResult(dsm, result);
        maybeCompleteGeneration(dsm);
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

    const Individual& individual = population[result.index];
    if (!individual.genome.has_value()) {
        const bool firstEvaluationThisGeneration = currentEval == 1;
        if (firstEvaluationThisGeneration || result.fitness > bestFitnessThisGen) {
            bestFitnessThisGen = result.fitness;
            bestThisGenOrigin_ = origin;
        }
    }

    if (result.fitness > bestFitnessAllTime) {
        if (individual.genome.has_value()) {
            const bool replacePendingCandidate = !pendingBestRobustness_
                || pendingBestRobustnessGeneration_ != generation
                || result.fitness > pendingBestRobustnessFirstSample_;
            if (replacePendingCandidate) {
                pendingBestRobustness_ = true;
                pendingBestRobustnessGeneration_ = generation;
                pendingBestRobustnessIndex_ = result.index;
                pendingBestRobustnessFirstSample_ = result.fitness;
                pendingBestSnapshot_ = std::move(result.snapshot);
                pendingBestSnapshotCommandsAccepted_ = result.commandsAccepted;
                pendingBestSnapshotCommandsRejected_ = result.commandsRejected;
                pendingBestSnapshotTopCommandSignatures_ = std::move(result.topCommandSignatures);
                pendingBestSnapshotTopCommandOutcomeSignatures_ =
                    std::move(result.topCommandOutcomeSignatures);
                if (!pendingBestSnapshot_.has_value()) {
                    LOG_WARN(
                        State,
                        "Evolution: Missing snapshot for pending robust best (gen={} eval={})",
                        generation,
                        result.index);
                }
                LOG_INFO(
                    State,
                    "Evolution: Best candidate {:.4f} at gen {} eval {} "
                    "(queued robust validation)",
                    result.fitness,
                    generation,
                    result.index);
            }
        }
        else {
            bestFitnessAllTime = result.fitness;
            pendingBestRobustness_ = false;
            pendingBestRobustnessGeneration_ = -1;
            pendingBestRobustnessIndex_ = -1;
            pendingBestRobustnessFirstSample_ = 0.0;
            pendingBestSnapshot_.reset();
            pendingBestSnapshotCommandsAccepted_ = 0;
            pendingBestSnapshotCommandsRejected_ = 0;
            pendingBestSnapshotTopCommandSignatures_.clear();
            pendingBestSnapshotTopCommandOutcomeSignatures_.clear();
            bestGenomeId = INVALID_GENOME_ID;
            LOG_INFO(
                State,
                "Evolution: Best fitness updated {:.4f} at gen {} eval {}",
                result.fitness,
                generation,
                result.index);
            setBestPlaybackSource(individual, result.fitness, generation);
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

void Evolution::startRobustnessPass(StateMachine& /*dsm*/)
{
    if (robustnessPassActive_) {
        return;
    }

    if (!pendingBestRobustness_) {
        return;
    }

    if (pendingBestRobustnessGeneration_ != generation) {
        pendingBestRobustness_ = false;
        return;
    }

    if (pendingBestRobustnessIndex_ < 0
        || pendingBestRobustnessIndex_ >= static_cast<int>(population.size())) {
        pendingBestRobustness_ = false;
        return;
    }

    const Individual& candidate = population[pendingBestRobustnessIndex_];
    if (!candidate.genome.has_value()) {
        pendingBestRobustness_ = false;
        return;
    }

    robustnessPassActive_ = true;
    robustnessPassGeneration_ = generation;
    robustnessPassIndex_ = pendingBestRobustnessIndex_;
    robustnessPassTargetEvalCount_ =
        resolveRobustnessEvalCount(evolutionConfig.robustFitnessEvaluationCount);
    robustnessPassCompletedSamples_ = 0;
    robustnessPassPendingSamples_ = robustnessPassTargetEvalCount_;
    robustnessPassVisibleSamplesRemaining_ = 0;
    robustnessPassNextVisibleSampleOrdinal_ = 1;
    robustnessPassSamples_.clear();

    pendingBestRobustness_ = false;

    const bool hasWorkerPool = workerState_ && workerState_->backgroundWorkerCount > 0;
    if (robustnessPassPendingSamples_ > 0) {
        robustnessPassVisibleSamplesRemaining_ = hasWorkerPool ? 1 : robustnessPassPendingSamples_;
    }
    const int workerSampleCount =
        robustnessPassPendingSamples_ - robustnessPassVisibleSamplesRemaining_;

    LOG_INFO(
        State,
        "Evolution: Starting robust pass for gen {} eval {} "
        "(target evals={}, extra samples={}, visible samples={}, worker samples={})",
        robustnessPassGeneration_,
        robustnessPassIndex_,
        robustnessPassTargetEvalCount_,
        robustnessPassPendingSamples_,
        robustnessPassVisibleSamplesRemaining_,
        workerSampleCount);

    if (workerSampleCount <= 0) {
        return;
    }

    const int firstWorkerSampleOrdinal = 1 + robustnessPassVisibleSamplesRemaining_;
    {
        std::lock_guard<std::mutex> lock(workerState_->taskMutex);
        for (int i = 0; i < workerSampleCount; ++i) {
            workerState_->taskQueue.push_back(
                WorkerTask{
                    .taskType = WorkerResult::TaskType::RobustnessEval,
                    .index = robustnessPassIndex_,
                    .robustGeneration = robustnessPassGeneration_,
                    .robustSampleOrdinal = firstWorkerSampleOrdinal + i,
                    .individual = candidate,
                });
        }
    }

    workerState_->taskCv.notify_all();
}

void Evolution::handleRobustnessSampleResult(StateMachine& dsm, const WorkerResult& result)
{
    if (!robustnessPassActive_) {
        return;
    }

    if (result.robustGeneration != robustnessPassGeneration_) {
        return;
    }

    if (result.index != robustnessPassIndex_) {
        return;
    }

    if (robustnessPassPendingSamples_ <= 0) {
        return;
    }

    robustnessPassSamples_.push_back(result.fitness);
    robustnessPassPendingSamples_--;
    robustnessPassCompletedSamples_++;

    LOG_INFO(
        State,
        "Evolution: Robust sample {}/{} for gen {} eval {} = {:.4f}",
        robustnessPassCompletedSamples_,
        robustnessPassTargetEvalCount_,
        robustnessPassGeneration_,
        robustnessPassIndex_,
        result.fitness);

    broadcastProgress(dsm);
}

void Evolution::finalizeRobustnessPass(StateMachine& dsm)
{
    if (!robustnessPassActive_) {
        return;
    }

    if (robustnessPassPendingSamples_ > 0) {
        return;
    }

    if (robustnessPassSamples_.size() > kRobustFitnessSampleWindow) {
        robustnessPassSamples_.erase(
            robustnessPassSamples_.begin(),
            robustnessPassSamples_.end()
                - static_cast<std::vector<double>::difference_type>(kRobustFitnessSampleWindow));
    }

    if (robustnessPassIndex_ < 0 || robustnessPassIndex_ >= static_cast<int>(population.size())) {
        robustnessPassActive_ = false;
        robustnessPassGeneration_ = -1;
        robustnessPassIndex_ = -1;
        robustnessPassTargetEvalCount_ = 0;
        robustnessPassPendingSamples_ = 0;
        robustnessPassCompletedSamples_ = 0;
        robustnessPassVisibleSamplesRemaining_ = 0;
        robustnessPassNextVisibleSampleOrdinal_ = 1;
        robustnessPassSamples_.clear();
        return;
    }

    const Individual& individual = population[robustnessPassIndex_];
    if (!individual.genome.has_value()) {
        robustnessPassActive_ = false;
        robustnessPassGeneration_ = -1;
        robustnessPassIndex_ = -1;
        robustnessPassTargetEvalCount_ = 0;
        robustnessPassPendingSamples_ = 0;
        robustnessPassCompletedSamples_ = 0;
        robustnessPassVisibleSamplesRemaining_ = 0;
        robustnessPassNextVisibleSampleOrdinal_ = 1;
        robustnessPassSamples_.clear();
        return;
    }

    const double robustFitness = computeMedian(robustnessPassSamples_);
    const double firstSampleFitness = std::isfinite(pendingBestRobustnessFirstSample_)
        ? pendingBestRobustnessFirstSample_
        : (robustnessPassSamples_.empty() ? 0.0 : robustnessPassSamples_.front());
    const GenomeMetadata meta{
        .name = "gen_" + std::to_string(robustnessPassGeneration_) + "_eval_"
            + std::to_string(robustnessPassIndex_),
        .fitness = firstSampleFitness,
        .robustFitness = robustFitness,
        .robustEvalCount = robustnessPassTargetEvalCount_,
        .robustFitnessSamples = robustnessPassSamples_,
        .generation = robustnessPassGeneration_,
        .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
        .scenarioId = individual.scenarioId,
        .notes = "",
        .organismType = trainingSpec.organismType,
        .brainKind = individual.brainKind,
        .brainVariant = individual.brainVariant,
        .trainingSessionId = trainingSessionId_,
    };

    bestFitnessThisGen = robustFitness;
    robustEvaluationCount_++;
    if (robustnessPassIndex_ >= 0
        && robustnessPassIndex_ < static_cast<int>(populationOrigins.size())) {
        bestThisGenOrigin_ = populationOrigins[robustnessPassIndex_];
    }
    else {
        bestThisGenOrigin_ = IndividualOrigin::Unknown;
    }

    auto& repo = dsm.getGenomeRepository();
    const bool hasSessionBest = !bestGenomeId.isNil();
    const bool robustBestUpdated = !hasSessionBest
        || (!fitnessTiesBest(robustFitness, bestFitnessAllTime)
            && robustFitness > bestFitnessAllTime);
    if (robustBestUpdated) {
        const auto storeResult = storeManagedGenome(
            dsm,
            individual.genome.value(),
            meta,
            evolutionConfig.genomeArchiveMaxSize,
            "current-session best (robust pass)");
        repo.markAsBest(storeResult.id);
        bestGenomeId = storeResult.id;
        bestFitnessAllTime = robustFitness;
        setBestPlaybackSource(individual, robustFitness, robustnessPassGeneration_);
        LOG_INFO(
            State,
            "Evolution: Promoted genome {} as current-session best (robust {:.4f})",
            storeResult.id.toShortString(),
            robustFitness);

        if (pendingBestSnapshot_.has_value()) {
            broadcastTrainingBestSnapshot(
                dsm,
                std::move(pendingBestSnapshot_.value()),
                robustFitness,
                robustnessPassGeneration_,
                pendingBestSnapshotCommandsAccepted_,
                pendingBestSnapshotCommandsRejected_,
                pendingBestSnapshotTopCommandSignatures_,
                pendingBestSnapshotTopCommandOutcomeSignatures_);
        }
        else {
            LOG_WARN(
                State,
                "Evolution: Missing snapshot for robust best broadcast (gen={} eval={})",
                robustnessPassGeneration_,
                robustnessPassIndex_);
        }
    }

    LOG_INFO(
        State,
        "Evolution: Finalized robust pass for gen {} eval {} (robust {:.4f}, evals {})",
        robustnessPassGeneration_,
        robustnessPassIndex_,
        robustFitness,
        robustnessPassTargetEvalCount_);

    robustnessPassActive_ = false;
    robustnessPassGeneration_ = -1;
    robustnessPassIndex_ = -1;
    robustnessPassTargetEvalCount_ = 0;
    robustnessPassPendingSamples_ = 0;
    robustnessPassCompletedSamples_ = 0;
    robustnessPassVisibleSamplesRemaining_ = 0;
    robustnessPassNextVisibleSampleOrdinal_ = 1;
    robustnessPassSamples_.clear();
    pendingBestSnapshot_.reset();
    pendingBestSnapshotCommandsAccepted_ = 0;
    pendingBestSnapshotCommandsRejected_ = 0;
    pendingBestSnapshotTopCommandSignatures_.clear();
    pendingBestSnapshotTopCommandOutcomeSignatures_.clear();
}

void Evolution::maybeCompleteGeneration(StateMachine& dsm)
{
    const int generationPopulationSize = static_cast<int>(population.size());
    if (currentEval < generationPopulationSize) {
        return;
    }

    startRobustnessPass(dsm);
    if (robustnessPassActive_) {
        if (robustnessPassPendingSamples_ > 0) {
            return;
        }
        finalizeRobustnessPass(dsm);
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
        "Evolution: Generation {} complete. Last robust={:.4f}, All-time={:.4f}, robust_passes={}",
        generation,
        bestFitnessThisGen,
        bestFitnessAllTime,
        robustEvaluationCount_);
    DIRTSIM_ASSERT(!robustnessPassActive_, "Evolution: robust pass must complete before advance");
    pendingBestRobustness_ = false;
    pendingBestRobustnessGeneration_ = -1;
    pendingBestRobustnessIndex_ = -1;
    pendingBestRobustnessFirstSample_ = 0.0;

    // Store best genome periodically.
    if (generation % saveInterval == 0) {
        storeBestGenome(dsm);
    }

    const int survivorPopulationSize = evolutionConfig.populationSize;
    DIRTSIM_ASSERT(survivorPopulationSize > 0, "Evolution: survivor population must be positive");

    if (pruneBeforeBreeding_) {
        // Prune only after the expanded population has been fully evaluated.
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

        const std::vector<int> diversityElitePositions = selectDiversityElitePositions(
            ranked,
            keepCount,
            evolutionConfig.diversityEliteCount,
            evolutionConfig.diversityEliteFitnessEpsilon);
        if (!diversityElitePositions.empty()) {
            LOG_INFO(
                State,
                "Evolution: Diversity elitism retained {} near-best genome(s) (epsilon={:.4f})",
                diversityElitePositions.size(),
                evolutionConfig.diversityEliteFitnessEpsilon);
        }

        std::vector<bool> selectedMask(ranked.size(), false);
        std::vector<int> selectedPositions;
        selectedPositions.reserve(keepCount);
        const auto selectPosition = [&](int position) {
            if (position < 0 || position >= static_cast<int>(ranked.size())) {
                return;
            }
            if (selectedMask[position]) {
                return;
            }
            selectedMask[position] = true;
            selectedPositions.push_back(position);
        };

        selectPosition(0);
        for (int position : diversityElitePositions) {
            if (static_cast<int>(selectedPositions.size()) >= keepCount) {
                break;
            }
            selectPosition(position);
        }
        for (int i = 1; i < static_cast<int>(ranked.size())
             && static_cast<int>(selectedPositions.size()) < keepCount;
             ++i) {
            selectPosition(i);
        }

        DIRTSIM_ASSERT(
            static_cast<int>(selectedPositions.size()) == keepCount,
            "Evolution: selected survivor count mismatch after pruning");

        std::vector<Individual> survivors;
        std::vector<double> survivorFitness;
        std::vector<IndividualOrigin> survivorOrigins;
        survivors.reserve(keepCount);
        survivorFitness.reserve(keepCount);
        survivorOrigins.reserve(keepCount);
        for (int position : selectedPositions) {
            Individual survivor = ranked[position].individual;
            survivor.parentFitness.reset();
            survivors.push_back(std::move(survivor));
            survivorFitness.push_back(ranked[position].fitness);
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

    // Only reset for next generation if we're not at the end.
    // This preserves currentEval at the generation-complete value in the final broadcast,
    // giving the UI a clean "all evals complete" signal.
    if (evolutionConfig.maxGenerations <= 0 || generation < evolutionConfig.maxGenerations) {
        currentEval = 0;
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

void Evolution::clearBestPlaybackRunner()
{
    bestPlaybackRunner_.reset();
    bestPlaybackDuckSecondPassActive_ = false;
    bestPlaybackDuckPrimarySpawnLeftFirst_ = true;
}

void Evolution::setBestPlaybackSource(const Individual& individual, double fitness, int generation)
{
    bestPlaybackIndividual_ = individual;
    bestPlaybackIndividual_->parentFitness.reset();
    bestPlaybackFitness_ = fitness;
    bestPlaybackGeneration_ = generation;
    bestPlaybackDuckNextPrimarySpawnLeftFirst_ = true;
    lastBestPlaybackBroadcastTime_ = std::chrono::steady_clock::time_point{};
    clearBestPlaybackRunner();
}

void Evolution::stepBestPlayback(StateMachine& dsm)
{
    if (!bestPlaybackEnabled_ || !bestPlaybackIndividual_.has_value()) {
        return;
    }

    const auto startRunner = [&](std::optional<bool> spawnSideOverride) {
        const TrainingRunner::Config runnerConfig{
            .brainRegistry = brainRegistry_,
            .duckClockSpawnLeftFirst = spawnSideOverride,
            .duckClockSpawnRngSeed = std::nullopt,
        };
        bestPlaybackRunner_ = std::make_unique<TrainingRunner>(
            trainingSpec,
            makeRunnerIndividual(bestPlaybackIndividual_.value()),
            evolutionConfig,
            dsm.getGenomeRepository(),
            runnerConfig);
    };

    const bool duckClockScenario =
        isDuckClockScenario(trainingSpec.organismType, bestPlaybackIndividual_->scenarioId);
    if (!bestPlaybackRunner_) {
        const std::optional<bool> primarySpawnSide = duckClockScenario
            ? std::optional<bool>(bestPlaybackDuckNextPrimarySpawnLeftFirst_)
            : std::nullopt;
        bestPlaybackDuckPrimarySpawnLeftFirst_ = primarySpawnSide.value_or(true);
        bestPlaybackDuckSecondPassActive_ = false;
        startRunner(primarySpawnSide);
    }

    // Always advance the sim every tick to play back at real speed.
    const TrainingRunner::Status status = bestPlaybackRunner_->step(1);

    // Broadcast frames at the configured interval, independent of sim step rate.
    const auto now = std::chrono::steady_clock::now();
    const auto interval = std::chrono::milliseconds(bestPlaybackIntervalMs_);
    if (lastBestPlaybackBroadcastTime_ == std::chrono::steady_clock::time_point{}
        || now - lastBestPlaybackBroadcastTime_ >= interval) {
        lastBestPlaybackBroadcastTime_ = now;
        World* world = bestPlaybackRunner_->getWorld();
        DIRTSIM_ASSERT(world != nullptr, "Evolution: Best playback runner missing World");
        broadcastTrainingBestPlaybackFrame(
            dsm,
            world->getData(),
            world->getOrganismManager().getGrid(),
            bestPlaybackFitness_,
            bestPlaybackGeneration_);
    }

    if (status.state == TrainingRunner::State::Running) {
        return;
    }

    if (duckClockScenario && !bestPlaybackDuckSecondPassActive_) {
        bestPlaybackDuckSecondPassActive_ = true;
        startRunner(std::optional<bool>(!bestPlaybackDuckPrimarySpawnLeftFirst_));
        return;
    }

    if (duckClockScenario) {
        bestPlaybackDuckNextPrimarySpawnLeftFirst_ = !bestPlaybackDuckNextPrimarySpawnLeftFirst_;
    }
    clearBestPlaybackRunner();
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

    auto& repo = dsm.getGenomeRepository();
    const bool hasAllTimeFitness = completedEvaluations_ > 0 && std::isfinite(bestFitnessAllTime)
        && bestFitnessAllTime > std::numeric_limits<double>::lowest();
    const double bestAllTime =
        (!bestGenomeId.isNil() || hasAllTimeFitness) ? bestFitnessAllTime : 0.0;

    // Compute CPU auto-tune fields.
    int activeParallelism = evolutionConfig.maxParallelEvaluations;
    double latestCpu = lastCpuPercent_;
    if (workerState_) {
        activeParallelism = workerState_->allowedConcurrency.load() + 1; // +1 for main thread.
    }

    const size_t totalGenomeCount = repo.count();
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
        .robustEvaluationCount = robustEvaluationCount_,
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
        .cpuPercentPerCore = lastCpuPercentPerCore_,
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
        .robustFitness = 0.0,
        .robustEvalCount = 0,
        .robustFitnessSamples = {},
        .generation = generation,
        .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
        .scenarioId = trainingSpec.scenarioId,
        .notes = "",
        .organismType = trainingSpec.organismType,
        .brainKind = population[bestIdx].brainKind,
        .brainVariant = population[bestIdx].brainVariant,
        .trainingSessionId = trainingSessionId_,
    };
    const auto storeResult = storeManagedGenome(
        dsm,
        population[bestIdx].genome.value(),
        meta,
        evolutionConfig.genomeArchiveMaxSize,
        "checkpoint");

    auto& repo = dsm.getGenomeRepository();
    bestGenomeId = repo.getBestId().value_or(INVALID_GENOME_ID);
    (void)storeResult;

    LOG_INFO(
        State, "Evolution: Stored checkpoint genome (gen {}, fitness {:.4f})", generation, bestFit);
}

UnsavedTrainingResult Evolution::buildUnsavedTrainingResult()
{
    UnsavedTrainingResult result;
    result.evolutionConfig = evolutionConfig;
    result.mutationConfig = mutationConfig;
    result.trainingSpec = trainingSpec;
    result.summary.scenarioId = trainingSpec.scenarioId;
    result.summary.organismType = trainingSpec.organismType;
    result.summary.populationSize = evolutionConfig.populationSize;
    result.summary.maxGenerations = evolutionConfig.maxGenerations;
    result.summary.completedGenerations = evolutionConfig.maxGenerations;
    result.summary.bestFitness = bestGenomeId.isNil() ? 0.0 : bestFitnessAllTime;
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
            .robustFitness = candidate.fitness,
            .robustEvalCount = 1,
            .robustFitnessSamples = { candidate.fitness },
            .generation = generationIndex,
            .createdTimestamp = now,
            .scenarioId = trainingSpec.scenarioId,
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
