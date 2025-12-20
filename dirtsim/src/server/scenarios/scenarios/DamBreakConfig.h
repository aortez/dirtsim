#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Dam break scenario config - water behind barrier.
 */
struct DamBreakConfig {
    using serialize = zpp::bits::members<3>;

    double dam_height = 10.0;  // Height of dam wall.
    bool auto_release = false; // Automatically break dam after delay.
    double release_time = 2.0; // Time in seconds before auto-release.
};

} // namespace DirtSim
