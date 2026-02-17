#pragma once

namespace DirtSim {

/**
 * Raw metrics collected during organism evaluation.
 * Used to compute fitness score for evolution.
 */
struct FitnessResult {
    double lifespan = 0.0;    // How long the organism survived (seconds).
    double maxEnergy = 0.0;   // Peak energy achieved during lifetime.
    int commandsAccepted = 0; // Total accepted commands during lifetime.
    int commandsRejected = 0; // Total rejected commands during lifetime.
    int idleCancels = 0;      // Cancel commands issued while no action was active.
};

} // namespace DirtSim
