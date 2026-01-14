#pragma once

#include <zpp_bits.h>

namespace DirtSim::Config {

struct Empty {
    using serialize = zpp::bits::members<0>;
};

} // namespace DirtSim::Config
