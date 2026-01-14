#pragma once

#include <zpp_bits.h>

namespace DirtSim::Config {

struct Sandbox {
    using serialize = zpp::bits::members<4>;

    bool quadrantEnabled = true;
    bool waterColumnEnabled = true;
    bool rightThrowEnabled = true;
    double rainRate = 0.0;
};

} // namespace DirtSim::Config
