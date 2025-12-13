#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Clock scenario config - displays system time using 7-segment digits.
 */
struct ClockConfig {
    using serialize = zpp::bits::members<2>;

    double horizontal_scale = 1.1; // World width = clock_width × scale.
    double vertical_scale = 2.0;   // World height = clock_height × scale.
};

} // namespace DirtSim
