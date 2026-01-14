#pragma once

#include <random>
#include <vector>

namespace DirtSim {

struct Genome;

/**
 * Tournament selection: pick k random individuals, return the fittest.
 * Selection pressure adjustable via tournament size.
 */
Genome tournamentSelect(
    const std::vector<Genome>& population,
    const std::vector<double>& fitness,
    int tournamentSize,
    std::mt19937& rng);

/**
 * Elitist replacement: combine parents and offspring, keep top N.
 * Best solutions are never lost.
 */
std::vector<Genome> elitistReplace(
    const std::vector<Genome>& parents,
    const std::vector<double>& parentFitness,
    const std::vector<Genome>& offspring,
    const std::vector<double>& offspringFitness,
    int populationSize);

} // namespace DirtSim
