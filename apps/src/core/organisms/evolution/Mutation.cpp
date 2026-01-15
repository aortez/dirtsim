#include "Mutation.h"

#include "core/organisms/brains/Genome.h"
#include "core/organisms/brains/WeightType.h"

namespace DirtSim {

Genome mutate(const Genome& parent, const MutationConfig& config, std::mt19937& rng)
{
    Genome child = parent;

    std::normal_distribution<WeightType> noise(0.0f, config.sigma);
    std::uniform_real_distribution<WeightType> coin(0.0f, 1.0f);

    for (size_t i = 0; i < child.weights.size(); i++) {
        const WeightType r = coin(rng);
        if (r < config.resetRate) {
            // Full reset (rare) - helps escape local optima.
            child.weights[i] = noise(rng) * 2.0f;
        }
        else if (r < config.resetRate + config.rate) {
            // Gaussian perturbation (common).
            child.weights[i] += noise(rng);
        }
    }

    return child;
}

} // namespace DirtSim
