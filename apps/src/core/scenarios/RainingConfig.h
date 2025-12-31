#pragma once

#include <zpp_bits.h>

namespace DirtSim::Config {

struct Raining {
    using serialize = zpp::bits::members<3>;

    double rainRate = 5.0;
    double drainSize = 0.0;
    double maxFillPercent = 0.0;
};

} // namespace DirtSim::Config
