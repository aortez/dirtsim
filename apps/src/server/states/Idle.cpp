#include "State.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/Timers.h"
#include "core/World.h"
#include "core/organisms/evolution/GenomeMetadataUtils.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/TrainingBrainRegistry.h"
#include "core/organisms/evolution/TrainingSpec.h"
#include "core/scenarios/ClockScenario.h"
#include "core/scenarios/NesFlappyParatroopaScenario.h"
#include "core/scenarios/ScenarioRegistry.h"
#include "server/ServerConfig.h"
#include "server/StateMachine.h"
#include "server/api/ApiError.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

namespace DirtSim {
namespace Server {
namespace State {

namespace {

ScenarioConfig buildScenarioConfigForRun(StateMachine& dsm, Scenario::EnumType scenarioId)
{
    ScenarioConfig scenarioConfig = makeDefaultConfig(scenarioId);
    if (dsm.serverConfig && getScenarioId(dsm.serverConfig->startupConfig) == scenarioId) {
        scenarioConfig = dsm.serverConfig->startupConfig;
    }

    if (auto* clockConfig = std::get_if<Config::Clock>(&scenarioConfig)) {
        clockConfig->timezoneIndex = static_cast<uint8_t>(std::clamp(
            dsm.getUserSettings().timezoneIndex,
            0,
            static_cast<int>(ClockScenario::TIMEZONES.size()) - 1));
    }

    return scenarioConfig;
}

bool isWarmGenomeCompatibleForPopulation(
    const GenomeMetadata& metadata, OrganismType organismType, const PopulationSpec& populationSpec)
{
    if (!metadata.organismType.has_value() || metadata.organismType.value() != organismType) {
        return false;
    }

    if (metadata.scenarioId != populationSpec.scenarioId) {
        return false;
    }

    if (!metadata.brainKind.has_value() || metadata.brainKind.value() != populationSpec.brainKind) {
        return false;
    }

    if (metadata.brainVariant.value_or("") != populationSpec.brainVariant.value_or("")) {
        return false;
    }

    return true;
}

struct WarmSeedCandidate {
    GenomeId id = INVALID_GENOME_ID;
    GenomeMetadata metadata;
    double robustFitness = 0.0;
    int robustEvalCount = 0;
};

constexpr double kWarmStartSamplingFitnessBaseWeight = 0.1;
constexpr double kWarmStartSamplingEpsilon = 1e-9;
constexpr int kWarmStartSamplingCandidateLimit = 64;

std::vector<WarmSeedCandidate> collectWarmSeedCandidates(
    const GenomeRepository& repo, int minRobustEvalCount)
{
    std::vector<WarmSeedCandidate> candidates;
    const auto entries = repo.list();
    candidates.reserve(entries.size());

    for (const auto& [id, metadata] : entries) {
        const int robustEvalCount = effectiveRobustEvalCount(metadata);
        if (robustEvalCount < minRobustEvalCount) {
            continue;
        }
        candidates.push_back(
            WarmSeedCandidate{
                .id = id,
                .metadata = metadata,
                .robustFitness = effectiveRobustFitness(metadata),
                .robustEvalCount = robustEvalCount,
            });
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const WarmSeedCandidate& left, const WarmSeedCandidate& right) {
            if (left.robustFitness != right.robustFitness) {
                return left.robustFitness > right.robustFitness;
            }
            if (left.robustEvalCount != right.robustEvalCount) {
                return left.robustEvalCount > right.robustEvalCount;
            }
            if (left.metadata.createdTimestamp != right.metadata.createdTimestamp) {
                return left.metadata.createdTimestamp > right.metadata.createdTimestamp;
            }
            return left.id.toString() < right.id.toString();
        });

    return candidates;
}

const Genome* loadWarmGenome(
    GenomeRepository& repo, GenomeId id, std::unordered_map<GenomeId, std::optional<Genome>>& cache)
{
    auto it = cache.find(id);
    if (it == cache.end()) {
        it = cache.emplace(id, repo.get(id)).first;
    }
    if (!it->second.has_value()) {
        return nullptr;
    }
    return &it->second.value();
}

bool canComputeGenomeWeightDistance(const Genome& left, const Genome& right)
{
    return !left.weights.empty() && left.weights.size() == right.weights.size();
}

double computeGenomeWeightDistance(const Genome& left, const Genome& right)
{
    DIRTSIM_ASSERT(
        canComputeGenomeWeightDistance(left, right),
        "Idle: comparable genomes required for warm-start distance calculation");

    double squaredDistance = 0.0;
    for (size_t i = 0; i < left.weights.size(); ++i) {
        const double delta =
            static_cast<double>(left.weights[i]) - static_cast<double>(right.weights[i]);
        squaredDistance += delta * delta;
    }
    return std::sqrt(squaredDistance / static_cast<double>(left.weights.size()));
}

int findNextWarmSeedByFitness(
    const std::vector<const WarmSeedCandidate*>& compatibleCandidates,
    const std::vector<bool>& selectedMask)
{
    for (int i = 0; i < static_cast<int>(compatibleCandidates.size()); ++i) {
        if (!selectedMask[i]) {
            return i;
        }
    }
    return -1;
}

int computeWarmStartSeedTargetCount(
    int randomCount, double warmStartSeedPercent, int warmStartSeedCount)
{
    if (randomCount <= 0) {
        return 0;
    }

    const double clampedWarmStartSeedPercent = std::clamp(warmStartSeedPercent, 0.0, 100.0);
    if (clampedWarmStartSeedPercent > 0.0) {
        const int percentTarget = static_cast<int>(
            std::ceil((static_cast<double>(randomCount) * clampedWarmStartSeedPercent) / 100.0));
        return std::clamp(percentTarget, 1, randomCount);
    }

    return std::min(randomCount, std::max(0, warmStartSeedCount));
}

double computeWarmSeedFitnessFloor(
    const std::vector<const WarmSeedCandidate*>& compatibleCandidates, double floorPercentile)
{
    if (compatibleCandidates.empty()) {
        return -std::numeric_limits<double>::infinity();
    }

    std::vector<double> remainingFitnessValues;
    remainingFitnessValues.reserve(compatibleCandidates.size());
    for (const auto* candidate : compatibleCandidates) {
        remainingFitnessValues.push_back(candidate->robustFitness);
    }

    std::sort(remainingFitnessValues.begin(), remainingFitnessValues.end());
    const double clampedFloorPercentile = std::clamp(floorPercentile, 0.0, 100.0);
    const size_t floorIndex = static_cast<size_t>(std::floor(
        (clampedFloorPercentile / 100.0) * static_cast<double>(remainingFitnessValues.size() - 1)));
    return remainingFitnessValues[floorIndex];
}

int sampleWarmSeedByFitnessAndNovelty(
    const std::vector<const WarmSeedCandidate*>& compatibleCandidates,
    const std::vector<bool>& selectedMask,
    const std::vector<GenomeId>& referenceSeedIds,
    double noveltyWeight,
    double fitnessFloorPercentile,
    GenomeRepository& repo,
    std::unordered_map<GenomeId, std::optional<Genome>>& genomeCache,
    std::mt19937& rng)
{
    const double clampedNoveltyWeight = std::clamp(noveltyWeight, 0.0, 1.0);
    const double fitnessWeight = 1.0 - clampedNoveltyWeight;
    const double fitnessFloor =
        computeWarmSeedFitnessFloor(compatibleCandidates, fitnessFloorPercentile);

    std::vector<int> eligible;
    eligible.reserve(compatibleCandidates.size());
    for (int i = 0; i < static_cast<int>(compatibleCandidates.size()); ++i) {
        if (selectedMask[i]) {
            continue;
        }
        if (compatibleCandidates[i]->robustFitness < fitnessFloor) {
            continue;
        }
        eligible.push_back(i);
        if (static_cast<int>(eligible.size()) >= kWarmStartSamplingCandidateLimit) {
            break;
        }
    }
    if (eligible.empty()) {
        return -1;
    }
    if (eligible.size() == 1) {
        return eligible.front();
    }

    double minEligibleFitness = std::numeric_limits<double>::infinity();
    double maxEligibleFitness = -std::numeric_limits<double>::infinity();
    for (const int index : eligible) {
        const double fitness = compatibleCandidates[index]->robustFitness;
        minEligibleFitness = std::min(minEligibleFitness, fitness);
        maxEligibleFitness = std::max(maxEligibleFitness, fitness);
    }
    const double fitnessRange =
        std::max(maxEligibleFitness - minEligibleFitness, kWarmStartSamplingEpsilon);

    std::vector<double> minDistances(eligible.size(), 0.0);
    double maxMinDistance = 0.0;
    if (!referenceSeedIds.empty()) {
        for (size_t i = 0; i < eligible.size(); ++i) {
            const auto* candidate = compatibleCandidates[eligible[i]];
            const Genome* candidateGenome = loadWarmGenome(repo, candidate->id, genomeCache);
            if (candidateGenome == nullptr) {
                continue;
            }

            bool hasComparableReference = false;
            double minDistance = std::numeric_limits<double>::infinity();
            for (const GenomeId& id : referenceSeedIds) {
                const Genome* referenceGenome = loadWarmGenome(repo, id, genomeCache);
                if (referenceGenome == nullptr) {
                    continue;
                }
                if (!canComputeGenomeWeightDistance(*candidateGenome, *referenceGenome)) {
                    continue;
                }
                hasComparableReference = true;
                minDistance = std::min(
                    minDistance, computeGenomeWeightDistance(*candidateGenome, *referenceGenome));
            }
            if (!hasComparableReference) {
                continue;
            }
            minDistances[i] = minDistance;
            maxMinDistance = std::max(maxMinDistance, minDistance);
        }
    }

    std::vector<double> weights;
    weights.reserve(eligible.size());
    for (size_t i = 0; i < eligible.size(); ++i) {
        const auto* candidate = compatibleCandidates[eligible[i]];
        double fitnessNorm = (candidate->robustFitness - minEligibleFitness) / fitnessRange;
        if (!std::isfinite(fitnessNorm)) {
            fitnessNorm = 0.0;
        }
        fitnessNorm = std::clamp(fitnessNorm, 0.0, 1.0);

        double noveltyNorm = 0.0;
        if (maxMinDistance > kWarmStartSamplingEpsilon) {
            noveltyNorm = minDistances[i] / maxMinDistance;
        }
        noveltyNorm = std::clamp(noveltyNorm, 0.0, 1.0);

        const double blendedFitness = kWarmStartSamplingFitnessBaseWeight
            + ((1.0 - kWarmStartSamplingFitnessBaseWeight) * fitnessNorm);
        double weight = (fitnessWeight * blendedFitness) + (clampedNoveltyWeight * noveltyNorm);
        if (!std::isfinite(weight) || weight <= 0.0) {
            weight = kWarmStartSamplingEpsilon;
        }
        weights.push_back(weight);
    }

    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    const int chosenEligible = dist(rng);
    if (chosenEligible < 0 || chosenEligible >= static_cast<int>(eligible.size())) {
        return -1;
    }
    return eligible[chosenEligible];
}

std::optional<ApiError> validateTrainingConfig(
    const Api::EvolutionStart::Command& command,
    ScenarioRegistry& registry,
    GenomeRepository& repo,
    TrainingSpec& outSpec,
    int& outPopulationSize,
    int& outWarmSeedInjectedCount)
{
    outWarmSeedInjectedCount = 0;
    outSpec.scenarioId = command.scenarioId;
    outSpec.organismType = command.organismType;
    outSpec.population = command.population;

    if (outSpec.population.empty()) {
        if (command.evolution.populationSize <= 0) {
            return ApiError("populationSize must be > 0 when population is empty");
        }

        PopulationSpec defaultSpec;
        defaultSpec.scenarioId = outSpec.scenarioId;
        defaultSpec.count = command.evolution.populationSize;

        switch (outSpec.organismType) {
            case OrganismType::TREE:
                defaultSpec.brainKind = TrainingBrainKind::NeuralNet;
                defaultSpec.randomCount = defaultSpec.count;
                break;
            case OrganismType::DUCK:
                defaultSpec.brainKind = TrainingBrainKind::DuckNeuralNetRecurrent;
                defaultSpec.randomCount = defaultSpec.count;
                break;
            case OrganismType::NES_FLAPPY_BIRD:
                defaultSpec.scenarioId = Scenario::EnumType::NesFlappyParatroopa;
                defaultSpec.brainKind = TrainingBrainKind::NesFlappyBird;
                defaultSpec.randomCount = defaultSpec.count;
                break;
            case OrganismType::GOOSE:
                defaultSpec.brainKind = TrainingBrainKind::Random;
                break;
            default:
                return ApiError("Unsupported organismType for training");
        }

        outSpec.population.push_back(defaultSpec);
    }

    TrainingBrainRegistry brainRegistry = TrainingBrainRegistry::createDefault();
    std::vector<WarmSeedCandidate> warmSeedCandidates;
    const bool warmStartAlwaysIncludeBest = command.evolution.warmStartAlwaysIncludeBest;
    const double warmStartFitnessFloorPercentile =
        std::clamp(command.evolution.warmStartFitnessFloorPercentile, 0.0, 100.0);
    const double warmStartNoveltyWeight =
        std::clamp(command.evolution.warmStartNoveltyWeight, 0.0, 1.0);
    const int warmStartSeedCount = std::max(0, command.evolution.warmStartSeedCount);
    const double warmStartSeedPercent =
        std::clamp(command.evolution.warmStartSeedPercent, 0.0, 100.0);
    const int warmStartMinRobustEvalCount =
        std::max(1, command.evolution.warmStartMinRobustEvalCount);
    std::mt19937 warmSeedSamplingRng(std::random_device{}());
    if (command.resumePolicy == TrainingResumePolicy::WarmFromBest) {
        warmSeedCandidates = collectWarmSeedCandidates(repo, warmStartMinRobustEvalCount);
    }

    outPopulationSize = 0;
    for (auto& spec : outSpec.population) {
        const ScenarioMetadata* metadata = registry.getMetadata(spec.scenarioId);
        if (!metadata) {
            return ApiError(
                std::string("Scenario not found: ") + std::string(toString(spec.scenarioId)));
        }
        if (spec.count <= 0) {
            return ApiError("Population entry count must be > 0");
        }
        if (spec.brainKind.empty()) {
            return ApiError("Population entry brainKind must not be empty");
        }

        const std::string variant = spec.brainVariant.value_or("");
        const BrainRegistryEntry* entry =
            brainRegistry.find(outSpec.organismType, spec.brainKind, variant);
        if (!entry) {
            std::string message = "Brain kind not registered: " + spec.brainKind;
            if (!variant.empty()) {
                message += " (" + variant + ")";
            }
            return ApiError(message);
        }

        if (entry->requiresGenome) {
            if (!warmSeedCandidates.empty() && spec.randomCount > 0 && spec.count > 0) {
                const int maxSeedsToInject = computeWarmStartSeedTargetCount(
                    spec.randomCount, warmStartSeedPercent, warmStartSeedCount);
                if (maxSeedsToInject > 0) {
                    std::vector<const WarmSeedCandidate*> compatibleCandidates;
                    compatibleCandidates.reserve(warmSeedCandidates.size());
                    for (const auto& candidate : warmSeedCandidates) {
                        if (!isWarmGenomeCompatibleForPopulation(
                                candidate.metadata, outSpec.organismType, spec)) {
                            continue;
                        }
                        if (std::find(
                                spec.seedGenomes.begin(), spec.seedGenomes.end(), candidate.id)
                            != spec.seedGenomes.end()) {
                            continue;
                        }
                        compatibleCandidates.push_back(&candidate);
                    }

                    if (!compatibleCandidates.empty()) {
                        std::vector<bool> selectedMask(compatibleCandidates.size(), false);
                        std::vector<GenomeId> selectedSeedIds = spec.seedGenomes;
                        std::unordered_map<GenomeId, std::optional<Genome>> genomeCache;
                        genomeCache.reserve(compatibleCandidates.size() + selectedSeedIds.size());

                        int injectedForSpec = 0;
                        const auto injectCandidate = [&](int index) {
                            selectedMask[index] = true;
                            const GenomeId id = compatibleCandidates[index]->id;
                            spec.seedGenomes.push_back(id);
                            selectedSeedIds.push_back(id);
                            spec.randomCount -= 1;
                            injectedForSpec += 1;
                            outWarmSeedInjectedCount += 1;
                        };

                        if (warmStartAlwaysIncludeBest) {
                            const int nextIndex =
                                findNextWarmSeedByFitness(compatibleCandidates, selectedMask);
                            if (nextIndex >= 0 && injectedForSpec < maxSeedsToInject) {
                                injectCandidate(nextIndex);
                            }
                        }

                        while (injectedForSpec < maxSeedsToInject) {
                            int sampledIndex = sampleWarmSeedByFitnessAndNovelty(
                                compatibleCandidates,
                                selectedMask,
                                selectedSeedIds,
                                warmStartNoveltyWeight,
                                warmStartFitnessFloorPercentile,
                                repo,
                                genomeCache,
                                warmSeedSamplingRng);
                            if (sampledIndex < 0) {
                                sampledIndex =
                                    findNextWarmSeedByFitness(compatibleCandidates, selectedMask);
                            }
                            if (sampledIndex < 0) {
                                break;
                            }
                            injectCandidate(sampledIndex);
                        }
                    }
                }
            }

            if (spec.randomCount < 0) {
                return ApiError("randomCount must be >= 0");
            }
            const int seedCount = static_cast<int>(spec.seedGenomes.size());
            if (spec.count != seedCount + spec.randomCount) {
                return ApiError("Population count must match seedGenomes + randomCount");
            }

            for (const auto& id : spec.seedGenomes) {
                if (id.isNil()) {
                    return ApiError("Seed genome ID is nil");
                }
                if (!repo.exists(id)) {
                    return ApiError("Seed genome not found: " + id.toShortString());
                }
                if (entry->isGenomeCompatible) {
                    auto genome = repo.get(id);
                    if (!genome.has_value()) {
                        return ApiError("Seed genome not found: " + id.toShortString());
                    }
                    if (!entry->isGenomeCompatible(genome.value())) {
                        return ApiError(
                            "Seed genome incompatible with brain kind: " + spec.brainKind);
                    }
                }
            }
        }
        else {
            if (!spec.seedGenomes.empty()) {
                return ApiError("seedGenomes are not allowed for this brain kind");
            }
            if (spec.randomCount != 0) {
                return ApiError("randomCount must be 0 for this brain kind");
            }
        }

        outPopulationSize += spec.count;
    }

    if (!outSpec.population.empty()) {
        outSpec.scenarioId = outSpec.population.front().scenarioId;
    }

    if (outPopulationSize <= 0) {
        return ApiError("Population size must be > 0");
    }

    return std::nullopt;
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

} // namespace

void Idle::onEnter(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Server ready, waiting for commands (no active World)");
    // Note: World is owned by SimRunning state, not StateMachine.
}

void Idle::onExit(StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Exiting");
}

State::Any Idle::onEvent(const Api::EvolutionStart::Cwc& cwc, StateMachine& dsm)
{
    LOG_INFO(State, "EvolutionStart command received");

    TrainingSpec trainingSpec;
    int populationSize = 0;
    int warmSeedInjectedCount = 0;
    auto error = validateTrainingConfig(
        cwc.command,
        dsm.getScenarioRegistry(),
        dsm.getGenomeRepository(),
        trainingSpec,
        populationSize,
        warmSeedInjectedCount);
    if (error.has_value()) {
        LOG_WARN(State, "EvolutionStart rejected: {}", error->message);
        cwc.sendResponse(Api::EvolutionStart::Response::error(error.value()));
        return Idle{};
    }

    Evolution newState;
    newState.evolutionConfig = cwc.command.evolution;
    newState.mutationConfig = cwc.command.mutation;
    newState.trainingSpec = std::move(trainingSpec);
    newState.evolutionConfig.populationSize = populationSize;
    newState.evolutionConfig.maxParallelEvaluations =
        resolveParallelEvaluations(cwc.command.evolution.maxParallelEvaluations, populationSize);

    LOG_INFO(
        State,
        "Starting evolution: population={}, generations={}, scenario={}, organism_type={}",
        newState.evolutionConfig.populationSize,
        cwc.command.evolution.maxGenerations,
        toString(newState.trainingSpec.scenarioId),
        static_cast<int>(newState.trainingSpec.organismType));
    LOG_INFO(
        State,
        "Evolution: max parallel evaluations = {}",
        newState.evolutionConfig.maxParallelEvaluations);
    if (cwc.command.resumePolicy == TrainingResumePolicy::WarmFromBest) {
        if (warmSeedInjectedCount > 0) {
            LOG_INFO(
                State,
                "Evolution: Warm resume injected {} robust genome seed(s)",
                warmSeedInjectedCount);
        }
        else {
            LOG_INFO(
                State,
                "Evolution: Warm resume found no compatible robust genomes "
                "(min_robust_eval_count={})",
                std::max(1, cwc.command.evolution.warmStartMinRobustEvalCount));
        }
    }

    cwc.sendResponse(Api::EvolutionStart::Response::okay({ .started = true }));
    return newState;
}

State::Any Idle::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Exit command received, shutting down");

    // Send success response.
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));

    // Transition to Shutdown state (Shutdown.onEnter will set shouldExit flag).
    return Shutdown{};
}

State::Any Idle::onEvent(const Api::SimRun::Cwc& cwc, StateMachine& dsm)
{
    assert(dsm.serverConfig && "serverConfig must be loaded");

    // Use scenario_id from command if provided, otherwise fall back to user settings.
    Scenario::EnumType scenarioId = cwc.command.scenario_id.has_value()
        ? cwc.command.scenario_id.value()
        : dsm.getUserSettings().defaultScenario;
    LOG_INFO(State, "SimRun command received, using scenario '{}'", toString(scenarioId));

    // Validate max_frame_ms parameter.
    if (cwc.command.max_frame_ms < 0) {
        LOG_ERROR(State, "Invalid max_frame_ms value: {}", cwc.command.max_frame_ms);
        cwc.sendResponse(
            Api::SimRun::Response::error(
                ApiError("max_frame_ms must be >= 0 (0 = unlimited, >0 = frame rate cap)")));
        return Idle{};
    }

    // Create new SimRunning state with world.
    SimRunning newState;

    // Get scenario metadata first to check for required dimensions.
    auto& registry = dsm.getScenarioRegistry();
    const ScenarioMetadata* metadata = registry.getMetadata(scenarioId);

    if (!metadata) {
        LOG_ERROR(State, "Scenario '{}' not found in registry", toString(scenarioId));
        cwc.sendResponse(
            Api::SimRun::Response::error(
                ApiError(std::string("Scenario not found: ") + std::string(toString(scenarioId)))));
        return Idle{};
    }

    // Determine world dimensions: container-based > scenario requirements > defaults.
    uint32_t worldWidth = dsm.defaultWidth;
    uint32_t worldHeight = dsm.defaultHeight;

    if (cwc.command.container_size.x > 0 && cwc.command.container_size.y > 0) {
        constexpr int targetCellSize = 16;
        worldWidth = static_cast<uint32_t>(cwc.command.container_size.x / targetCellSize);
        worldHeight = static_cast<uint32_t>(cwc.command.container_size.y / targetCellSize);
        worldWidth = std::max(worldWidth, 10u);
        worldHeight = std::max(worldHeight, 10u);
    }
    else if (metadata->requiredWidth > 0 && metadata->requiredHeight > 0) {
        worldWidth = metadata->requiredWidth;
        worldHeight = metadata->requiredHeight;
    }

    // Create world with appropriate dimensions.
    LOG_INFO(
        State,
        "Creating World {}x{} (container: {}x{})",
        worldWidth,
        worldHeight,
        cwc.command.container_size.x,
        cwc.command.container_size.y);
    newState.world = std::make_unique<World>(worldWidth, worldHeight);

    // Create scenario instance from factory.
    newState.scenario = registry.createScenario(scenarioId);
    assert(newState.scenario && "Scenario factory failed after metadata check");

    // Set scenario ID in state.
    newState.scenario_id = scenarioId;

    // Apply config from server settings and user settings.
    ScenarioConfig scenarioConfig = buildScenarioConfigForRun(dsm, scenarioId);
    if (scenarioId == Scenario::EnumType::NesFlappyParatroopa) {
        const auto* nesConfig = std::get_if<Config::NesFlappyParatroopa>(&scenarioConfig);
        if (!nesConfig) {
            cwc.sendResponse(
                Api::SimRun::Response::error(
                    ApiError("Scenario config mismatch for NES scenario")));
            return Idle{};
        }
        const NesConfigValidationResult validation =
            NesFlappyParatroopaScenario::validateConfig(*nesConfig);
        if (!validation.valid) {
            cwc.sendResponse(
                Api::SimRun::Response::error(
                    ApiError("Invalid NES config: " + validation.message)));
            return Idle{};
        }
    }

    newState.scenario->setConfig(scenarioConfig, *newState.world);

    // Run scenario setup to initialize world.
    newState.scenario->setup(*newState.world);

    // Register scenario with World for tick during advanceTime.
    newState.world->setScenario(newState.scenario.get());

    LOG_INFO(State, "Scenario '{}' applied to new world", toString(scenarioId));

    // Set run parameters.
    newState.stepDurationMs = cwc.command.timestep * 1000.0; // Convert seconds to milliseconds.
    newState.targetSteps =
        cwc.command.max_steps > 0 ? static_cast<uint32_t>(cwc.command.max_steps) : 0;
    newState.stepCount = 0;
    newState.frameLimit = cwc.command.max_frame_ms;

    spdlog::info(
        "Idle: World created, transitioning to {} (timestep={}ms, max_steps={}, "
        "max_frame_ms={})",
        cwc.command.start_paused ? "SimPaused" : "SimRunning",
        newState.stepDurationMs,
        cwc.command.max_steps,
        newState.frameLimit);

    // Send response: running=false if starting paused.
    cwc.sendResponse(Api::SimRun::Response::okay({ !cwc.command.start_paused, 0 }));

    // Transition to SimRunning or SimPaused based on start_paused flag.
    if (cwc.command.start_paused) {
        return SimPaused{ std::move(newState) };
    }
    return newState;
}

} // namespace State
} // namespace Server
} // namespace DirtSim
