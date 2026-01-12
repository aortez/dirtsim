#include "State.h"
#include "core/Assert.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
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

void Evolution::onEnter(StateMachine& /*dsm*/)
{
    LOG_INFO(
        State,
        "Evolution: Starting with population={}, generations={}",
        evolutionConfig.populationSize,
        evolutionConfig.maxGenerations);

    // Seed RNG.
    rng.seed(std::random_device{}());

    // Initialize population.
    initializePopulation();
}

void Evolution::onExit(StateMachine& dsm)
{
    LOG_INFO(State, "Evolution: Exiting at generation {}, eval {}", generation, currentEval);

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

    // Evaluate current organism to completion.
    const double fitness = evaluateGenome(population[currentEval], dsm);
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
        bestGenomeId = repo.store(population[currentEval], meta);
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

    // Advance to next individual.
    currentEval++;

    if (currentEval >= evolutionConfig.populationSize) {
        advanceGeneration(dsm);
    }

    // Broadcast progress.
    broadcastProgress(dsm);

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
}

double Evolution::evaluateGenome(const Genome& genome, StateMachine& /*dsm*/)
{
    // Create fresh 9x9 world for evaluation.
    auto world = std::make_unique<World>(9, 9);
    DIRTSIM_ASSERT(world != nullptr, "World creation must succeed");

    // Clear to air.
    for (int y = 0; y < world->getData().height; ++y) {
        for (int x = 0; x < world->getData().width; ++x) {
            world->getData().at(x, y) = Cell();
        }
    }

    // Add dirt at bottom 3 rows.
    for (int y = 6; y < world->getData().height; ++y) {
        for (int x = 0; x < world->getData().width; ++x) {
            world->addMaterialAtCell(
                { static_cast<int16_t>(x), static_cast<int16_t>(y) }, MaterialType::DIRT, 1.0);
        }
    }

    // Create brain from genome and plant tree.
    auto brain = std::make_unique<NeuralNetBrain>(genome);
    const OrganismId treeId =
        world->getOrganismManager().createTree(*world, 4, 4, std::move(brain));

    // Run simulation until tree dies or max time reached.
    constexpr double TIMESTEP = 0.016; // 60 FPS physics.
    const double maxTime = evolutionConfig.maxSimulationTime;

    double simTime = 0.0;
    double maxEnergy = 0.0;

    while (simTime < maxTime) {
        world->advanceTime(TIMESTEP);
        simTime += TIMESTEP;

        // Track max energy.
        Tree* tree = world->getOrganismManager().getTree(treeId);
        if (!tree) {
            break; // Tree died.
        }
        maxEnergy = std::max(maxEnergy, tree->getEnergy());
    }

    // Get final lifespan.
    double lifespan = simTime;
    Tree* tree = world->getOrganismManager().getTree(treeId);
    if (tree) {
        lifespan = tree->getAge();
    }

    // Compute fitness.
    const FitnessResult result{ .lifespan = lifespan, .maxEnergy = maxEnergy };
    return result.computeFitness(maxTime, evolutionConfig.energyReference);
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

    // Reset for new generation.
    generation++;
    currentEval = 0;
    bestFitnessThisGen = 0.0;
    std::fill(fitnessScores.begin(), fitnessScores.end(), 0.0);
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

    const Api::EvolutionProgress progress{
        .generation = generation,
        .maxGenerations = evolutionConfig.maxGenerations,
        .currentEval = currentEval,
        .populationSize = evolutionConfig.populationSize,
        .bestFitnessThisGen = bestFitnessThisGen,
        .bestFitnessAllTime = bestFitnessAllTime,
        .averageFitness = avgFitness,
        .bestGenomeId = bestGenomeId,
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
    const GenomeId id = repo.store(population[bestIdx], meta);

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
