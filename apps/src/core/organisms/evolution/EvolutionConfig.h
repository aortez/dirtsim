#pragma once

#include "core/ReflectSerializer.h"
#include <nlohmann/json.hpp>

namespace DirtSim {

/**
 * Configuration for the genetic algorithm evolution process.
 */
struct EvolutionConfig {
    int populationSize = 50;
    int tournamentSize = 3;
    int maxGenerations = 1;
    int maxParallelEvaluations = 0; // 0 = auto (use detected core count).
    int targetCpuPercent = 0;       // 0 = disabled. Typical: 50. Auto-tunes parallelism.

    // Evaluation settings.
    double maxSimulationTime = 1000.0; // Seconds of sim time per organism.
    double energyReference = 100.0;    // Reference energy for fitness scaling.
    double waterReference = 100.0;     // Reference water for resource scaling.
};

/**
 * Configuration for genome mutation during evolution.
 */
struct MutationConfig {
    double rate = 0.015;       // Probability each weight is mutated.
    double sigma = 0.05;       // Gaussian noise standard deviation.
    double resetRate = 0.0005; // Probability of full weight reset (escapes local optima).
};

inline void to_json(nlohmann::json& j, const EvolutionConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

inline void from_json(const nlohmann::json& j, EvolutionConfig& config)
{
    config = ReflectSerializer::from_json<EvolutionConfig>(j);
}

inline void to_json(nlohmann::json& j, const MutationConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

inline void from_json(const nlohmann::json& j, MutationConfig& config)
{
    config = ReflectSerializer::from_json<MutationConfig>(j);
}

} // namespace DirtSim
