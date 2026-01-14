#include "State.h"
#include "core/Assert.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/UUID.h"
#include "core/World.h"
#include "core/network/BinaryProtocol.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/evolution/FitnessResult.h"
#include "core/organisms/evolution/GenomeRepository.h"
#include "core/organisms/evolution/Mutation.h"
#include "core/organisms/evolution/Selection.h"
#include "server/StateMachine.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/EvolutionStop.h"
#include <algorithm>
#include <ctime>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Server {
namespace State {

namespace {
constexpr double TIMESTEP = 0.016; // 60 FPS physics.
} // namespace

void Evolution::onEnter(StateMachine& /*dsm*/)
{
    LOG_INFO(
        State,
        "Evolution: Starting with population={}, generations={}",
        evolutionConfig.populationSize,
        evolutionConfig.maxGenerations);

    // Record training start time.
    trainingStartTime_ = std::chrono::steady_clock::now();

    // Seed RNG.
    rng.seed(std::random_device{}());

    // Initialize population.
    initializePopulation();
}

void Evolution::onExit(StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Exiting at generation {}, eval {}", generation, currentEval);

    // Clean up any in-progress evaluation.
    evalWorld_.reset();

    // Store final best genome.
    storeBestGenome(dsm);
}

std::optional<Any> Evolution::tick(StateMachine& dsm)
{
    // Check if evolution complete.
    if (generation >= evolutionConfig.maxGenerations) {
        LOG_INFO(State, "Evolution complete: {} generations", generation);
        storeBestGenome(dsm);
        return Idle{};
    }

    DIRTSIM_ASSERT(!population.empty(), "Population must not be empty");
    DIRTSIM_ASSERT(
        currentEval < static_cast<int>(population.size()),
        "currentEval must be within population bounds");

    // Start a new evaluation if needed.
    if (!evalWorld_) {
        startEvaluation();
    }

    // Advance one physics step.
    evalWorld_->advanceTime(TIMESTEP);
    evalSimTime_ += TIMESTEP;

    // Broadcast render message for live training view.
    dsm.broadcastRenderMessage(
        evalWorld_->getData(),
        evalWorld_->getOrganismManager().getGrid(),
        scenarioId,
        Config::TreeGermination{});

    // Broadcast progress for real-time time display updates.
    broadcastProgress(dsm);

    // Track max energy.
    Tree* tree = evalWorld_->getOrganismManager().getTree(evalTreeId_);
    if (tree) {
        evalMaxEnergy_ = std::max(evalMaxEnergy_, tree->getEnergy());
    }

    // Check if evaluation complete (tree died or max time reached).
    const bool treeDied = (tree == nullptr);
    const bool timeUp = (evalSimTime_ >= evolutionConfig.maxSimulationTime);

    if (treeDied || timeUp) {
        finishEvaluation(dsm);
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

Any Evolution::onEvent(const Api::Exit::Cwc& cwc, StateMachine& /*dsm*/)
{
    LOG_INFO(State, "Evolution: Exit received, shutting down");
    cwc.sendResponse(Api::Exit::Response::okay(std::monostate{}));
    return Shutdown{};
}

void Evolution::initializePopulation()
{
    population.clear();
    fitnessScores.clear();

    population.reserve(evolutionConfig.populationSize);
    fitnessScores.resize(evolutionConfig.populationSize, 0.0);

    for (int i = 0; i < evolutionConfig.populationSize; ++i) {
        population.push_back(Genome::random(rng));
    }

    generation = 0;
    currentEval = 0;
    bestFitnessThisGen = 0.0;

    // Clear evaluation state.
    evalWorld_.reset();
    evalSimTime_ = 0.0;
    evalMaxEnergy_ = 0.0;
}

void Evolution::startEvaluation()
{
    DIRTSIM_ASSERT(
        currentEval < static_cast<int>(population.size()),
        "currentEval must be within population bounds");

    // Create fresh 9x9 world for evaluation.
    evalWorld_ = std::make_unique<World>(9, 9);
    DIRTSIM_ASSERT(evalWorld_ != nullptr, "World creation must succeed");

    // Clear to air.
    for (int y = 0; y < evalWorld_->getData().height; ++y) {
        for (int x = 0; x < evalWorld_->getData().width; ++x) {
            evalWorld_->getData().at(x, y) = Cell();
        }
    }

    // Add dirt at bottom 3 rows.
    for (int y = 6; y < evalWorld_->getData().height; ++y) {
        for (int x = 0; x < evalWorld_->getData().width; ++x) {
            evalWorld_->addMaterialAtCell(
                { static_cast<int16_t>(x), static_cast<int16_t>(y) },
                Material::EnumType::Dirt,
                1.0);
        }
    }

    // Create brain from genome and plant tree.
    auto brain = std::make_unique<NeuralNetBrain>(population[currentEval]);
    evalTreeId_ = evalWorld_->getOrganismManager().createTree(*evalWorld_, 4, 4, std::move(brain));

    // Reset evaluation tracking.
    evalSimTime_ = 0.0;
    evalMaxEnergy_ = 0.0;
}

void Evolution::finishEvaluation(StateMachine& dsm)
{
    // Get final lifespan.
    double lifespan = evalSimTime_;
    Tree* tree = evalWorld_->getOrganismManager().getTree(evalTreeId_);
    if (tree) {
        lifespan = tree->getAge();
    }

    // Compute fitness.
    const FitnessResult result{ .lifespan = lifespan, .maxEnergy = evalMaxEnergy_ };
    const double fitness =
        result.computeFitness(evolutionConfig.maxSimulationTime, evolutionConfig.energyReference);

    fitnessScores[currentEval] = fitness;

    // Update tracking.
    if (fitness > bestFitnessThisGen) {
        bestFitnessThisGen = fitness;
    }
    if (fitness > bestFitnessAllTime) {
        bestFitnessAllTime = fitness;

        // Store this genome as best.
        auto& repo = dsm.getGenomeRepository();
        const GenomeMetadata meta{
            .name = "gen_" + std::to_string(generation) + "_eval_" + std::to_string(currentEval),
            .fitness = fitness,
            .generation = generation,
            .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
            .scenarioId = scenarioId,
            .notes = "",
        };
        bestGenomeId = UUID::generate();
        repo.store(bestGenomeId, population[currentEval], meta);
        repo.markAsBest(bestGenomeId);

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

    // Advance to next individual.
    currentEval++;

    if (currentEval >= evolutionConfig.populationSize) {
        advanceGeneration(dsm);
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
    std::vector<Genome> offspring;
    std::vector<double> offspringFitness;
    offspring.reserve(evolutionConfig.populationSize);
    offspringFitness.reserve(evolutionConfig.populationSize);

    for (int i = 0; i < evolutionConfig.populationSize; ++i) {
        const Genome parent =
            tournamentSelect(population, fitnessScores, evolutionConfig.tournamentSize, rng);
        offspring.push_back(mutate(parent, mutationConfig, rng));
        offspringFitness.push_back(0.0); // Will be evaluated next generation.
    }

    // Elitist replacement: keep best from parents + offspring.
    population = elitistReplace(
        population, fitnessScores, offspring, offspringFitness, evolutionConfig.populationSize);

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

void Evolution::storeBestGenome(StateMachine& dsm)
{
    if (population.empty() || fitnessScores.empty()) {
        return;
    }

    // Find best in current population.
    int bestIdx = 0;
    double bestFit = fitnessScores[0];
    for (int i = 1; i < static_cast<int>(fitnessScores.size()); ++i) {
        if (fitnessScores[i] > bestFit) {
            bestFit = fitnessScores[i];
            bestIdx = i;
        }
    }

    auto& repo = dsm.getGenomeRepository();
    const GenomeMetadata meta{
        .name = "checkpoint_gen_" + std::to_string(generation),
        .fitness = bestFit,
        .generation = generation,
        .createdTimestamp = static_cast<uint64_t>(std::time(nullptr)),
        .scenarioId = scenarioId,
        .notes = "",
    };
    const GenomeId id = UUID::generate();
    repo.store(id, population[bestIdx], meta);

    if (bestFit >= bestFitnessAllTime) {
        repo.markAsBest(id);
        bestGenomeId = id;
    }

    LOG_INFO(
        State, "Evolution: Stored checkpoint genome (gen {}, fitness {:.4f})", generation, bestFit);
}

} // namespace State
} // namespace Server
} // namespace DirtSim
