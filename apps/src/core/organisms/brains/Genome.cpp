#include "Genome.h"

#include <cmath>

namespace DirtSim {

namespace {

// Input layout: 2250 materials + 6 state + 7 command one-hot + 1 progress.
constexpr int INPUT_SIZE = 2264;
constexpr int HIDDEN_SIZE = 48;
// Output layout: 7 commands + 225 positions.
constexpr int OUTPUT_SIZE = 232;

constexpr int W_IH_SIZE = INPUT_SIZE * HIDDEN_SIZE;
constexpr int B_H_SIZE = HIDDEN_SIZE;
constexpr int W_HO_SIZE = HIDDEN_SIZE * OUTPUT_SIZE;
constexpr int B_O_SIZE = OUTPUT_SIZE;
constexpr int TOTAL_WEIGHTS = W_IH_SIZE + B_H_SIZE + W_HO_SIZE + B_O_SIZE;

} // namespace

Genome::Genome() : weights(TOTAL_WEIGHTS, 0.0f)
{}

Genome Genome::random(std::mt19937& rng)
{
    Genome g;

    // Xavier initialization: stddev = sqrt(2 / (fan_in + fan_out)).
    WeightType ih_stddev = std::sqrt(2.0f / (INPUT_SIZE + HIDDEN_SIZE));
    WeightType ho_stddev = std::sqrt(2.0f / (HIDDEN_SIZE + OUTPUT_SIZE));

    std::normal_distribution<WeightType> ih_dist(0.0f, ih_stddev);
    std::normal_distribution<WeightType> ho_dist(0.0f, ho_stddev);

    int idx = 0;

    // W_ih weights.
    for (int i = 0; i < W_IH_SIZE; i++) {
        g.weights[idx++] = ih_dist(rng);
    }

    // b_h biases (zero init).
    for (int i = 0; i < B_H_SIZE; i++) {
        g.weights[idx++] = 0.0f;
    }

    // W_ho weights.
    for (int i = 0; i < W_HO_SIZE; i++) {
        g.weights[idx++] = ho_dist(rng);
    }

    // b_o biases (zero init).
    for (int i = 0; i < B_O_SIZE; i++) {
        g.weights[idx++] = 0.0f;
    }

    return g;
}

Genome Genome::constant(WeightType value)
{
    Genome g;
    g.weights.assign(TOTAL_WEIGHTS, value);
    return g;
}

bool Genome::operator==(const Genome& other) const
{
    return weights == other.weights;
}

} // namespace DirtSim
