#pragma once

#include <cstdint>
#include <zpp_bits.h>

namespace DirtSim::Config {

struct DuckTraining {
    uint32_t obstacleSeed = 1337;
    uint8_t obstacleCount = 3;

    using serialize = zpp::bits::members<2>;
};

} // namespace DirtSim::Config
