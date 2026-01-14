#pragma once

#include "core/MaterialType.h"
#include "core/Vector2.h"

namespace DirtSim {

/**
 * Hinge configuration for bone connections.
 */
enum class HingeEnd {
    CELL_A, // cell_a is the pivot point.
    CELL_B, // cell_b is the pivot point.
    NONE    // Symmetric spring - both ends free to rotate.
};

/**
 * Structural connection between two cells in an organism.
 */
struct Bone {
    Vector2i cell_a;
    Vector2i cell_b;
    double rest_distance;
    double stiffness;

    // Hinge/motor properties for rotational control.
    HingeEnd hinge_end = HingeEnd::NONE;
    double rotational_damping = 0.0;
};

/**
 * Get bone stiffness based on connected material types.
 */
double getBoneStiffness(Material::EnumType a, Material::EnumType b);

} // namespace DirtSim
