#include "Mutation.h"

#include "core/organisms/brains/Genome.h"
#include "core/organisms/brains/WeightType.h"

#include <algorithm>
#include <unordered_set>

namespace DirtSim {

namespace {
size_t clampMutationCount(int value, size_t maxValue)
{
    if (value <= 0 || maxValue == 0) {
        return 0;
    }
    if (static_cast<size_t>(value) >= maxValue) {
        return maxValue;
    }
    return static_cast<size_t>(value);
}

std::vector<size_t> sampleUniqueIndices(size_t domainSize, size_t count, std::mt19937& rng)
{
    count = std::min(count, domainSize);
    std::vector<size_t> indices;
    indices.reserve(count);

    if (count == 0 || domainSize == 0) {
        return indices;
    }

    std::unordered_set<size_t> selected;
    selected.reserve(count * 2);

    const size_t start = domainSize - count;
    for (size_t j = start; j < domainSize; ++j) {
        std::uniform_int_distribution<size_t> dist(0, j);
        const size_t t = dist(rng);
        if (!selected.insert(t).second) {
            selected.insert(j);
        }
    }

    for (const size_t idx : selected) {
        indices.push_back(idx);
    }
    std::shuffle(indices.begin(), indices.end(), rng);
    return indices;
}
} // namespace

Genome mutate(
    const Genome& parent, const MutationConfig& config, std::mt19937& rng, MutationStats* stats)
{
    if (stats) {
        stats->perturbations = 0;
        stats->resets = 0;
    }

    Genome child = parent;

    std::normal_distribution<WeightType> noise(0.0f, config.sigma);
    std::uniform_real_distribution<WeightType> coin(0.0f, 1.0f);

    if (config.useBudget) {
        const size_t weightCount = child.weights.size();
        const size_t resetCount = clampMutationCount(config.resetsPerOffspring, weightCount);
        const size_t perturbCount =
            clampMutationCount(config.perturbationsPerOffspring, weightCount - resetCount);
        const size_t totalCount = resetCount + perturbCount;

        const auto indices = sampleUniqueIndices(weightCount, totalCount, rng);
        for (size_t i = 0; i < indices.size(); ++i) {
            const size_t idx = indices[i];
            if (i < resetCount) {
                // Full reset (rare) - helps escape local optima.
                child.weights[idx] = noise(rng) * 2.0f;
                if (stats) {
                    stats->resets++;
                }
            }
            else {
                // Gaussian perturbation (common).
                child.weights[idx] += noise(rng);
                if (stats) {
                    stats->perturbations++;
                }
            }
        }
    }
    else {
        for (size_t i = 0; i < child.weights.size(); i++) {
            const WeightType r = coin(rng);
            if (r < config.resetRate) {
                // Full reset (rare) - helps escape local optima.
                child.weights[i] = noise(rng) * 2.0f;
                if (stats) {
                    stats->resets++;
                }
            }
            else if (r < config.resetRate + config.rate) {
                // Gaussian perturbation (common).
                child.weights[i] += noise(rng);
                if (stats) {
                    stats->perturbations++;
                }
            }
        }
    }

    return child;
}

} // namespace DirtSim
