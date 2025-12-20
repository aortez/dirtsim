#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Sandbox scenario config - interactive playground with configurable features.
 */
struct SandboxConfig {
    using serialize = zpp::bits::members<4>;

    // Initial setup features.
    bool quadrant_enabled = true; // Lower-right quadrant filled with dirt.

    // Continuous particle generation features.
    bool water_column_enabled = true; // Water column on left side (5 wide × 20 tall).
    bool right_throw_enabled = true;  // Periodic dirt throw from right side.
    double rain_rate = 0.0;           // Rain rate in drops per second (0 = disabled).
};

} // namespace DirtSim
