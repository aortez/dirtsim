#pragma once

#include <nlohmann/json_fwd.hpp>
#include <zpp_bits.h>

namespace DirtSim::Config {

struct Sandbox {
    using serialize = zpp::bits::members<4>;

    bool quadrantEnabled = true;
    bool waterColumnEnabled = true;
    bool rightThrowEnabled = true;
    double rainRate = 0.0;
};

void from_json(const nlohmann::json& j, Sandbox& config);
void to_json(nlohmann::json& j, const Sandbox& config);

} // namespace DirtSim::Config
