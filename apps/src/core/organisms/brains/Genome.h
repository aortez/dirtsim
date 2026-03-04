#pragma once

#include "WeightType.h"

#include <vector>

namespace DirtSim {

/**
 * Neural network genome - a flat vector of weights for evolution.
 */
struct Genome {
    std::vector<WeightType> weights;

    Genome() = default;
    explicit Genome(size_t weightCount);
    Genome(size_t weightCount, WeightType value);

    size_t getSizeBytes() const { return weights.size() * sizeof(WeightType); }

    bool operator==(const Genome& other) const;
};

} // namespace DirtSim
