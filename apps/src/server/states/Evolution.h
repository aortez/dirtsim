#pragma once

#include "StateForward.h"
#include "core/ScenarioId.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "server/Event.h"

#include <optional>
#include <random>
#include <vector>

namespace DirtSim {
namespace Server {
namespace State {

/**
 * Evolution state — runs genetic algorithm to evolve tree neural network brains.
 *
 * Each tick() evaluates one organism to completion (blocking), then advances
 * to the next individual or next generation. Stores best genomes in the
 * repository and broadcasts progress.
 */
struct Evolution {
    // Config.
    EvolutionConfig evolutionConfig;
    MutationConfig mutationConfig;
    ScenarioId scenarioId = ScenarioId::TreeGermination;

    // Population.
    std::vector<Genome> population;
    std::vector<double> fitnessScores;
    int generation = 0;
    int currentEval = 0;

    // Tracking.
    double bestFitnessThisGen = 0.0;
    double bestFitnessAllTime = 0.0;
    GenomeId bestGenomeId{};
    int saveInterval = 10; // Store best every N generations.

    // RNG.
    std::mt19937 rng;

    void onEnter(StateMachine& dsm);
    void onExit(StateMachine& dsm);

    // Called each frame by main loop to advance evolution.
    // Returns a state to transition to, or nullopt to stay in Evolution.
    std::optional<Any> tick(StateMachine& dsm);

    Any onEvent(const Api::EvolutionStop::Cwc& cwc, StateMachine& dsm);
    Any onEvent(const Api::Exit::Cwc& cwc, StateMachine& dsm);

    static constexpr const char* name() { return "Evolution"; }

private:
    void initializePopulation();
    double evaluateGenome(const Genome& genome, StateMachine& dsm);
    void advanceGeneration(StateMachine& dsm);
    void broadcastProgress(StateMachine& dsm);
    void storeBestGenome(StateMachine& dsm);
};

} // namespace State
} // namespace Server
} // namespace DirtSim
