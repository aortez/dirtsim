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
    int genomeArchiveMaxSize = 0; // Per organismType+brainKind cap for managed genomes.
    // Latest robust median for genome brains, or latest evaluated-generation best for non-genome
    // brains.
    double bestFitnessThisGen = 0.0;
    double bestFitnessAllTime = 0.0;
    uint64_t robustEvaluationCount = 0;
    // Running average fitness for the currently evaluated generation.
    double averageFitness = 0.0;
    int lastCompletedGeneration = -1;
    // Final average fitness for lastCompletedGeneration.
    double lastGenerationAverageFitness = 0.0;
    double lastGenerationFitnessMin = 0.0;
    double lastGenerationFitnessMax = 0.0;
    std::vector<uint32_t> lastGenerationFitnessHistogram;
    std::string bestThisGenSource = "none";
    GenomeId bestGenomeId{};
    double totalTrainingSeconds = 0.0;     // Real-world seconds since training started.
    double currentSimTime = 0.0;           // Sim time for current individual.
    double cumulativeSimTime = 0.0;        // Total sim time across all individuals.
    double speedupFactor = 0.0;            // Sim time / real time.
    double etaSeconds = 0.0;               // Estimated time remaining.
    int activeParallelism = 0;             // Current allowed concurrency (background + main).
    double cpuPercent = 0.0;               // Latest system CPU measurement.
    std::vector<double> cpuPercentPerCore; // Latest per-core CPU measurements.

    // Breeding telemetry from the most recent offspring generation step.
    double lastBreedingPerturbationsAvg = 0.0;
    double lastBreedingResetsAvg = 0.0;
    double lastBreedingWeightChangesAvg = 0.0;
    int lastBreedingWeightChangesMin = 0;
    int lastBreedingWeightChangesMax = 0;

    // Telemetry from the most recently completed generation evaluation.
    int lastGenerationEliteCarryoverCount = 0;
    int lastGenerationSeedCount = 0;
    int lastGenerationOffspringCloneCount = 0;
    int lastGenerationOffspringMutatedCount = 0;
    int lastGenerationOffspringCloneBeatsParentCount = 0;
    double lastGenerationOffspringCloneAvgDeltaFitness = 0.0;
    int lastGenerationOffspringMutatedBeatsParentCount = 0;
    double lastGenerationOffspringMutatedAvgDeltaFitness = 0.0;
    int lastGenerationPhenotypeUniqueCount = 0;
    int lastGenerationPhenotypeUniqueEliteCarryoverCount = 0;
    int lastGenerationPhenotypeUniqueOffspringMutatedCount = 0;
    int lastGenerationPhenotypeNovelOffspringMutatedCount = 0;

    nlohmann::json toJson() const;
    static constexpr const char* name() { return "EvolutionProgress"; }

    using serialize = zpp::bits::members<42>;
};

} // namespace Api
} // namespace DirtSim
