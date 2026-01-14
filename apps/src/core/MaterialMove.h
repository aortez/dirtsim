#pragma once

#include "MaterialType.h"
#include "Vector2.h"

namespace DirtSim {

/**
 * @brief Types of collisions that can occur during material transfer
 *
 * This enum defines how materials interact when they collide during
 * movement in the World physics simulation.
 */
enum class CollisionType : uint8_t {
    TRANSFER_ONLY,       // Material moves between cells (default behavior)
    ELASTIC_REFLECTION,  // Bouncing with energy conservation
    INELASTIC_COLLISION, // Bouncing with energy loss
    FRAGMENTATION,       // Break apart into smaller pieces
    ABSORPTION           // One material absorbs the other
};

/**
 * @brief Represents a material transfer between cells with collision physics
 *
 * This struct encapsulates all data needed to perform a material transfer
 * including collision detection, energy calculations, and physics responses.
 * It supports both simple transfers and complex collision interactions.
 *
 * Layout optimized for minimal size (~44 bytes):
 * - Coordinates packed as Vector2s (int16_t) - sufficient for grids up to 32767x32767.
 * - boundary_normal removed - computed on-the-fly via getDirection().
 * - CollisionType and Material::EnumType packed as uint8_t.
 * - All floating-point values use float (32-bit) instead of double (64-bit).
 */
struct MaterialMove {
    // Basic transfer data (optimized layout for packing).
    float amount;                // Amount of material to transfer.
    Vector2f momentum;           // Velocity/momentum of the moving material.
    Vector2s from;               // Source cell coordinates.
    Vector2s to;                 // Target cell coordinates.
    Material::EnumType material; // Type of material being transferred.
    CollisionType collision_type = CollisionType::TRANSFER_ONLY;

    // Collision-specific data.
    float collision_energy = 0.0f;        // Calculated impact energy.
    float restitution_coefficient = 0.0f; // Material-specific bounce factor.
    float material_mass = 0.0f;           // Mass of moving material.
    float target_mass = 0.0f;             // Mass of target material (if any).

    // Pressure from excess material that can't transfer.
    float pressure_from_excess = 0.0f; // Pressure to add to target cell.

    // Compute direction on-the-fly (replaces stored boundary_normal).
    inline Vector2f getDirection() const { return Vector2f(to.x - from.x, to.y - from.y); }
};

} // namespace DirtSim