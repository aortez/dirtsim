#pragma once

namespace DirtSim {

/**
 * Raw metrics collected during organism evaluation.
 * Used to compute fitness score for evolution.
 */
struct FitnessResult {
    double lifespan = 0.0;         // How long the organism survived (seconds).
    double distanceTraveled = 0.0; // Straight-line displacement from spawn.
    double maxEnergy = 0.0;        // Peak energy achieved during lifetime.
};

} // namespace DirtSim
