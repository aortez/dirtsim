#pragma once

#include <zpp_bits.h>

namespace DirtSim::Config {

struct DamBreak {
    using serialize = zpp::bits::members<3>;

    double damHeight = 10.0;
    bool autoRelease = false;
    double releaseTime = 2.0;
};

} // namespace DirtSim::Config
