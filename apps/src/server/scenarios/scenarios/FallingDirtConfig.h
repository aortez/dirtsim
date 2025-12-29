#pragma once

#include <zpp_bits.h>

namespace DirtSim::Config {

struct FallingDirt {
    using serialize = zpp::bits::members<2>;

    double dropHeight = 20.0;
    double dropRate = 2.0;
};

} // namespace DirtSim::Config
