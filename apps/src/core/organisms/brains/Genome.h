#pragma once

#include <random>
#include <vector>

namespace DirtSim {

/**
 * Neural network genome - a flat vector of weights for evolution.
 */
struct Genome {
    std::vector<double> weights;

    Genome();

    static Genome random(std::mt19937& rng);
    static Genome constant(double value);

    bool operator==(const Genome& other) const;
};

} // namespace DirtSim
