#pragma once

#include "EvolutionConfig.h"
#include "GenomeLayout.h"

#include <random>

namespace DirtSim {

struct Genome;

struct MutationStats {
    int perturbations = 0;
    int resets = 0;

    int totalChanges() const { return perturbations + resets; }
};

/**
 * Mutate a genome by applying Gaussian noise to weights.
 * Occasionally resets weights entirely to escape local optima.
 * Budget is distributed across genome segments so every layer gets touched.
 */
Genome mutate(
    const Genome& parent,
    const MutationConfig& config,
    const GenomeLayout& layout,
    std::mt19937& rng,
    MutationStats* stats = nullptr);

} // namespace DirtSim
