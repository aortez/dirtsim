#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Water equalization scenario config - pressure equilibration test.
 */
struct WaterEqualizationConfig {
    using serialize = zpp::bits::members<3>;

    double left_height = 15.0;     // Water column height on left.
    double right_height = 5.0;     // Water column height on right.
    bool separator_enabled = true; // Start with separator wall.
};

} // namespace DirtSim
