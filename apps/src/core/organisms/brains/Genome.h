#pragma once

#include "WeightType.h"

#include <random>
#include <vector>

namespace DirtSim {

/**
 * Neural network genome - a flat vector of weights for evolution.
 */
struct Genome {
    std::vector<WeightType> weights;

    Genome();

    static Genome random(std::mt19937& rng);
    static Genome constant(WeightType value);

    static constexpr size_t EXPECTED_WEIGHT_COUNT = 120088;
    static constexpr size_t EXPECTED_SIZE_BYTES = EXPECTED_WEIGHT_COUNT * sizeof(WeightType);

    size_t getSizeBytes() const { return weights.size() * sizeof(WeightType); }

    bool operator==(const Genome& other) const;
};

} // namespace DirtSim
