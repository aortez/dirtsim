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
#include "core/organisms/evolution/FitnessResult.h"
#include "core/organisms/evolution/GenomeMetadataUtils.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/Mutation.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "core/scenarios/nes/NesTileBrainMetadata.h"
#include "core/scenarios/nes/NesTileTokenizer.h"
#include "core/scenarios/nes/NesTileTokenizerBootstrapper.h"
#include "server/StateMachine.h"
#include "server/api/EvolutionMutationControlsSet.h"
#include "server/api/EvolutionPauseSet.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/EvolutionStop.h"
#include "server/api/TrainingBestPlaybackFrame.h"
#include "server/api/TrainingBestSnapshot.h"
#include "server/api/TrainingResult.h"
#include "server/evolution/TrainingBestSnapshotGenerator.h"
#include <algorithm>
#include <array>
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
constexpr size_t kTelemetrySignatureLimit = 6;
constexpr size_t kFitnessDistributionBinCount = 16;
constexpr double kBestFitnessTieRelativeEpsilon = 1e-12;
constexpr size_t kRobustFitnessSampleWindow = 7;
constexpr int kMutationControlPerturbationsMax = 5000;
constexpr int kMutationControlRecoveryWindowMax = 100;
constexpr int kMutationControlResetsMax = 200;
constexpr double kMutationControlSigmaMax = 0.3;
constexpr int kMutationControlStagnationWindowMax = 100;

constexpr std::string_view adaptiveMutationModeName(AdaptiveMutationMode mode)
{
    switch (mode) {
        case AdaptiveMutationMode::Baseline:
            return "baseline";
        case AdaptiveMutationMode::Explore:
            return "explore";
        case AdaptiveMutationMode::Rescue:
            return "rescue";
        case AdaptiveMutationMode::Recover:
            return "recover";
    }
    return "unknown";
}

constexpr std::string_view adaptiveMutationControlModeName(AdaptiveMutationControlMode mode)
{
    switch (mode) {
        case AdaptiveMutationControlMode::Auto:
            return "auto";
        case AdaptiveMutationControlMode::Baseline:
            return "baseline";
        case AdaptiveMutationControlMode::Explore:
            return "explore";
        case AdaptiveMutationControlMode::Rescue:
            return "rescue";
    }
    return "unknown";
}

constexpr std::string_view trainingPhaseName(TrainingPhase phase)
{
    switch (phase) {
        case TrainingPhase::Normal:
            return "normal";
        case TrainingPhase::Plateau:
            return "plateau";
        case TrainingPhase::Stuck:
            return "stuck";
        case TrainingPhase::Recovery:
            return "recovery";
    }
    return "unknown";
}

int clampArchiveMetric(size_t value)
{
    return value > static_cast<size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(value);
}

AdaptiveMutationControlMode adaptiveMutationControlModeSanitize(AdaptiveMutationControlMode mode)
{
    switch (mode) {
        case AdaptiveMutationControlMode::Auto:
        case AdaptiveMutationControlMode::Baseline:
        case AdaptiveMutationControlMode::Explore:
        case AdaptiveMutationControlMode::Rescue:
            return mode;
    }

    return AdaptiveMutationControlMode::Auto;
}

Api::EvolutionMutationControlsSet::Okay evolutionMutationControlsApply(
    Evolution& evolution, const Api::EvolutionMutationControlsSet::Command& command)
{
    evolution.mutationConfig.perturbationsPerOffspring = std::clamp(
        command.mutationConfig.perturbationsPerOffspring, 0, kMutationControlPerturbationsMax);
    evolution.mutationConfig.resetsPerOffspring =
        std::clamp(command.mutationConfig.resetsPerOffspring, 0, kMutationControlResetsMax);
    evolution.mutationConfig.sigma =
        std::clamp(command.mutationConfig.sigma, 0.0, kMutationControlSigmaMax);
    if (!std::isfinite(evolution.mutationConfig.sigma)) {
        evolution.mutationConfig.sigma = MutationConfig{}.sigma;
    }

    evolution.evolutionConfig.stagnationWindowGenerations =
        std::clamp(command.stagnationWindowGenerations, 1, kMutationControlStagnationWindowMax);
    evolution.evolutionConfig.recoveryWindowGenerations =
        std::clamp(command.recoveryWindowGenerations, 0, kMutationControlRecoveryWindowMax);
    evolution.mutationControlMode_ = adaptiveMutationControlModeSanitize(command.controlMode);

    return Api::EvolutionMutationControlsSet::Okay{
        .mutationConfig = evolution.mutationConfig,
        .stagnationWindowGenerations = evolution.evolutionConfig.stagnationWindowGenerations,
        .recoveryWindowGenerations = evolution.evolutionConfig.recoveryWindowGenerations,
        .controlMode = evolution.mutationControlMode_,
    };
}

std::vector<std::string> managedArchiveBrainKindsGet(const TrainingSpec& trainingSpec)
{
    std::vector<std::string> brainKinds;
    brainKinds.reserve(trainingSpec.population.size());
    for (const auto& populationSpec : trainingSpec.population) {
        if (populationSpec.brainKind.empty()) {
            continue;
        }
        brainKinds.push_back(populationSpec.brainKind);
    }

    std::sort(brainKinds.begin(), brainKinds.end());
    brainKinds.erase(std::unique(brainKinds.begin(), brainKinds.end()), brainKinds.end());
    return brainKinds;
}

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

int resolveRobustnessPassSampleCount(int configuredCount)
{
    if (configuredCount <= 0) {
        return 0;
    }

    return configuredCount;
}

uint64_t computePhenotypeHash(const EvolutionSupport::CompletedEvaluation& result)
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

TrainingRunner::Individual makeRunnerIndividual(const Evolution::Individual& individual)
{
    TrainingRunner::Individual runner;
    runner.brain.brainKind = individual.brainKind;
    runner.brain.brainVariant = individual.brainVariant;
    runner.scenarioId = individual.scenarioId;
    runner.genome = individual.genome;
    return runner;
}

bool requiresNesTileTokenizer(const Evolution::Individual& individual)
{
    return individual.brainKind == TrainingBrainKind::NesTileRecurrent;
}

bool trainingRequiresNesTileTokenizer(const TrainingSpec& trainingSpec)
{
    return std::any_of(
        trainingSpec.population.begin(), trainingSpec.population.end(), [](const auto& spec) {
            return spec.brainKind == TrainingBrainKind::NesTileRecurrent;
        });
}

std::optional<NesTileBrainCompatibilityMetadata> brainCompatibilityMetadataForIndividual(
    const Evolution::Individual& individual,
    const std::optional<NesTileBrainCompatibilityMetadata>& nesTileCompatibility)
{
    if (individual.brainKind == TrainingBrainKind::NesTileRecurrent) {
        return nesTileCompatibility;
    }
    return std::nullopt;
}

EvolutionSupport::EvaluationIndividual makeEvaluationIndividual(
    const Evolution::Individual& individual)
{
    return EvolutionSupport::EvaluationIndividual{
        .brainKind = individual.brainKind,
        .brainVariant = individual.brainVariant,
        .scenarioId = individual.scenarioId,
        .genome = individual.genome,
    };
}

EvolutionSupport::EvaluationRequest makeEvaluationRequest(
    const Evolution::Individual& individual, int index)
{
    return EvolutionSupport::EvaluationRequest{
        .taskType = EvolutionSupport::EvaluationTaskType::GenerationEval,
        .index = index,
        .robustGeneration = -1,
        .robustSampleOrdinal = 0,
        .individual = makeEvaluationIndividual(individual),
    };
}

void mergeTimerStats(
    std::unordered_map<std::string, EvolutionSupport::EvaluationTimerAggregate>& target,
    const std::unordered_map<std::string, EvolutionSupport::EvaluationTimerAggregate>& source)
{
    for (const auto& [name, aggregate] : source) {
        auto& merged = target[name];
        merged.totalMs += aggregate.totalMs;
        merged.calls += aggregate.calls;
    }
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
    EvolutionSupport::EvaluationSnapshot snapshot,
    const EvolutionSupport::FitnessEvaluation& fitnessEvaluation,
    const EvolutionSupport::FitnessModelBundle& fitnessModel,
    int generation,
    int commandsAccepted,
    int commandsRejected,
    const std::vector<std::pair<std::string, int>>& topCommandSignatures,
    const std::vector<std::pair<std::string, int>>& topCommandOutcomeSignatures)
{
    Api::TrainingBestSnapshot bestSnapshot = EvolutionSupport::trainingBestSnapshotBuild(
        std::move(snapshot.worldData),
        std::move(snapshot.organismIds),
        fitnessEvaluation,
        fitnessModel,
        generation,
        commandsAccepted,
        commandsRejected,
        topCommandSignatures,
        topCommandOutcomeSignatures,
        std::move(snapshot.scenarioVideoFrame));
    dsm.updateCachedTrainingBestSnapshot(bestSnapshot);
    dsm.broadcastEventData(
        Api::TrainingBestSnapshot::name(), Network::serialize_payload(bestSnapshot));
}

void broadcastTrainingBestPlaybackFrame(
    StateMachine& dsm,
    WorldData worldData,
    std::vector<OrganismId> organismIds,
    double fitness,
    int generation,
    std::optional<NesControllerTelemetry> nesControllerTelemetry,
    std::optional<ScenarioVideoFrame> scenarioVideoFrame)
{
    Api::TrainingBestPlaybackFrame frame;
    frame.worldData = std::move(worldData);
    frame.organismIds = std::move(organismIds);
    frame.fitness = fitness;
    frame.generation = generation;
    frame.nesControllerTelemetry = std::move(nesControllerTelemetry);
    frame.scenarioVideoFrame = std::move(scenarioVideoFrame);

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

void Evolution::BestPlaybackState::reset()
{
    individual.reset();
    nesTileTokenizer.reset();
    runner.reset();
    fitness = 0.0;
    generation = 0;
    duckSecondPassActive = false;
    duckNextPrimarySpawnLeftFirst = true;
    duckPrimarySpawnLeftFirst = true;
}

void Evolution::BestPlaybackState::clearRunner()
{
    runner.reset();
    duckSecondPassActive = false;
    duckPrimarySpawnLeftFirst = true;
}

void Evolution::PendingBestState::reset()
{
    robustness = false;
    robustnessGeneration = -1;
    robustnessIndex = -1;
    robustnessFirstSample = 0.0;
    snapshot.reset();
    snapshotFitnessEvaluation.reset();
    snapshotCommandsAccepted = 0;
    snapshotCommandsRejected = 0;
    snapshotTopCommandSignatures.clear();
    snapshotTopCommandOutcomeSignatures.clear();
}

void Evolution::PendingBestState::resetTrigger()
{
    robustness = false;
    robustnessGeneration = -1;
    robustnessIndex = -1;
    robustnessFirstSample = 0.0;
}

void Evolution::RobustnessPassState::reset()
{
    active = false;
    generation = -1;
    index = -1;
    targetEvalCount = 0;
    pendingSamples = 0;
    completedSamples = 0;
    samples.clear();
}

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
    totalPausedDuration_ = std::chrono::steady_clock::duration::zero();
    trainingPauseStartTime_ = std::chrono::steady_clock::time_point{};
    trainingPaused_ = false;
    lastBestPlaybackBroadcastTime_ = std::chrono::steady_clock::time_point{};
    lastProgressBroadcastTime_ = std::chrono::steady_clock::time_point{};
    trainingSessionId_ = UUID::generate();
    pendingTrainingResult_.reset();
    cumulativeSimTime_ = 0.0;
    sumFitnessThisGen_ = 0.0;
    generationTelemetry_.reset();
    lastGenTelemetry_ = {};
    pendingBest_.reset();
    timerStatsAggregate_.clear();
    dsm.clearCachedTrainingBestSnapshot();
    scenarioConfigOverride_.reset();
    fitnessModel_ =
        EvolutionSupport::fitnessModelResolve(trainingSpec.organismType, trainingSpec.scenarioId);
    switch (trainingSpec.scenarioId) {
        case Scenario::EnumType::Benchmark:
            break;
        case Scenario::EnumType::Clock:
            scenarioConfigOverride_ = dsm.getUserSettings().clockScenarioConfig;
            break;
        case Scenario::EnumType::DamBreak:
            break;
        case Scenario::EnumType::Empty:
            break;
        case Scenario::EnumType::GooseTest:
            break;
        case Scenario::EnumType::Lights:
            break;
        case Scenario::EnumType::NesFlappyParatroopa:
            break;
        case Scenario::EnumType::NesSuperMarioBros:
            break;
        case Scenario::EnumType::NesSuperTiltBro:
            break;
        case Scenario::EnumType::Sandbox:
            scenarioConfigOverride_ = dsm.getUserSettings().sandboxScenarioConfig;
            break;
        case Scenario::EnumType::Raining:
            scenarioConfigOverride_ = dsm.getUserSettings().rainingScenarioConfig;
            break;
        case Scenario::EnumType::TreeGermination:
            scenarioConfigOverride_ = dsm.getUserSettings().treeGerminationScenarioConfig;
            break;
        case Scenario::EnumType::WaterEqualization:
            break;
    }

    // Deterministic scenarios don't benefit from repeated robust evaluations.
    const auto* scenarioMeta = dsm.getScenarioRegistry().getMetadata(trainingSpec.scenarioId);
    if (scenarioMeta && scenarioMeta->deterministicEvaluation) {
        evolutionConfig.robustFitnessEvaluationCount = 0;
        evolutionConfig.warmStartMinRobustEvalCount = 1;
    }
    genomePoolId_ = scenarioMeta ? scenarioMeta->genomePoolId : GenomePoolId::DirtSim;

    nesTileTokenizer_.reset();
    nesTileBrainCompatibility_.reset();
    if (trainingRequiresNesTileTokenizer(trainingSpec)) {
        auto tokenizerResult =
            NesTileTokenizerBootstrapper::build(trainingSpec.scenarioId, scenarioConfigOverride_);
        DIRTSIM_ASSERT(
            tokenizerResult.isValue(),
            "Evolution: Failed to bootstrap NES tile tokenizer: " + tokenizerResult.errorValue());
        nesTileTokenizer_ = std::move(tokenizerResult).value();
        nesTileBrainCompatibility_ = makeNesTileBrainCompatibilityMetadata(*nesTileTokenizer_);
    }

    bestPlayback_.reset();
    executor_.reset();

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

    executor_ = std::make_unique<EvolutionSupport::EvaluationExecutor>(
        EvolutionSupport::EvaluationExecutor::Config{
            .trainingSpec = trainingSpec,
            .brainRegistry = brainRegistry_,
            .genomeRepository = &dsm.getGenomeRepository(),
            .fitnessModel = fitnessModel_,
            .nesTileTokenizer = nesTileTokenizer_,
        });
    executor_->start(evolutionConfig.maxParallelEvaluations);
    queueGenerationTasks();
}

void Evolution::onExit(StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Exiting at generation {}, eval {}", generation, currentEval);
    if (executor_) {
        executor_->stop();
    }
    bestPlayback_.reset();
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
    if (!trainingPaused_ && cpuMetrics_) {
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
        const auto visibleTick = executor_ ? executor_->visibleTick(
                                                 std::chrono::steady_clock::now(),
                                                 dsm.getUserSettings().uiTraining.streamIntervalMs)
                                           : EvolutionSupport::VisibleTickResult{};
        if (visibleTick.frame.has_value()) {
            dsm.broadcastRenderMessage(
                visibleTick.frame->worldData,
                visibleTick.frame->organismIds,
                visibleTick.frame->scenarioId,
                visibleTick.frame->scenarioConfig,
                visibleTick.frame->nesControllerTelemetry,
                visibleTick.frame->scenarioVideoFrame);
        }
        for (auto& result : visibleTick.completed) {
            processResult(dsm, result);
        }
        if (visibleTick.progressed || !visibleTick.completed.empty()) {
            broadcastProgress(dsm);
        }
        if (!trainingPaused_) {
            stepBestPlayback(dsm);
        }
    }

    if (trainingComplete_) {
        auto nextState = broadcastTrainingResult(dsm);
        if (nextState.has_value()) {
            return nextState;
        }
    }
    return std::nullopt;
}

Any Evolution::onEvent(const Api::EvolutionPauseSet::Cwc& cwc, StateMachine& dsm)
{
    pauseSet(cwc.command.paused, dsm);

    Api::EvolutionPauseSet::Okay okay{ .paused = trainingPaused_ };
    cwc.sendResponse(Api::EvolutionPauseSet::Response::okay(std::move(okay)));
    return std::move(*this);
}

Any Evolution::onEvent(const Api::EvolutionMutationControlsSet::Cwc& cwc, StateMachine& dsm)
{
    const Api::EvolutionMutationControlsSet::Okay applied =
        evolutionMutationControlsApply(*this, cwc.command);

    LOG_INFO(
        State,
        "Evolution: mutation controls updated control_mode={} perturbations={} resets={} "
        "sigma={:.4f} stagnation_window={} recovery_window={}",
        adaptiveMutationControlModeName(applied.controlMode),
        applied.mutationConfig.perturbationsPerOffspring,
        applied.mutationConfig.resetsPerOffspring,
        applied.mutationConfig.sigma,
        applied.stagnationWindowGenerations,
        applied.recoveryWindowGenerations);

    cwc.sendResponse(Api::EvolutionMutationControlsSet::Response::okay(applied));
    lastProgressBroadcastTime_ = std::chrono::steady_clock::time_point{};
    broadcastProgress(dsm);
    return std::move(*this);
}

Any Evolution::onEvent(const Api::EvolutionStop::Cwc& cwc, StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Stopping at generation {}, eval {}", generation, currentEval);
    if (executor_) {
        executor_->stop();
    }
    bestPlayback_.clearRunner();
    storeBestGenome(dsm);
    cwc.sendResponse(Api::EvolutionStop::Response::okay(std::monostate{}));
    return Idle{};
}

Any Evolution::onEvent(const Api::TimerStatsGet::Cwc& cwc, StateMachine& /*dsm*/)
{
    using Response = Api::TimerStatsGet::Response;

    // Include in-flight visible runner timers so callers can profile active evaluations.
    auto mergedStats = timerStatsAggregate_;
    if (executor_) {
        mergeTimerStats(mergedStats, executor_->visibleTimerStatsCollect());
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

Any Evolution::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Evolution: Exit received, shutting down");
    if (executor_) {
        executor_->stop();
    }
    bestPlayback_.clearRunner();
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
    wingUpSecondsPerIndividual.resize(population.size(), 0.0);

    generation = 0;
    currentEval = 0;
    bestFitnessThisGen = 0.0;
    bestFitnessAllTime = std::numeric_limits<double>::lowest();
    bestGenomeId = INVALID_GENOME_ID;
    robustEvaluationCount_ = 0;
    bestThisGenOrigin_ = IndividualOrigin::Unknown;
    lastCompletedGeneration_ = -1;
    lastGenerationAverageFitness_ = 0.0;
    lastGenerationFitnessMin_ = 0.0;
    lastGenerationFitnessMax_ = 0.0;
    lastGenerationFitnessHistogram_.clear();
    pruneBeforeBreeding_ = false;
    completedEvaluations_ = 0;
    lastEffectiveAdaptiveMutation_ = {
        .mode = AdaptiveMutationMode::Baseline,
        .mutationConfig = mutationConfig,
    };
    sumFitnessThisGen_ = 0.0;
    pendingBest_.reset();
    robustnessPass_.reset();
    trainingPhaseTracker_.reset();
    bestPlayback_.reset();
}

void Evolution::queueGenerationTasks()
{
    if (!executor_) {
        return;
    }

    std::vector<EvolutionSupport::EvaluationRequest> requests;
    requests.reserve(population.size());
    for (int i = 0; i < static_cast<int>(population.size()); ++i) {
        requests.push_back(makeEvaluationRequest(population[i], i));
    }

    executor_->generationBatchSubmit(requests, evolutionConfig, scenarioConfigOverride_);
}

void Evolution::drainResults(StateMachine& dsm)
{
    if (!executor_) {
        return;
    }

    std::vector<EvolutionSupport::CompletedEvaluation> results = executor_->completedDrain();
    for (auto& result : results) {
        processResult(dsm, result);
    }

    if (!results.empty()) {
        broadcastProgress(dsm);
    }
}
void Evolution::processResult(StateMachine& dsm, EvolutionSupport::CompletedEvaluation result)
{
    if (result.index < 0 || result.index >= static_cast<int>(population.size())) {
        return;
    }

    if (result.taskType == EvolutionSupport::EvaluationTaskType::RobustnessEval) {
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
                const double delta =
                    result.fitnessEvaluation.totalFitness - individual.parentFitness.value();
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
                const double delta =
                    result.fitnessEvaluation.totalFitness - individual.parentFitness.value();
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

    fitnessScores[result.index] = result.fitnessEvaluation.totalFitness;
    sumFitnessThisGen_ += result.fitnessEvaluation.totalFitness;

    const std::optional<double> wingUpSeconds =
        EvolutionSupport::fitnessEvaluationWingUpSecondsGet(result.fitnessEvaluation);
    if (wingUpSeconds.has_value()) {
        wingUpSecondsPerIndividual[result.index] = wingUpSeconds.value();
    }
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
        if (firstEvaluationThisGeneration
            || result.fitnessEvaluation.totalFitness > bestFitnessThisGen) {
            bestFitnessThisGen = result.fitnessEvaluation.totalFitness;
            bestThisGenOrigin_ = origin;
        }
    }

    if (result.fitnessEvaluation.totalFitness > bestFitnessAllTime) {
        if (individual.genome.has_value()) {
            const bool replacePendingCandidate = !pendingBest_.robustness
                || pendingBest_.robustnessGeneration != generation
                || result.fitnessEvaluation.totalFitness > pendingBest_.robustnessFirstSample;
            if (replacePendingCandidate) {
                pendingBest_.robustness = true;
                pendingBest_.robustnessGeneration = generation;
                pendingBest_.robustnessIndex = result.index;
                pendingBest_.robustnessFirstSample = result.fitnessEvaluation.totalFitness;
                pendingBest_.snapshot = std::move(result.snapshot);
                pendingBest_.snapshotFitnessEvaluation = result.fitnessEvaluation;
                pendingBest_.snapshotCommandsAccepted = result.commandsAccepted;
                pendingBest_.snapshotCommandsRejected = result.commandsRejected;
                pendingBest_.snapshotTopCommandSignatures = std::move(result.topCommandSignatures);
                pendingBest_.snapshotTopCommandOutcomeSignatures =
                    std::move(result.topCommandOutcomeSignatures);
                if (!pendingBest_.snapshot.has_value()) {
                    LOG_WARN(
                        State,
                        "Evolution: Missing snapshot for pending robust best (gen={} eval={})",
                        generation,
                        result.index);
                }
                const char* validationMode =
                    resolveRobustnessPassSampleCount(evolutionConfig.robustFitnessEvaluationCount)
                        > 0
                    ? "queued robust validation"
                    : "queued single-sample finalization";
                LOG_INFO(
                    State,
                    "Evolution: Best candidate {:.4f} at gen {} eval {} ({})",
                    result.fitnessEvaluation.totalFitness,
                    generation,
                    result.index,
                    validationMode);
            }
        }
        else {
            bestFitnessAllTime = result.fitnessEvaluation.totalFitness;
            pendingBest_.reset();
            bestGenomeId = INVALID_GENOME_ID;
            LOG_INFO(
                State,
                "Evolution: Best fitness updated {:.4f} at gen {} eval {}",
                result.fitnessEvaluation.totalFitness,
                generation,
                result.index);
            setBestPlaybackSource(individual, result.fitnessEvaluation.totalFitness, generation);
        }
    }

    const int generationPopulationSize = static_cast<int>(population.size());
    const std::string logSummary = fitnessModel_.formatLogSummary
        ? fitnessModel_.formatLogSummary(result.fitnessEvaluation)
        : std::string{};
    if (!logSummary.empty()) {
        LOG_INFO(
            State,
            "Evolution: gen={} eval={}/{} fitness={:.4f} ({})",
            generation,
            currentEval,
            generationPopulationSize,
            result.fitnessEvaluation.totalFitness,
            logSummary);
    }
    else {
        LOG_INFO(
            State,
            "Evolution: gen={} eval={}/{} fitness={:.4f}",
            generation,
            currentEval,
            generationPopulationSize,
            result.fitnessEvaluation.totalFitness);
    }

    maybeCompleteGeneration(dsm);
}

void Evolution::startRobustnessPass(StateMachine& /*dsm*/)
{
    if (robustnessPass_.active) {
        return;
    }

    if (!pendingBest_.robustness) {
        return;
    }

    if (pendingBest_.robustnessGeneration != generation) {
        pendingBest_.robustness = false;
        return;
    }

    if (pendingBest_.robustnessIndex < 0
        || pendingBest_.robustnessIndex >= static_cast<int>(population.size())) {
        pendingBest_.robustness = false;
        return;
    }

    const Individual& candidate = population[pendingBest_.robustnessIndex];
    if (!candidate.genome.has_value()) {
        pendingBest_.robustness = false;
        return;
    }

    const int targetSampleCount =
        resolveRobustnessPassSampleCount(evolutionConfig.robustFitnessEvaluationCount);
    if (targetSampleCount <= 0) {
        return;
    }

    robustnessPass_.active = true;
    robustnessPass_.generation = generation;
    robustnessPass_.index = pendingBest_.robustnessIndex;
    robustnessPass_.targetEvalCount = targetSampleCount;
    robustnessPass_.completedSamples = 0;
    robustnessPass_.pendingSamples = robustnessPass_.targetEvalCount;
    robustnessPass_.samples.clear();

    pendingBest_.robustness = false;

    LOG_INFO(
        State,
        "Evolution: Starting robust pass for gen {} eval {} "
        "(validation samples={}, pending samples={}, worker samples={})",
        robustnessPass_.generation,
        robustnessPass_.index,
        robustnessPass_.targetEvalCount,
        robustnessPass_.pendingSamples,
        robustnessPass_.pendingSamples);

    if (!executor_ || robustnessPass_.pendingSamples <= 0) {
        return;
    }

    executor_->robustnessPassSubmit(
        EvolutionSupport::EvaluationRequest{
            .taskType = EvolutionSupport::EvaluationTaskType::RobustnessEval,
            .index = robustnessPass_.index,
            .robustGeneration = robustnessPass_.generation,
            .robustSampleOrdinal = 0,
            .individual = makeEvaluationIndividual(candidate),
        },
        robustnessPass_.pendingSamples,
        evolutionConfig,
        scenarioConfigOverride_);
}

void Evolution::finalizePendingBestWithoutRobustnessPass(StateMachine& dsm)
{
    if (!pendingBest_.robustness) {
        return;
    }

    if (pendingBest_.robustnessGeneration != generation) {
        pendingBest_.reset();
        return;
    }

    if (pendingBest_.robustnessIndex < 0
        || pendingBest_.robustnessIndex >= static_cast<int>(population.size())) {
        pendingBest_.reset();
        return;
    }

    const Individual& individual = population[pendingBest_.robustnessIndex];
    if (!individual.genome.has_value()) {
        pendingBest_.reset();
        return;
    }

    const double bestFitness = pendingBest_.robustnessFirstSample;
    const GenomeMetadata meta{
        .name = "gen_" + std::to_string(pendingBest_.robustnessGeneration) + "_eval_"
            + std::to_string(pendingBest_.robustnessIndex),
        .fitness = bestFitness,
        .robustFitness = bestFitness,
        .robustEvalCount = 1,
        .robustFitnessSamples = { bestFitness },
        .generation = pendingBest_.robustnessGeneration,
        .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
        .scenarioId = individual.scenarioId,
        .notes = "",
        .organismType = trainingSpec.organismType,
        .brainKind = individual.brainKind,
        .brainVariant = individual.brainVariant,
        .trainingSessionId = trainingSessionId_,
        .genomePoolId = genomePoolId_,
        .nesTileBrainCompatibility =
            brainCompatibilityMetadataForIndividual(individual, nesTileBrainCompatibility_),
    };

    bestFitnessThisGen = bestFitness;
    if (pendingBest_.robustnessIndex >= 0
        && pendingBest_.robustnessIndex < static_cast<int>(populationOrigins.size())) {
        bestThisGenOrigin_ = populationOrigins[pendingBest_.robustnessIndex];
    }
    else {
        bestThisGenOrigin_ = IndividualOrigin::Unknown;
    }

    auto& repo = dsm.getGenomeRepository();
    const bool hasSessionBest = !bestGenomeId.isNil();
    const bool bestUpdated = !hasSessionBest
        || (!fitnessTiesBest(bestFitness, bestFitnessAllTime) && bestFitness > bestFitnessAllTime);
    if (bestUpdated) {
        const auto storeResult = storeManagedGenome(
            dsm,
            individual.genome.value(),
            meta,
            evolutionConfig.genomeArchiveMaxSize,
            "current-session best (single-sample)");
        repo.markAsBest(storeResult.id);
        bestGenomeId = storeResult.id;
        bestFitnessAllTime = bestFitness;
        setBestPlaybackSource(individual, bestFitness, pendingBest_.robustnessGeneration);
        LOG_INFO(
            State,
            "Evolution: Promoted genome {} as current-session best "
            "(single-sample {:.4f})",
            storeResult.id.toShortString(),
            bestFitness);

        if (pendingBest_.snapshot.has_value()) {
            EvolutionSupport::FitnessEvaluation snapshotFitnessEvaluation =
                pendingBest_.snapshotFitnessEvaluation.value_or(
                    EvolutionSupport::FitnessEvaluation{
                        .totalFitness = bestFitness,
                        .details = std::monostate{},
                    });
            broadcastTrainingBestSnapshot(
                dsm,
                std::move(pendingBest_.snapshot.value()),
                snapshotFitnessEvaluation,
                fitnessModel_,
                pendingBest_.robustnessGeneration,
                pendingBest_.snapshotCommandsAccepted,
                pendingBest_.snapshotCommandsRejected,
                pendingBest_.snapshotTopCommandSignatures,
                pendingBest_.snapshotTopCommandOutcomeSignatures);
        }
        else {
            LOG_WARN(
                State,
                "Evolution: Missing snapshot for single-sample best broadcast (gen={} eval={})",
                pendingBest_.robustnessGeneration,
                pendingBest_.robustnessIndex);
        }
    }

    LOG_INFO(
        State,
        "Evolution: Finalized pending best for gen {} eval {} without robust pass "
        "(fitness {:.4f})",
        pendingBest_.robustnessGeneration,
        pendingBest_.robustnessIndex,
        bestFitness);

    pendingBest_.reset();
}

void Evolution::handleRobustnessSampleResult(
    StateMachine& dsm, const EvolutionSupport::CompletedEvaluation& result)
{
    if (!robustnessPass_.active) {
        return;
    }

    if (result.robustGeneration != robustnessPass_.generation) {
        return;
    }

    if (result.index != robustnessPass_.index) {
        return;
    }

    if (robustnessPass_.pendingSamples <= 0) {
        return;
    }

    robustnessPass_.samples.push_back(result.fitnessEvaluation.totalFitness);
    robustnessPass_.pendingSamples--;
    robustnessPass_.completedSamples++;

    LOG_INFO(
        State,
        "Evolution: Robust sample {}/{} for gen {} eval {} = {:.4f}",
        robustnessPass_.completedSamples,
        robustnessPass_.targetEvalCount,
        robustnessPass_.generation,
        robustnessPass_.index,
        result.fitnessEvaluation.totalFitness);

    broadcastProgress(dsm);
}

void Evolution::finalizeRobustnessPass(StateMachine& dsm)
{
    if (!robustnessPass_.active) {
        return;
    }

    if (robustnessPass_.pendingSamples > 0) {
        return;
    }

    if (robustnessPass_.samples.size() > kRobustFitnessSampleWindow) {
        robustnessPass_.samples.erase(
            robustnessPass_.samples.begin(),
            robustnessPass_.samples.end()
                - static_cast<std::vector<double>::difference_type>(kRobustFitnessSampleWindow));
    }

    if (robustnessPass_.index < 0 || robustnessPass_.index >= static_cast<int>(population.size())) {
        robustnessPass_.reset();
        return;
    }

    const Individual& individual = population[robustnessPass_.index];
    if (!individual.genome.has_value()) {
        robustnessPass_.reset();
        return;
    }

    const double robustFitness = computeMedian(robustnessPass_.samples);
    const double firstSampleFitness = std::isfinite(pendingBest_.robustnessFirstSample)
        ? pendingBest_.robustnessFirstSample
        : (robustnessPass_.samples.empty() ? 0.0 : robustnessPass_.samples.front());
    const GenomeMetadata meta{
        .name = "gen_" + std::to_string(robustnessPass_.generation) + "_eval_"
            + std::to_string(robustnessPass_.index),
        .fitness = firstSampleFitness,
        .robustFitness = robustFitness,
        .robustEvalCount = robustnessPass_.targetEvalCount,
        .robustFitnessSamples = robustnessPass_.samples,
        .generation = robustnessPass_.generation,
        .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
        .scenarioId = individual.scenarioId,
        .notes = "",
        .organismType = trainingSpec.organismType,
        .brainKind = individual.brainKind,
        .brainVariant = individual.brainVariant,
        .trainingSessionId = trainingSessionId_,
        .genomePoolId = genomePoolId_,
        .nesTileBrainCompatibility =
            brainCompatibilityMetadataForIndividual(individual, nesTileBrainCompatibility_),
    };

    bestFitnessThisGen = robustFitness;
    robustEvaluationCount_++;
    if (robustnessPass_.index >= 0
        && robustnessPass_.index < static_cast<int>(populationOrigins.size())) {
        bestThisGenOrigin_ = populationOrigins[robustnessPass_.index];
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
        setBestPlaybackSource(individual, robustFitness, robustnessPass_.generation);
        LOG_INFO(
            State,
            "Evolution: Promoted genome {} as current-session best (robust {:.4f})",
            storeResult.id.toShortString(),
            robustFitness);

        if (pendingBest_.snapshot.has_value()) {
            EvolutionSupport::FitnessEvaluation snapshotFitnessEvaluation =
                pendingBest_.snapshotFitnessEvaluation.value_or(
                    EvolutionSupport::FitnessEvaluation{
                        .totalFitness = robustFitness,
                        .details = std::monostate{},
                    });
            broadcastTrainingBestSnapshot(
                dsm,
                std::move(pendingBest_.snapshot.value()),
                snapshotFitnessEvaluation,
                fitnessModel_,
                robustnessPass_.generation,
                pendingBest_.snapshotCommandsAccepted,
                pendingBest_.snapshotCommandsRejected,
                pendingBest_.snapshotTopCommandSignatures,
                pendingBest_.snapshotTopCommandOutcomeSignatures);
        }
        else {
            LOG_WARN(
                State,
                "Evolution: Missing snapshot for robust best broadcast (gen={} eval={})",
                robustnessPass_.generation,
                robustnessPass_.index);
        }
    }

    LOG_INFO(
        State,
        "Evolution: Finalized robust pass for gen {} eval {} (robust {:.4f}, evals {})",
        robustnessPass_.generation,
        robustnessPass_.index,
        robustFitness,
        robustnessPass_.targetEvalCount);

    robustnessPass_.reset();
    pendingBest_.reset();
}

void Evolution::maybeCompleteGeneration(StateMachine& dsm)
{
    const int generationPopulationSize = static_cast<int>(population.size());
    if (currentEval < generationPopulationSize) {
        return;
    }

    if (resolveRobustnessPassSampleCount(evolutionConfig.robustFitnessEvaluationCount) <= 0) {
        finalizePendingBestWithoutRobustnessPass(dsm);
    }

    startRobustnessPass(dsm);
    if (robustnessPass_.active) {
        if (robustnessPass_.pendingSamples > 0) {
            return;
        }
        finalizeRobustnessPass(dsm);
    }

    updateTrainingPhaseTelemetry();
    captureLastGenerationFitnessDistribution();
    captureLastGenerationTelemetry();

    if (evolutionConfig.maxGenerations > 0 && generation + 1 >= evolutionConfig.maxGenerations) {
        finalAverageFitness_ =
            generationPopulationSize > 0 ? (sumFitnessThisGen_ / generationPopulationSize) : 0.0;
        auto now = std::chrono::steady_clock::now();
        finalTrainingSeconds_ = activeTrainingSecondsGet(now);
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

    // Log wing usage diagnostics for this generation.
    {
        int flapCount = 0;
        double maxWingUp = 0.0;
        double bestFlapperFitness = 0.0;
        double bestNonFlapperFitness = 0.0;
        const int popSize = static_cast<int>(wingUpSecondsPerIndividual.size());
        for (int i = 0; i < popSize; ++i) {
            const double wingUp = wingUpSecondsPerIndividual[i];
            const double fitness = fitnessScores[i];
            if (wingUp > 0.5) {
                ++flapCount;
                maxWingUp = std::max(maxWingUp, wingUp);
                bestFlapperFitness = std::max(bestFlapperFitness, fitness);
            }
            else {
                bestNonFlapperFitness = std::max(bestNonFlapperFitness, fitness);
            }
        }
        LOG_INFO(
            State,
            "Evolution: Gen {} wing diagnostics: flappers={}/{}, maxWingUp={:.1f}s, "
            "bestFlapFit={:.4f}, bestNoFlapFit={:.4f}",
            generation,
            flapCount,
            popSize,
            maxWingUp,
            bestFlapperFitness,
            bestNonFlapperFitness);
    }

    DIRTSIM_ASSERT(!robustnessPass_.active, "Evolution: robust pass must complete before advance");
    pendingBest_.resetTrigger();

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

    const EffectiveAdaptiveMutation effectiveAdaptiveMutation = adaptiveMutationResolve(
        mutationConfig,
        trainingPhaseTracker_.status(),
        lastEffectiveAdaptiveMutation_,
        evolutionConfig,
        mutationControlMode_);
    const MutationConfig& effectiveMutationConfig = effectiveAdaptiveMutation.mutationConfig;

    LOG_INFO(
        State,
        "Evolution: breeding config gen={} mode={} perturbations={} resets={} sigma={:.4f}",
        generation,
        adaptiveMutationModeName(effectiveAdaptiveMutation.mode),
        effectiveMutationConfig.perturbationsPerOffspring,
        effectiveMutationConfig.resetsPerOffspring,
        effectiveMutationConfig.sigma);

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
            const std::string variant = parent.brainVariant.value_or("");
            const BrainRegistryEntry* entry =
                brainRegistry_.find(trainingSpec.organismType, parent.brainKind, variant);
            DIRTSIM_ASSERT(entry != nullptr, "Evolution: brain kind not registered for mutation");
            DIRTSIM_ASSERT(entry->getGenomeLayout, "Evolution: brain has no genome layout");
            const GenomeLayout layout = entry->getGenomeLayout();
            Genome mutatedGenome =
                mutate(parent.genome.value(), effectiveMutationConfig, layout, rng, &stats);
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

    lastEffectiveAdaptiveMutation_ = effectiveAdaptiveMutation;
    lastGenTelemetry_.breedingUsesBudget = true;
    lastGenTelemetry_.breedingMutationMode = effectiveAdaptiveMutation.mode;
    lastGenTelemetry_.breedingResolvedPerturbationsPerOffspring =
        effectiveMutationConfig.perturbationsPerOffspring;
    lastGenTelemetry_.breedingResolvedResetsPerOffspring =
        effectiveMutationConfig.resetsPerOffspring;
    lastGenTelemetry_.breedingResolvedSigma = effectiveMutationConfig.sigma;
    lastGenTelemetry_.breedingPerturbationsAvg = survivorPopulationSize > 0
        ? static_cast<double>(perturbationsTotal) / static_cast<double>(survivorPopulationSize)
        : 0.0;
    lastGenTelemetry_.breedingResetsAvg = survivorPopulationSize > 0
        ? static_cast<double>(resetsTotal) / static_cast<double>(survivorPopulationSize)
        : 0.0;
    lastGenTelemetry_.breedingWeightChangesAvg = survivorPopulationSize > 0
        ? static_cast<double>(perturbationsTotal + resetsTotal)
            / static_cast<double>(survivorPopulationSize)
        : 0.0;
    lastGenTelemetry_.breedingWeightChangesMin =
        weightChangesMin == std::numeric_limits<int>::max() ? 0 : weightChangesMin;
    lastGenTelemetry_.breedingWeightChangesMax = weightChangesMax;

    LOG_INFO(
        State,
        "Evolution: offspring cycle gen={} mode={} total={} mutated={} clones={} "
        "(no_genome={} mutation_disabled={} no_delta={})",
        generation,
        adaptiveMutationModeName(effectiveAdaptiveMutation.mode),
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
        wingUpSecondsPerIndividual.assign(population.size(), 0.0);
    }

    adjustConcurrency();
}

void Evolution::captureLastGenerationFitnessDistribution()
{
    const int evaluatedCount = std::min(currentEval, static_cast<int>(fitnessScores.size()));
    if (evaluatedCount <= 0) {
        lastCompletedGeneration_ = -1;
        lastGenerationAverageFitness_ = 0.0;
        lastGenerationFitnessMin_ = 0.0;
        lastGenerationFitnessMax_ = 0.0;
        lastGenerationFitnessHistogram_.clear();
        return;
    }

    double minFitness = fitnessScores[0];
    double maxFitness = fitnessScores[0];
    double sumFitness = fitnessScores[0];
    for (int i = 1; i < evaluatedCount; ++i) {
        minFitness = std::min(minFitness, fitnessScores[i]);
        maxFitness = std::max(maxFitness, fitnessScores[i]);
        sumFitness += fitnessScores[i];
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
    lastGenerationAverageFitness_ = sumFitness / static_cast<double>(evaluatedCount);
    lastGenerationFitnessMin_ = minFitness;
    lastGenerationFitnessMax_ = maxFitness;
    lastGenerationFitnessHistogram_ = std::move(bins);
}

void Evolution::captureLastGenerationTelemetry()
{
    lastGenTelemetry_.eliteCarryoverCount = generationTelemetry_.eliteCarryoverCount;
    lastGenTelemetry_.seedCount = generationTelemetry_.seedCount;
    lastGenTelemetry_.offspringCloneCount = generationTelemetry_.offspringCloneCount;
    lastGenTelemetry_.offspringMutatedCount = generationTelemetry_.offspringMutatedCount;

    lastGenTelemetry_.offspringCloneBeatsParentCount =
        generationTelemetry_.offspringCloneBeatsParentCount;
    lastGenTelemetry_.offspringCloneAvgDeltaFitness =
        generationTelemetry_.offspringCloneComparedCount > 0
        ? generationTelemetry_.offspringCloneDeltaFitnessSum
            / static_cast<double>(generationTelemetry_.offspringCloneComparedCount)
        : 0.0;

    lastGenTelemetry_.offspringMutatedBeatsParentCount =
        generationTelemetry_.offspringMutatedBeatsParentCount;
    lastGenTelemetry_.offspringMutatedAvgDeltaFitness =
        generationTelemetry_.offspringMutatedComparedCount > 0
        ? generationTelemetry_.offspringMutatedDeltaFitnessSum
            / static_cast<double>(generationTelemetry_.offspringMutatedComparedCount)
        : 0.0;

    lastGenTelemetry_.phenotypeUniqueCount =
        static_cast<int>(generationTelemetry_.phenotypeAll.size());
    lastGenTelemetry_.phenotypeUniqueEliteCarryoverCount =
        static_cast<int>(generationTelemetry_.phenotypeEliteCarryover.size());
    lastGenTelemetry_.phenotypeUniqueOffspringMutatedCount =
        static_cast<int>(generationTelemetry_.phenotypeOffspringMutated.size());

    int novelOffspringMutated = 0;
    for (const uint64_t hash : generationTelemetry_.phenotypeOffspringMutated) {
        if (generationTelemetry_.phenotypeEliteCarryover.find(hash)
            == generationTelemetry_.phenotypeEliteCarryover.end()) {
            novelOffspringMutated++;
        }
    }
    lastGenTelemetry_.phenotypeNovelOffspringMutatedCount = novelOffspringMutated;
}

void Evolution::updateTrainingPhaseTelemetry()
{
    const TrainingPhaseUpdate update = trainingPhaseTracker_.updateCompletedGeneration(
        generation, bestFitnessThisGen, evolutionConfig);
    if (!update.phaseChanged) {
        return;
    }

    const TrainingPhaseStatus& status = trainingPhaseTracker_.status();
    LOG_INFO(
        State,
        "Evolution: training phase {} -> {} at generation {} "
        "(best={:.4f}, since_improvement={}, stagnation_level={}, recovery_level={})",
        trainingPhaseName(update.previousPhase),
        trainingPhaseName(update.phase),
        generation,
        bestFitnessThisGen,
        status.generationsSinceImprovement,
        status.stagnationLevel,
        status.recoveryLevel);
}

void Evolution::adjustConcurrency()
{
    if (evolutionConfig.targetCpuPercent <= 0 || !executor_ || cpuSamples_.empty()) {
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

    const int current = executor_->allowedConcurrencyGet();
    int adjusted = current;

    if (avgCpu > target + kTolerance && current > 1) {
        adjusted = current - 1;
    }
    else if (avgCpu < target - kTolerance && current < executor_->backgroundWorkerCountGet()) {
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
        executor_->allowedConcurrencySet(adjusted);
    }
}

double Evolution::activeTrainingSecondsGet(std::chrono::steady_clock::time_point now) const
{
    auto pausedDuration = totalPausedDuration_;
    if (trainingPaused_ && trainingPauseStartTime_ != std::chrono::steady_clock::time_point{}) {
        pausedDuration += now - trainingPauseStartTime_;
    }

    const auto activeDuration = now - trainingStartTime_ - pausedDuration;
    return std::max(0.0, std::chrono::duration<double>(activeDuration).count());
}

void Evolution::pauseSet(bool paused, StateMachine& dsm)
{
    const auto now = std::chrono::steady_clock::now();
    if (paused == trainingPaused_) {
        lastProgressBroadcastTime_ = std::chrono::steady_clock::time_point{};
        broadcastProgress(dsm);
        return;
    }

    if (paused) {
        trainingPaused_ = true;
        trainingPauseStartTime_ = now;
        if (executor_) {
            executor_->pauseSet(true);
        }
        LOG_INFO(State, "Evolution: Paused training");
    }
    else {
        const auto pausedDuration =
            trainingPauseStartTime_ == std::chrono::steady_clock::time_point{}
            ? std::chrono::steady_clock::duration::zero()
            : now - trainingPauseStartTime_;
        totalPausedDuration_ += pausedDuration;
        trainingPauseStartTime_ = std::chrono::steady_clock::time_point{};
        trainingPaused_ = false;
        if (executor_) {
            executor_->pauseSet(false);
        }
        if (lastBestPlaybackBroadcastTime_ != std::chrono::steady_clock::time_point{}) {
            lastBestPlaybackBroadcastTime_ += pausedDuration;
        }
        if (lastBestPlaybackStepTime_ != std::chrono::steady_clock::time_point{}) {
            lastBestPlaybackStepTime_ += pausedDuration;
        }
        if (lastCpuSampleTime_ != std::chrono::steady_clock::time_point{}) {
            lastCpuSampleTime_ += pausedDuration;
        }
        LOG_INFO(
            State,
            "Evolution: Resumed training after {:.3f}s pause",
            std::chrono::duration<double>(pausedDuration).count());
    }

    lastProgressBroadcastTime_ = std::chrono::steady_clock::time_point{};
    broadcastProgress(dsm);
}

void Evolution::setBestPlaybackSource(const Individual& individual, double fitness, int generation)
{
    bestPlayback_.individual = individual;
    bestPlayback_.individual->parentFitness.reset();
    bestPlayback_.nesTileTokenizer.reset();
    bestPlayback_.fitness = fitness;
    bestPlayback_.generation = generation;
    bestPlayback_.duckNextPrimarySpawnLeftFirst = true;
    lastBestPlaybackBroadcastTime_ = std::chrono::steady_clock::time_point{};
    bestPlayback_.clearRunner();
}

void Evolution::stepBestPlayback(StateMachine& dsm)
{
    const auto& uiTraining = dsm.getUserSettings().uiTraining;
    if (!uiTraining.bestPlaybackEnabled) {
        bestPlayback_.clearRunner();
        return;
    }

    if (!bestPlayback_.individual.has_value()) {
        return;
    }

    const auto startRunner = [&](std::optional<bool> spawnSideOverride) {
        if (requiresNesTileTokenizer(bestPlayback_.individual.value())
            && !bestPlayback_.nesTileTokenizer) {
            if (nesTileTokenizer_) {
                bestPlayback_.nesTileTokenizer = nesTileTokenizer_;
            }
            else {
                auto tokenizerResult = NesTileTokenizerBootstrapper::build(
                    bestPlayback_.individual->scenarioId, scenarioConfigOverride_);
                if (tokenizerResult.isError()) {
                    LOG_WARN(
                        State,
                        "Evolution: Failed to bootstrap NES tile tokenizer for best playback: {}",
                        tokenizerResult.errorValue());
                    return;
                }
                bestPlayback_.nesTileTokenizer = std::move(tokenizerResult).value();
            }
        }

        const TrainingRunner::Config runnerConfig{
            .brainRegistry = brainRegistry_,
            .nesTileTokenizer = bestPlayback_.nesTileTokenizer,
            .duckClockSpawnLeftFirst = spawnSideOverride,
            .duckClockSpawnRngSeed = std::nullopt,
            .scenarioConfigOverride = scenarioConfigOverride_,
        };
        bestPlayback_.runner = std::make_unique<TrainingRunner>(
            trainingSpec,
            makeRunnerIndividual(bestPlayback_.individual.value()),
            evolutionConfig,
            dsm.getGenomeRepository(),
            runnerConfig);
    };

    const bool duckClockScenario =
        isDuckClockScenario(trainingSpec.organismType, bestPlayback_.individual->scenarioId);
    if (!bestPlayback_.runner) {
        const std::optional<bool> primarySpawnSide = duckClockScenario
            ? std::optional<bool>(bestPlayback_.duckNextPrimarySpawnLeftFirst)
            : std::nullopt;
        bestPlayback_.duckPrimarySpawnLeftFirst = primarySpawnSide.value_or(true);
        bestPlayback_.duckSecondPassActive = false;
        startRunner(primarySpawnSide);
        if (!bestPlayback_.runner) {
            return;
        }
    }

    // Advance the sim at real-time pace: one step per TIMESTEP of wall-clock time.
    const auto now = std::chrono::steady_clock::now();
    const auto stepInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(TrainingRunner::TIMESTEP));
    TrainingRunner::Status status{ .state = TrainingRunner::State::Running };
    if (lastBestPlaybackStepTime_ == std::chrono::steady_clock::time_point{}
        || now - lastBestPlaybackStepTime_ >= stepInterval) {
        lastBestPlaybackStepTime_ = now;
        status = bestPlayback_.runner->step(1);
    }

    // Broadcast frames at the configured interval, independent of sim step rate.
    const auto interval = std::chrono::milliseconds(uiTraining.bestPlaybackIntervalMs);
    if (lastBestPlaybackBroadcastTime_ == std::chrono::steady_clock::time_point{}
        || now - lastBestPlaybackBroadcastTime_ >= interval) {
        lastBestPlaybackBroadcastTime_ = now;
        const WorldData* worldData = bestPlayback_.runner->getWorldData();
        const std::vector<OrganismId>* organismGrid = bestPlayback_.runner->getOrganismGrid();
        DIRTSIM_ASSERT(worldData != nullptr, "Evolution: Best playback runner missing WorldData");
        DIRTSIM_ASSERT(
            organismGrid != nullptr, "Evolution: Best playback runner missing organism grid");

        std::optional<ScenarioVideoFrame> videoFrame =
            bestPlayback_.runner->getScenarioVideoFrame();
        if (bestPlayback_.runner->isNesScenario()
            && uiTraining.nesTileDebugView != NesTileDebugView::NormalVideo) {
            auto debugFrameResult = bestPlayback_.runner->makeNesTileDebugScenarioVideoFrame(
                uiTraining.nesTileDebugView);
            if (debugFrameResult.isValue()) {
                videoFrame = std::move(debugFrameResult).value();
            }
            else {
                LOG_WARN(
                    State,
                    "Evolution: Failed to render NES tile debug best playback frame: {}",
                    debugFrameResult.errorValue());
            }
        }
        if (!bestPlayback_.runner->isNesScenario() || videoFrame.has_value()) {
            broadcastTrainingBestPlaybackFrame(
                dsm,
                *worldData,
                *organismGrid,
                bestPlayback_.fitness,
                bestPlayback_.generation,
                bestPlayback_.runner->getNesLastControllerTelemetry(),
                videoFrame);
        }
    }

    if (status.state == TrainingRunner::State::Running) {
        return;
    }

    if (duckClockScenario && !bestPlayback_.duckSecondPassActive) {
        bestPlayback_.duckSecondPassActive = true;
        startRunner(std::optional<bool>(!bestPlayback_.duckPrimarySpawnLeftFirst));
        return;
    }

    if (duckClockScenario) {
        bestPlayback_.duckNextPrimarySpawnLeftFirst = !bestPlayback_.duckNextPrimarySpawnLeftFirst;
    }
    bestPlayback_.clearRunner();
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
    double totalSeconds = activeTrainingSecondsGet(now);

    const double visibleSimTime = executor_ ? executor_->visibleSimTimeGet() : 0.0;
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
    const bool validatingBest = robustnessPass_.active;
    const int validatingBestCompletedSamples =
        validatingBest ? robustnessPass_.completedSamples : 0;
    const int validatingBestTargetSamples = validatingBest ? robustnessPass_.targetEvalCount : 0;
    const TrainingPhaseStatus& trainingPhaseStatus = trainingPhaseTracker_.status();

    // Compute CPU auto-tune fields.
    int activeParallelism = evolutionConfig.maxParallelEvaluations;
    double latestCpu = lastCpuPercent_;
    if (executor_) {
        activeParallelism = executor_->allowedConcurrencyGet();
    }

    const auto archiveBrainKinds = managedArchiveBrainKindsGet(trainingSpec);
    size_t managedGenomeCount = 0;
    for (const auto& brainKind : archiveBrainKinds) {
        managedGenomeCount +=
            repo.countManagedByBucket(trainingSpec.organismType, brainKind, genomePoolId_);
    }
    const int totalGenomeCountForProgress = clampArchiveMetric(managedGenomeCount);
    size_t totalArchiveCapacity = 0;
    if (evolutionConfig.genomeArchiveMaxSize > 0) {
        const size_t archiveMaxSize = static_cast<size_t>(evolutionConfig.genomeArchiveMaxSize);
        totalArchiveCapacity =
            archiveBrainKinds.size() > std::numeric_limits<size_t>::max() / archiveMaxSize
            ? std::numeric_limits<size_t>::max()
            : archiveBrainKinds.size() * archiveMaxSize;
    }
    const int genomeArchiveMaxSizeForProgress = clampArchiveMetric(totalArchiveCapacity);

    const Api::EvolutionProgress progress{
        .generation = generation,
        .maxGenerations = evolutionConfig.maxGenerations,
        .currentEval = currentEval,
        .populationSize = static_cast<int>(population.size()),
        .totalGenomeCount = totalGenomeCountForProgress,
        .genomeArchiveMaxSize = genomeArchiveMaxSizeForProgress,
        .bestFitnessThisGen = bestFitnessThisGen,
        .bestFitnessAllTime = bestAllTime,
        .robustEvaluationCount = robustEvaluationCount_,
        .validatingBest = validatingBest,
        .validatingBestCompletedSamples = validatingBestCompletedSamples,
        .validatingBestTargetSamples = validatingBestTargetSamples,
        .trainingPhase = trainingPhaseStatus.phase,
        .generationsSinceImprovement = trainingPhaseStatus.generationsSinceImprovement,
        .lastImprovementGeneration = trainingPhaseStatus.lastImprovementGeneration,
        .stagnationLevel = trainingPhaseStatus.stagnationLevel,
        .recoveryLevel = trainingPhaseStatus.recoveryLevel,
        .averageFitness = avgFitness,
        .lastCompletedGeneration = lastCompletedGeneration_,
        .lastGenerationAverageFitness = lastGenerationAverageFitness_,
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
        .isPaused = trainingPaused_,
        .activeParallelism = activeParallelism,
        .cpuPercent = latestCpu,
        .cpuPercentPerCore = lastCpuPercentPerCore_,
        .lastBreeding =
            Api::EvolutionBreedingTelemetry{
                .usesBudget = lastGenTelemetry_.breedingUsesBudget,
                .mutationMode = lastGenTelemetry_.breedingMutationMode,
                .resolvedPerturbationsPerOffspring =
                    lastGenTelemetry_.breedingResolvedPerturbationsPerOffspring,
                .resolvedResetsPerOffspring = lastGenTelemetry_.breedingResolvedResetsPerOffspring,
                .resolvedSigma = lastGenTelemetry_.breedingResolvedSigma,
                .perturbationsAvg = lastGenTelemetry_.breedingPerturbationsAvg,
                .resetsAvg = lastGenTelemetry_.breedingResetsAvg,
                .weightChangesAvg = lastGenTelemetry_.breedingWeightChangesAvg,
                .weightChangesMin = lastGenTelemetry_.breedingWeightChangesMin,
                .weightChangesMax = lastGenTelemetry_.breedingWeightChangesMax,
            },
        .lastGenerationEliteCarryoverCount = lastGenTelemetry_.eliteCarryoverCount,
        .lastGenerationSeedCount = lastGenTelemetry_.seedCount,
        .lastGenerationOffspringCloneCount = lastGenTelemetry_.offspringCloneCount,
        .lastGenerationOffspringMutatedCount = lastGenTelemetry_.offspringMutatedCount,
        .lastGenerationOffspringCloneBeatsParentCount =
            lastGenTelemetry_.offspringCloneBeatsParentCount,
        .lastGenerationOffspringCloneAvgDeltaFitness =
            lastGenTelemetry_.offspringCloneAvgDeltaFitness,
        .lastGenerationOffspringMutatedBeatsParentCount =
            lastGenTelemetry_.offspringMutatedBeatsParentCount,
        .lastGenerationOffspringMutatedAvgDeltaFitness =
            lastGenTelemetry_.offspringMutatedAvgDeltaFitness,
        .lastGenerationPhenotypeUniqueCount = lastGenTelemetry_.phenotypeUniqueCount,
        .lastGenerationPhenotypeUniqueEliteCarryoverCount =
            lastGenTelemetry_.phenotypeUniqueEliteCarryoverCount,
        .lastGenerationPhenotypeUniqueOffspringMutatedCount =
            lastGenTelemetry_.phenotypeUniqueOffspringMutatedCount,
        .lastGenerationPhenotypeNovelOffspringMutatedCount =
            lastGenTelemetry_.phenotypeNovelOffspringMutatedCount,
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
        .genomePoolId = genomePoolId_,
        .nesTileBrainCompatibility = brainCompatibilityMetadataForIndividual(
            population[bestIdx], nesTileBrainCompatibility_),
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
            .genomePoolId = genomePoolId_,
            .nesTileBrainCompatibility =
                brainCompatibilityMetadataForIndividual(population[i], nesTileBrainCompatibility_),
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
