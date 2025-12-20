#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Falling dirt scenario config - gravity and pile formation.
 */
struct FallingDirtConfig {
    using serialize = zpp::bits::members<2>;

    double drop_height = 20.0; // Height from which dirt drops.
    double drop_rate = 2.0;    // Drop rate in particles per second.
};

} // namespace DirtSim
