#include "Selection.h"

#include "core/organisms/brains/Genome.h"

#include <algorithm>
#include <cassert>

namespace DirtSim {

Genome tournamentSelect(
    const std::vector<Genome>& population,
    const std::vector<double>& fitness,
    int tournamentSize,
    std::mt19937& rng)
{
    assert(!population.empty());
    assert(population.size() == fitness.size());
    assert(tournamentSize > 0);

    std::uniform_int_distribution<int> dist(0, static_cast<int>(population.size()) - 1);

    int bestIdx = dist(rng);
    double bestFitness = fitness[bestIdx];

    for (int i = 1; i < tournamentSize; i++) {
        const int idx = dist(rng);
        if (fitness[idx] > bestFitness) {
            bestIdx = idx;
            bestFitness = fitness[idx];
        }
    }

    return population[bestIdx];
}

std::vector<Genome> elitistReplace(
    const std::vector<Genome>& parents,
    const std::vector<double>& parentFitness,
    const std::vector<Genome>& offspring,
    const std::vector<double>& offspringFitness,
    int populationSize)
{
    assert(parents.size() == parentFitness.size());
    assert(offspring.size() == offspringFitness.size());
    assert(populationSize > 0);

    // Combine parents and offspring with their fitness scores.
    std::vector<std::pair<double, Genome>> pool;
    pool.reserve(parents.size() + offspring.size());

    for (size_t i = 0; i < parents.size(); i++) {
        pool.emplace_back(parentFitness[i], parents[i]);
    }
    for (size_t i = 0; i < offspring.size(); i++) {
        pool.emplace_back(offspringFitness[i], offspring[i]);
    }

    // Sort by fitness descending.
    std::sort(
        pool.begin(), pool.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    // Take top populationSize.
    std::vector<Genome> nextGeneration;
    nextGeneration.reserve(populationSize);

    const int count = std::min(populationSize, static_cast<int>(pool.size()));
    for (int i = 0; i < count; i++) {
        nextGeneration.push_back(pool[i].second);
    }

    return nextGeneration;
}

} // namespace DirtSim
