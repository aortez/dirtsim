#ifndef WORLDBADHESIONCALCULATOR_H
#define WORLDBADHESIONCALCULATOR_H

#include "MaterialType.h"
#include "Vector2.h"
#include "WorldCalculatorBase.h"
#include "bitmaps/MaterialNeighborhood.h"

namespace DirtSim {

class World;

/**
 * Calculator for adhesion forces between cells in World.
 *
 * Adhesion forces create attractive forces between neighboring cells of
 * different material types. The force strength is based on the geometric
 * mean of the materials' adhesion properties, weighted by fill ratios
 * and distance.
 */
class WorldAdhesionCalculator : public WorldCalculatorBase {
public:
    // Data structure for adhesion force results.
    struct AdhesionForce {
        Vector2f force_direction;           // Direction of adhesive pull/resistance.
        float force_magnitude;              // Strength of adhesive force.
        Material::EnumType target_material; // Strongest interacting material.
        int contact_points;                 // Number of contact interfaces.
    };

    // Default constructor - calculator is stateless.
    WorldAdhesionCalculator() = default;

    // Main calculation method.
    AdhesionForce calculateAdhesionForce(const World& world, int x, int y) const;

    // Cache-optimized version using MaterialNeighborhood.
    AdhesionForce calculateAdhesionForce(
        const World& world, int x, int y, const MaterialNeighborhood& mat_n) const;

    // Adhesion parameters - NOTE: Now uses World.physicsSettings, these are legacy wrappers.
    // These methods are kept for backward compatibility but delegate to World.physicsSettings.
};

} // namespace DirtSim

#endif // WORLDBADHESIONCALCULATOR_H