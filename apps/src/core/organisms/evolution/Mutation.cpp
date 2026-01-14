#include "Mutation.h"

#include "core/organisms/brains/Genome.h"

namespace DirtSim {

Genome mutate(const Genome& parent, const MutationConfig& config, std::mt19937& rng)
{
    Genome child = parent;

    std::normal_distribution<double> noise(0.0, config.sigma);
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    for (size_t i = 0; i < child.weights.size(); i++) {
        const double r = coin(rng);
        if (r < config.resetRate) {
            // Full reset (rare) - helps escape local optima.
            child.weights[i] = noise(rng) * 2.0;
        }
        else if (r < config.resetRate + config.rate) {
            // Gaussian perturbation (common).
            child.weights[i] += noise(rng);
        }
    }

    return child;
}

} // namespace DirtSim
