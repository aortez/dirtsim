#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Raining scenario config - continuous rain.
 */
struct RainingConfig {
    using serialize = zpp::bits::members<3>;

    double rain_rate = 5.0;        // Rain rate in drops per second.
    double drain_size = 0.0;       // Drain opening width in cells (0 = solid floor, no drain).
    double max_fill_percent = 0.0; // Evaporate when above this fill % (0 = disabled).
};

} // namespace DirtSim
