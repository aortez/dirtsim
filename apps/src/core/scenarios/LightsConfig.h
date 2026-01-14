#pragma once

#include <zpp_bits.h>

namespace DirtSim::Config {

struct Lights {
    using serialize = zpp::bits::members<0>;

    // No configurable parameters for this test scenario.
};

} // namespace DirtSim::Config
