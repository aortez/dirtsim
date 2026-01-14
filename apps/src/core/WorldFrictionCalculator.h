#pragma once

#include "MaterialType.h"
#include "Vector2.h"
#include "WorldCalculatorBase.h"
#include <vector>

namespace DirtSim {

class Cell;
class World;
class GridOfCells;

/**
 * @brief Calculates contact-based friction forces for World physics.
 *
 * This class implements true surface friction between adjacent cells based on:
 * - Normal force (pressure difference + weight for vertical contacts)
 * - Relative tangential velocity between surfaces
 * - Material-specific static and kinetic friction coefficients
 *
 * Friction forces oppose relative sliding motion between contacting surfaces.
 */
class WorldFrictionCalculator : public WorldCalculatorBase {
public:
    // Constructor requires GridOfCells reference for debug info storage.
    explicit WorldFrictionCalculator(GridOfCells& grid);

    /**
     * @brief Data structure representing a contact interface between two cells.
     */
    struct ContactInterface {
        Vector2s cell_A_pos;          // Position of first cell.
        Vector2s cell_B_pos;          // Position of second cell.
        Vector2f interface_normal;    // Unit vector pointing from A to B.
        float contact_area;           // Relative contact area (1.0 cardinal, 0.707 diagonal).
        float normal_force;           // Force pressing surfaces together.
        Vector2f relative_velocity;   // Velocity of A relative to B.
        Vector2f tangential_velocity; // Tangential component of relative velocity.
        float friction_coefficient;   // Combined friction coefficient (static or kinetic).
    };

    /**
     * @brief Calculate and apply friction forces for all contact interfaces.
     * @param world World providing access to grid and cells (non-const for modifications).
     * @param deltaTime Time step for physics integration.
     */
    void calculateAndApplyFrictionForces(World& world, float deltaTime);

    /**
     * @brief Set the global friction strength multiplier.
     * @param strength Multiplier for all friction forces (0.0 = disabled, 1.0 = normal).
     */
    void setFrictionStrength(float strength) { friction_strength_ = strength; }

    /**
     * @brief Get the global friction strength multiplier.
     * @return Current friction strength.
     */
    float getFrictionStrength() const { return friction_strength_; }

private:
    /**
     * @brief Accumulate friction forces from all contact interfaces (cached path).
     * @param world World providing access to grid and cells.
     */
    void accumulateFrictionForces(World& world);

    /**
     * @brief Detect all contact interfaces in the world.
     * @param world World providing access to grid and cells.
     * @return Vector of contact interfaces with calculated properties.
     */
    std::vector<ContactInterface> detectContactInterfaces(const World& world) const;

    /**
     * @brief Calculate normal force for a contact interface.
     * @param world World providing access to grid and cells.
     * @param cellA First cell in contact.
     * @param cellB Second cell in contact.
     * @param posA Position of first cell.
     * @param posB Position of second cell.
     * @param interface_normal Normal vector of interface (A to B).
     * @return Normal force magnitude.
     */
    float calculateNormalForce(
        const World& world,
        const Cell& cellA,
        const Cell& cellB,
        const Vector2s& posA,
        const Vector2s& posB,
        const Vector2f& interface_normal) const;

    /**
     * @brief Calculate friction coefficient based on relative tangential velocity.
     * @param tangential_speed Magnitude of tangential relative velocity.
     * @param propsA Material properties of first cell.
     * @param propsB Material properties of second cell.
     * @return Combined friction coefficient.
     */
    float calculateFrictionCoefficient(
        float tangential_speed,
        const Material::Properties& propsA,
        const Material::Properties& propsB) const;

    /**
     * @brief Decompose relative velocity into normal and tangential components.
     * @param relative_velocity Velocity of A relative to B.
     * @param interface_normal Normal vector of interface.
     * @return Tangential component of relative velocity.
     */
    Vector2f calculateTangentialVelocity(
        const Vector2f& relative_velocity, const Vector2f& interface_normal) const;

    /**
     * @brief Accumulate friction forces from pre-detected contacts (reference path).
     * @param world World providing access to grid and cells (non-const for modifications).
     * @param contacts Vector of contact interfaces with calculated properties.
     */
    void accumulateFrictionFromContacts(
        World& world, const std::vector<ContactInterface>& contacts);

    GridOfCells& grid_; // Reference to grid for debug info storage.

    // Configuration parameters.
    float friction_strength_ = 1.0f;

    // Physical constants.
    static constexpr float MIN_NORMAL_FORCE = 0.01f;     // Minimum normal force for friction.
    static constexpr float MIN_TANGENTIAL_SPEED = 1e-6f; // Minimum speed to apply friction.
};

} // namespace DirtSim
