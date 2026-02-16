#pragma once

#include "core/organisms/evolution/GenomeMetadata.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

/**
 * Progress update broadcast from server during evolution.
 * Not a request/response — pushed to subscribed clients.
 */
struct EvolutionProgress {
    int generation = 0;
    int maxGenerations = 0;
    int currentEval = 0;
    int populationSize = 0;
    int totalGenomeCount = 0;
    int genomeArchiveMaxSize = 0;
    double bestFitnessThisGen = 0.0;
    double bestFitnessAllTime = 0.0;
    double averageFitness = 0.0;
    int lastCompletedGeneration = -1;
    double lastGenerationFitnessMin = 0.0;
    double lastGenerationFitnessMax = 0.0;
    std::vector<uint32_t> lastGenerationFitnessHistogram;
    std::string bestThisGenSource = "none";
    GenomeId bestGenomeId{};
    double totalTrainingSeconds = 0.0; // Real-world seconds since training started.
    double currentSimTime = 0.0;       // Sim time for current individual.
    double cumulativeSimTime = 0.0;    // Total sim time across all individuals.
    double speedupFactor = 0.0;        // Sim time / real time.
    double etaSeconds = 0.0;           // Estimated time remaining.
    int activeParallelism = 0;         // Current allowed concurrency (background + main).
    double cpuPercent = 0.0;           // Latest system CPU measurement.

    nlohmann::json toJson() const;
    static constexpr const char* name() { return "EvolutionProgress"; }

    using serialize = zpp::bits::members<22>;
};

} // namespace Api
} // namespace DirtSim
