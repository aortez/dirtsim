#include "Mutation.h"

#include "core/Assert.h"
#include "core/organisms/brains/Genome.h"
#include "core/organisms/brains/WeightType.h"

#include <algorithm>
#include <numeric>
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

// Distribute a budget across segments: floor of 1 per segment, remainder proportional to size.
std::vector<int> distributeBudget(const GenomeLayout& layout, int budget)
{
    const int segmentCount = static_cast<int>(layout.segments.size());
    std::vector<int> alloc(segmentCount, 0);

    if (budget <= 0 || segmentCount == 0) {
        return alloc;
    }

    // Floor: give each segment 1 (if budget allows).
    const int floorTotal = std::min(budget, segmentCount);
    for (int i = 0; i < floorTotal; ++i) {
        alloc[i] = 1;
    }
    int remaining = budget - floorTotal;

    if (remaining <= 0) {
        return alloc;
    }

    // Proportional: distribute remainder by segment size.
    const int totalSize = layout.totalSize();
    if (totalSize <= 0) {
        return alloc;
    }

    int distributed = 0;
    for (int i = 0; i < segmentCount; ++i) {
        const int share =
            static_cast<int>(static_cast<int64_t>(remaining) * layout.segments[i].size / totalSize);
        alloc[i] += share;
        distributed += share;
    }

    // Distribute rounding leftovers to the largest segments first.
    int leftover = remaining - distributed;
    if (leftover > 0) {
        // Build index list sorted by segment size descending.
        std::vector<int> order(segmentCount);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return layout.segments[a].size > layout.segments[b].size;
        });
        for (int i = 0; leftover > 0 && i < segmentCount; ++i) {
            alloc[order[i]]++;
            leftover--;
        }
    }

    return alloc;
}
} // namespace

Genome mutate(
    const Genome& parent,
    const MutationConfig& config,
    const GenomeLayout& layout,
    std::mt19937& rng,
    MutationStats* stats)
{
    if (stats) {
        stats->perturbations = 0;
        stats->resets = 0;
    }

    Genome child = parent;

    std::normal_distribution<WeightType> noise(0.0f, config.sigma);
    const size_t weightCount = child.weights.size();
    const size_t resetCount = clampMutationCount(config.resetsPerOffspring, weightCount);
    const size_t perturbCount =
        clampMutationCount(config.perturbationsPerOffspring, weightCount - resetCount);

    DIRTSIM_ASSERT(!layout.segments.empty(), "Mutation: layout must have segments");
    DIRTSIM_ASSERT(
        layout.totalSize() == static_cast<int>(weightCount),
        "Mutation: layout totalSize must match genome weight count");

    // Resets: sample globally (reset count is typically very small, 1-2).
    if (resetCount > 0) {
        const auto resetIndices = sampleUniqueIndices(weightCount, resetCount, rng);
        for (const size_t idx : resetIndices) {
            child.weights[idx] = noise(rng) * 2.0f;
            if (stats) {
                stats->resets++;
            }
        }
    }

    // Perturbations: distribute across segments with floor-of-1 per segment.
    const auto segBudgets = distributeBudget(layout, static_cast<int>(perturbCount));
    int segOffset = 0;
    for (size_t s = 0; s < layout.segments.size(); ++s) {
        const int segSize = layout.segments[s].size;
        const int segBudget = segBudgets[s];

        if (segBudget > 0 && segSize > 0) {
            const auto indices = sampleUniqueIndices(
                static_cast<size_t>(segSize), static_cast<size_t>(segBudget), rng);
            for (const size_t localIdx : indices) {
                const size_t globalIdx = static_cast<size_t>(segOffset) + localIdx;
                child.weights[globalIdx] += noise(rng);
                if (stats) {
                    stats->perturbations++;
                }
            }
        }
        segOffset += segSize;
    }

    return child;
}

} // namespace DirtSim
