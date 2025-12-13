#pragma once

#include <zpp_bits.h>

namespace DirtSim {

/**
 * @brief Empty scenario config - no configuration needed.
 */
struct EmptyConfig {
    using serialize = zpp::bits::members<0>;
};

} // namespace DirtSim
