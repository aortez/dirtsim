#pragma once

#include <zpp_bits.h>

namespace DirtSim::Config {

struct WaterEqualization {
    using serialize = zpp::bits::members<3>;

    double leftHeight = 15.0;
    double rightHeight = 5.0;
    bool separatorEnabled = true;
};

} // namespace DirtSim::Config
