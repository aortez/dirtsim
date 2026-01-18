#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
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
#include "server/api/TrainingResultAvailable.h"
#include <algorithm>
#include <ctime>
#include <limits>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Server {
namespace State {

namespace {
constexpr double TIMESTEP = 0.016; // 60 FPS physics.

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

Vector2i findSpawnCell(World& world)
{
    auto& data = world.getData();
    const int width = data.width;
    const int height = data.height;
    const int centerX = width / 2;
    const int centerY = height / 2;

    const auto isSpawnable = [&world, &data](int x, int y) {
        if (!data.inBounds(x, y)) {
            return false;
        }
        if (!data.at(x, y).isAir()) {
            return false;
        }
        return !world.getOrganismManager().hasOrganism({ x, y });
    };

    if (isSpawnable(centerX, centerY)) {
        return { centerX, centerY };
    }

    auto findNearestInRows = [&](int startY, int endY) -> std::optional<Vector2i> {
        if (startY > endY) {
            return std::nullopt;
        }

        long long bestDistance = std::numeric_limits<long long>::max();
        Vector2i best{ 0, 0 };
        bool found = false;

        for (int y = startY; y <= endY; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!isSpawnable(x, y)) {
                    continue;
                }
                const long long dx = static_cast<long long>(x) - centerX;
                const long long dy = static_cast<long long>(y) - centerY;
                const long long distance = dx * dx + dy * dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = { x, y };
                    found = true;
                }
            }
        }

        if (!found) {
            return std::nullopt;
        }
        return best;
    };

    if (auto above = findNearestInRows(0, centerY); above.has_value()) {
        return above.value();
    }

    if (auto below = findNearestInRows(centerY + 1, height - 1); below.has_value()) {
        return below.value();
    }

    if (world.getOrganismManager().hasOrganism({ centerX, centerY })) {
        DIRTSIM_ASSERT(false, "Evolution: Spawn location already occupied");
    }

    data.at(centerX, centerY).clear();
    return { centerX, centerY };
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
    trainingSessionId_ = UUID::generate();
    trainingResultAvailableSent_ = false;
    pendingTrainingResult_.reset();

    // Seed RNG.
    rng.seed(std::random_device{}());

    brainRegistry_ = TrainingBrainRegistry::createDefault();

    // Initialize population.
    initializePopulation(dsm);
}

void Evolution::onExit(StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Exiting at generation {}, eval {}", generation, currentEval);

    // Clean up any in-progress evaluation.
    evalWorld_.reset();
    evalScenario_.reset();

    // Store final best genome.
    storeBestGenome(dsm);
}

std::optional<Any> Evolution::tick(StateMachine& dsm)
{
    if (trainingComplete_) {
        auto nextState = broadcastTrainingResultAvailable(dsm);
        if (nextState.has_value()) {
            return nextState;
        }
        return std::nullopt;
    }

    // Check if evolution complete.
    if (generation >= evolutionConfig.maxGenerations) {
        LOG_INFO(State, "Evolution complete: {} generations", generation);
        trainingComplete_ = true;
        auto nextState = broadcastTrainingResultAvailable(dsm);
        if (nextState.has_value()) {
            return nextState;
        }
        return std::nullopt;
    }

    DIRTSIM_ASSERT(!population.empty(), "Population must not be empty");
    DIRTSIM_ASSERT(
        currentEval < static_cast<int>(population.size()),
        "currentEval must be within population bounds");

    // Start a new evaluation if needed.
    if (!evalWorld_) {
        startEvaluation(dsm);
    }

    // Advance one physics step.
    evalWorld_->advanceTime(TIMESTEP);
    evalSimTime_ += TIMESTEP;

    // Broadcast render message for live training view.
    dsm.broadcastRenderMessage(
        evalWorld_->getData(),
        evalWorld_->getOrganismManager().getGrid(),
        trainingSpec.scenarioId,
        evalScenarioConfig_);

    // Broadcast progress for real-time time display updates.
    broadcastProgress(dsm);

    Organism::Body* organism = evalWorld_->getOrganismManager().getOrganism(evalOrganismId_);
    if (organism) {
        evalLastPosition_ = organism->position;
    }

    if (trainingSpec.organismType == OrganismType::TREE) {
        Tree* tree = evalWorld_->getOrganismManager().getTree(evalOrganismId_);
        if (tree) {
            evalMaxEnergy_ = std::max(evalMaxEnergy_, tree->getEnergy());
        }
    }

    // Check if evaluation complete (organism died or max time reached).
    const bool organismDied = (organism == nullptr);
    const bool timeUp = (evalSimTime_ >= evolutionConfig.maxSimulationTime);

    if (organismDied || timeUp) {
        finishEvaluation(dsm);
        if (trainingComplete_) {
            auto nextState = broadcastTrainingResultAvailable(dsm);
            if (nextState.has_value()) {
                return nextState;
            }
            return std::nullopt;
        }
    }

    return std::nullopt;
}

Any Evolution::onEvent(const Api::EvolutionStop::Cwc& cwc, StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Stopping at generation {}, eval {}", generation, currentEval);
    storeBestGenome(dsm);
    cwc.sendResponse(Api::EvolutionStop::Response::okay(std::monostate{}));
    return Idle{};
}

Any Evolution::onEvent(const Api::TrainingResultAvailableAck::Cwc& cwc, StateMachine& /*dsm*/)
{
    if (!pendingTrainingResult_.has_value()) {
        cwc.sendResponse(Api::TrainingResultAvailableAck::Response::error(
            ApiError("TrainingResultAvailableAck received without pending result")));
        return std::move(*this);
    }

    Api::TrainingResultAvailableAck::Okay response;
    response.acknowledged = true;
    cwc.sendResponse(Api::TrainingResultAvailableAck::Response::okay(std::move(response)));

    trainingResultAvailableSent_ = false;
    UnsavedTrainingResult result = std::move(pendingTrainingResult_.value());
    pendingTrainingResult_.reset();
    return result;
}

Any Evolution::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Evolution: Exit received, shutting down");
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
                population.push_back(Individual{ .brainKind = spec.brainKind,
                                                 .brainVariant = spec.brainVariant,
                                                 .genome = genome.value(),
                                                 .allowsMutation = entry->allowsMutation });
            }

            for (int i = 0; i < spec.randomCount; ++i) {
                population.push_back(Individual{ .brainKind = spec.brainKind,
                                                 .brainVariant = spec.brainVariant,
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
                population.push_back(Individual{ .brainKind = spec.brainKind,
                                                 .brainVariant = spec.brainVariant,
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

    // Clear evaluation state.
    evalWorld_.reset();
    evalScenario_.reset();
    evalOrganismId_ = INVALID_ORGANISM_ID;
    evalSpawnPosition_ = Vector2d{ 0.0, 0.0 };
    evalLastPosition_ = Vector2d{ 0.0, 0.0 };
    evalSimTime_ = 0.0;
    evalMaxEnergy_ = 0.0;
    evalScenarioConfig_ = Config::Empty{};
}

void Evolution::startEvaluation(StateMachine& dsm)
{
    DIRTSIM_ASSERT(
        currentEval < static_cast<int>(population.size()),
        "currentEval must be within population bounds");

    auto& registry = dsm.getScenarioRegistry();
    const ScenarioMetadata* metadata = registry.getMetadata(trainingSpec.scenarioId);
    DIRTSIM_ASSERT(metadata != nullptr, "Training scenario not found in registry");

    const int width = metadata->requiredWidth > 0 ? metadata->requiredWidth : 9;
    const int height = metadata->requiredHeight > 0 ? metadata->requiredHeight : 9;

    evalWorld_ = std::make_unique<World>(width, height);
    DIRTSIM_ASSERT(evalWorld_ != nullptr, "World creation must succeed");

    evalScenario_ = registry.createScenario(trainingSpec.scenarioId);
    DIRTSIM_ASSERT(evalScenario_ != nullptr, "Training scenario factory failed");

    evalScenario_->setup(*evalWorld_);
    evalWorld_->setScenario(evalScenario_.get());
    evalScenarioConfig_ = evalScenario_->getConfig();

    const Vector2i spawnCell = findSpawnCell(*evalWorld_);

    const Individual& individual = population[currentEval];
    const std::string variant = individual.brainVariant.value_or("");
    const BrainRegistryEntry* entry =
        brainRegistry_.find(trainingSpec.organismType, individual.brainKind, variant);
    DIRTSIM_ASSERT(entry != nullptr, "Evolution: Brain kind is not registered");

    const Genome* genomePtr = individual.genome.has_value() ? &individual.genome.value() : nullptr;
    if (entry->requiresGenome) {
        DIRTSIM_ASSERT(genomePtr != nullptr, "Evolution: Genome required but missing");
    }

    evalOrganismId_ = entry->spawn(*evalWorld_, spawnCell.x, spawnCell.y, genomePtr);
    DIRTSIM_ASSERT(evalOrganismId_ != INVALID_ORGANISM_ID, "Evolution: Spawn failed");

    const Organism::Body* organism = evalWorld_->getOrganismManager().getOrganism(evalOrganismId_);
    DIRTSIM_ASSERT(organism != nullptr, "Evolution: Spawned organism not found");
    evalSpawnPosition_ = organism->position;
    evalLastPosition_ = evalSpawnPosition_;

    // Reset evaluation tracking.
    evalSimTime_ = 0.0;
    evalMaxEnergy_ = 0.0;
}

void Evolution::finishEvaluation(StateMachine& dsm)
{
    // Get final lifespan.
    double lifespan = evalSimTime_;
    Organism::Body* organism = evalWorld_->getOrganismManager().getOrganism(evalOrganismId_);
    if (organism) {
        lifespan = organism->getAge();
    }

    Vector2d delta{ evalLastPosition_.x - evalSpawnPosition_.x,
                    evalLastPosition_.y - evalSpawnPosition_.y };
    const double distanceTraveled = delta.mag();

    // Compute fitness.
    const FitnessResult result{ .lifespan = lifespan,
                                .distanceTraveled = distanceTraveled,
                                .maxEnergy = evalMaxEnergy_ };
    const double fitness = computeFitnessForOrganism(
        result,
        trainingSpec.organismType,
        evalWorld_->getData().width,
        evalWorld_->getData().height,
        evolutionConfig);

    fitnessScores[currentEval] = fitness;

    // Update tracking.
    if (fitness > bestFitnessThisGen) {
        bestFitnessThisGen = fitness;
    }
    if (fitness > bestFitnessAllTime) {
        bestFitnessAllTime = fitness;

        const Individual& individual = population[currentEval];
        if (individual.genome.has_value()) {
            auto& repo = dsm.getGenomeRepository();
            const GenomeMetadata meta{
                .name =
                    "gen_" + std::to_string(generation) + "_eval_" + std::to_string(currentEval),
                .fitness = fitness,
                .generation = generation,
                .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
                .scenarioId = trainingSpec.scenarioId,
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
            fitness,
            generation,
            currentEval);
    }

    LOG_DEBUG(
        State,
        "Evolution: gen={} eval={}/{} fitness={:.4f}",
        generation,
        currentEval + 1,
        evolutionConfig.populationSize,
        fitness);

    // Add this individual's sim time to cumulative total.
    cumulativeSimTime_ += evalSimTime_;

    // Clean up world.
    evalWorld_.reset();
    evalScenario_.reset();
    evalOrganismId_ = INVALID_ORGANISM_ID;

    // Advance to next individual.
    currentEval++;

    if (currentEval >= evolutionConfig.populationSize) {
        if (generation + 1 >= evolutionConfig.maxGenerations) {
            double avgFitness = 0.0;
            for (const double fitnessScore : fitnessScores) {
                avgFitness += fitnessScore;
            }
            if (!fitnessScores.empty()) {
                avgFitness /= fitnessScores.size();
            }

            auto now = std::chrono::steady_clock::now();
            finalTrainingSeconds_ = std::chrono::duration<double>(now - trainingStartTime_).count();
            finalAverageFitness_ = avgFitness;
            trainingComplete_ = true;
            generation = evolutionConfig.maxGenerations;
        }
        else {
            advanceGeneration(dsm);
        }
    }

    // Broadcast progress.
    broadcastProgress(dsm);
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
    std::vector<std::pair<double, Individual>> pool;
    pool.reserve(population.size() + offspring.size());
    for (size_t i = 0; i < population.size(); ++i) {
        pool.emplace_back(fitnessScores[i], population[i]);
    }
    for (size_t i = 0; i < offspring.size(); ++i) {
        pool.emplace_back(offspringFitness[i], offspring[i]);
    }

    std::sort(
        pool.begin(), pool.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    population.clear();
    population.reserve(evolutionConfig.populationSize);
    const int count = std::min(evolutionConfig.populationSize, static_cast<int>(pool.size()));
    for (int i = 0; i < count; ++i) {
        population.push_back(pool[i].second);
    }

    // Advance to next generation.
    generation++;

    // Only reset for next generation if we're not at the end.
    // This preserves currentEval = populationSize in the final broadcast,
    // giving the UI a clean "all evals complete" signal.
    if (generation < evolutionConfig.maxGenerations) {
        currentEval = 0;
        bestFitnessThisGen = 0.0;
        std::fill(fitnessScores.begin(), fitnessScores.end(), 0.0);
    }
}

void Evolution::broadcastProgress(StateMachine& dsm)
{
    // Calculate average fitness of evaluated individuals.
    double avgFitness = 0.0;
    if (currentEval > 0) {
        for (int i = 0; i < currentEval; ++i) {
            avgFitness += fitnessScores[i];
        }
        avgFitness /= currentEval;
    }

    // Calculate total training time.
    auto now = std::chrono::steady_clock::now();
    double totalSeconds = std::chrono::duration<double>(now - trainingStartTime_).count();

    // Cumulative sim time = completed individuals + current individual's progress.
    double cumulative = cumulativeSimTime_ + evalSimTime_;

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

    const Api::EvolutionProgress progress{
        .generation = generation,
        .maxGenerations = evolutionConfig.maxGenerations,
        .currentEval = currentEval,
        .populationSize = evolutionConfig.populationSize,
        .bestFitnessThisGen = bestFitnessThisGen,
        .bestFitnessAllTime = bestFitnessAllTime,
        .averageFitness = avgFitness,
        .bestGenomeId = bestGenomeId,
        .totalTrainingSeconds = totalSeconds,
        .currentSimTime = evalSimTime_,
        .cumulativeSimTime = cumulative,
        .speedupFactor = speedup,
        .etaSeconds = eta,
    };

    dsm.broadcastEventData(Api::EvolutionProgress::name(), Network::serialize_payload(progress));
}

std::optional<Any> Evolution::broadcastTrainingResultAvailable(StateMachine& dsm)
{
    if (trainingResultAvailableSent_) {
        return std::nullopt;
    }

    if (!pendingTrainingResult_.has_value()) {
        pendingTrainingResult_ = buildUnsavedTrainingResult();
    }

    Api::TrainingResultAvailable available;
    available.summary = pendingTrainingResult_->summary;
    available.candidates.reserve(pendingTrainingResult_->candidates.size());
    for (const auto& candidate : pendingTrainingResult_->candidates) {
        available.candidates.push_back(Api::TrainingResultAvailable::Candidate{
            .id = candidate.id,
            .fitness = candidate.fitness,
            .brainKind = candidate.brainKind,
            .brainVariant = candidate.brainVariant,
            .generation = candidate.generation,
        });
    }

    dsm.recordTrainingResult(available);

    auto* wsService = dsm.getWebSocketService();
    if (wsService) {
        const auto response =
            wsService->sendCommandAndGetResponse<Api::TrainingResultAvailable::OkayType>(
                available, 5000);
        if (response.isError()) {
            LOG_WARN(State, "TrainingResultAvailable send failed: {}", response.errorValue());
        }
        else if (response.value().isError()) {
            LOG_WARN(
                State,
                "TrainingResultAvailable response error: {}",
                response.value().errorValue().message);
        }
        else {
            trainingResultAvailableSent_ = false;
            UnsavedTrainingResult result = std::move(pendingTrainingResult_.value());
            pendingTrainingResult_.reset();
            return Any{ result };
        }
    }

    dsm.broadcastEventData(
        Api::TrainingResultAvailable::name(), Network::serialize_payload(available));
    trainingResultAvailableSent_ = true;

    return std::nullopt;
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
        .scenarioId = trainingSpec.scenarioId,
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
    result.summary.scenarioId = trainingSpec.scenarioId;
    result.summary.organismType = trainingSpec.organismType;
    result.summary.populationSize = evolutionConfig.populationSize;
    result.summary.maxGenerations = evolutionConfig.maxGenerations;
    result.summary.completedGenerations = evolutionConfig.maxGenerations;
    result.summary.bestFitness = bestFitnessAllTime;
    result.summary.averageFitness = finalAverageFitness_;
    result.summary.totalTrainingSeconds = finalTrainingSeconds_;
    result.summary.trainingSessionId = trainingSessionId_;

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
