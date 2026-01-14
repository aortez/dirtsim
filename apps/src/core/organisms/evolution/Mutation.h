#pragma once

#include "EvolutionConfig.h"

#include <random>

namespace DirtSim {

struct Genome;

/**
 * Mutate a genome by applying Gaussian noise to weights.
 * Occasionally resets weights entirely to escape local optima.
 */
Genome mutate(const Genome& parent, const MutationConfig& config, std::mt19937& rng);

} // namespace DirtSim
