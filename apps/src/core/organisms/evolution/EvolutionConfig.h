#pragma once

namespace DirtSim {

/**
 * Configuration for the genetic algorithm evolution process.
 */
struct EvolutionConfig {
    int populationSize = 50;
    int tournamentSize = 3;
    int maxGenerations = 100;

    // Evaluation settings.
    double maxSimulationTime = 600.0; // 10 minutes sim time per organism.
    double energyReference = 100.0;   // Reference energy for fitness scaling.
};

/**
 * Configuration for genome mutation during evolution.
 */
struct MutationConfig {
    double rate = 0.015;       // Probability each weight is mutated.
    double sigma = 0.05;       // Gaussian noise standard deviation.
    double resetRate = 0.0005; // Probability of full weight reset (escapes local optima).
};

} // namespace DirtSim
