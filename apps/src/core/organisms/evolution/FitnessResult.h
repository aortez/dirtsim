#pragma once

#include <algorithm>
#include <cmath>

namespace DirtSim {

/**
 * Raw metrics collected during organism evaluation.
 * Used to compute fitness score for evolution.
 */
struct FitnessResult {
    double lifespan = 0.0;         // How long the organism survived (seconds).
    double distanceTraveled = 0.0; // Straight-line displacement from spawn.
    double maxEnergy = 0.0;        // Peak energy achieved during lifetime.

    // Compute fitness using multiplicative formula.
    // Must survive AND move to score well; energy is optional.
    double computeFitness(
        double maxTime,
        int worldWidth,
        int worldHeight,
        double energyReference,
        bool includeEnergy) const
    {
        const double lifespanScore = lifespan / maxTime;
        const double maxDistance = std::max(
            1.0,
            std::sqrt(static_cast<double>(worldWidth * worldWidth + worldHeight * worldHeight)));
        const double distanceScore = distanceTraveled / maxDistance;
        const double baseScore = lifespanScore * (1.0 + distanceScore);

        if (!includeEnergy) {
            return baseScore;
        }

        const double energyScore = maxEnergy / energyReference;
        return baseScore * (1.0 + energyScore);
    }
};

} // namespace DirtSim
