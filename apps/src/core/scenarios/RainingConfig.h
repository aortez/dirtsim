#pragma once

#include <nlohmann/json_fwd.hpp>
#include <zpp_bits.h>

namespace DirtSim::Config {

struct Raining {
    using serialize = zpp::bits::members<3>;

    double rainRate = 5.0;
    double drainSize = 0.0;
    double maxFillPercent = 0.0;
};

void from_json(const nlohmann::json& j, Raining& config);
void to_json(nlohmann::json& j, const Raining& config);

} // namespace DirtSim::Config
