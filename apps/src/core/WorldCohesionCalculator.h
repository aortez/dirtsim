#pragma once

#include "Vector2.h"
#include "WorldCalculatorBase.h"

namespace DirtSim {

class Cell;
class GridOfCells;
class MaterialNeighborhood;
class World;

/**
 * @brief Calculates cohesion forces for World physics.
 *
 * This class encapsulates cohesion-related calculations including:
 * - Resistance-based cohesion (movement threshold)
 * - Center-of-mass cohesion forces (attractive clustering)
 */
class WorldCohesionCalculator : public WorldCalculatorBase {
public:
    // Default constructor - calculator is stateless.
    WorldCohesionCalculator() = default;

    // Force calculation structures for cohesion physics (moved from World).
    struct CohesionForce {
        float resistance_magnitude; // Strength of cohesive resistance.
        int connected_neighbors;    // Number of same-material neighbors.
    };

    struct COMCohesionForce {
        Vector2f force_direction;     // Net force direction toward neighbors.
        float force_magnitude;        // Strength of cohesive pull.
        Vector2f center_of_neighbors; // Average position of connected neighbors.
        int active_connections;       // Number of neighbors contributing.
        // NEW fields for mass-based calculations:
        float total_neighbor_mass;  // Sum of all neighbor masses.
        float cell_mass;            // Mass of current cell.
        bool force_active;          // Whether force should be applied (cutoff check).
        float resistance_magnitude; // Cohesion resistance (for force blocking in resolveForces).
    };

    // Cohesion-specific constants.
    static constexpr float MIN_SUPPORT_FACTOR = 0.1f; // Minimum cohesion when no support.

    CohesionForce calculateCohesionForce(const World& world, int x, int y) const;

    COMCohesionForce calculateCOMCohesionForce(
        const World& world,
        int x,
        int y,
        int com_cohesion_range,
        const GridOfCells* grid = nullptr) const;

private:
    // Cache-optimized version using MaterialNeighborhood.
    COMCohesionForce calculateCOMCohesionForceCached(
        const World& world,
        int x,
        int y,
        int com_cohesion_range,
        const MaterialNeighborhood& mat_n) const;
};

} // namespace DirtSim
