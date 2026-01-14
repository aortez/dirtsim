#pragma once

namespace DirtSim {

/**
 * Raw metrics collected during organism evaluation.
 * Used to compute fitness score for evolution.
 */
struct FitnessResult {
    double lifespan = 0.0;  // How long the organism survived (seconds).
    double maxEnergy = 0.0; // Peak energy achieved during lifetime.

    // Compute fitness using multiplicative formula.
    // Must survive AND grow to score well.
    double computeFitness(double maxTime, double energyReference) const
    {
        const double lifespanScore = lifespan / maxTime;
        const double energyScore = maxEnergy / energyReference;
        return lifespanScore * (1.0 + energyScore);
    }
};

} // namespace DirtSim
