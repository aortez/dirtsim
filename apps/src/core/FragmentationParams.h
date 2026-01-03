#pragma once

#include <cmath>

namespace DirtSim {

/**
 * @brief Parameters controlling fragmentation behavior.
 *
 * Used by the fragmentation system to control how material breaks apart
 * on high-energy collisions or other events. Can be customized per-material
 * or for specific scenario events.
 */
struct FragmentationParams {
    // Blend between reflection-based (0.0) and radial/explosion-based (1.0) spray direction.
    double radial_bias = 0.6;

    // Arc width range in radians. Scales with collision energy.
    double min_arc = M_PI / 3.0; // 60 degrees at threshold energy.
    double max_arc = M_PI;       // 180 degrees at high energy.

    // Edge fragments move faster than center to avoid collision. >1.0 = faster edges.
    double edge_speed_factor = 1.0;

    // Base fragment speed as fraction of original momentum. Lower = gentler drift apart.
    double base_speed_factor = 0.7;
};

} // namespace DirtSim
