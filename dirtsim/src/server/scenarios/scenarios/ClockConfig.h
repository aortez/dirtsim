#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Clock scenario config - displays system time using 7-segment digits.
 *
 * World size is computed from clock dimensions × scale factors.
 * Clock dimensions: 40 cells wide × 7 cells tall (HH:MM:SS with 5×7 digits).
 */
struct ClockConfig {
    using serialize = zpp::bits::members<4>;

    double horizontal_scale = 1.1; // World width = clock_width × scale.
    double vertical_scale = 2.0;   // World height = clock_height × scale.
    uint8_t timezone_index = 0;    // Index into TIMEZONES array (0 = Local).
    bool show_seconds = true;      // Show seconds (HH:MM:SS vs HH:MM).
};

} // namespace DirtSim
