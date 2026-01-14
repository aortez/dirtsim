#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <zpp_bits.h>

namespace DirtSim {

struct GlowConfig {
    uint32_t digitColor = 0;
    float digitIntensity = 0.0f;
    float floorIntensity = 0.0f;
    float obstacleIntensity = 0.0f;
    float wallIntensity = 0.0f;
    float waterIntensity = 0.0f;

    using serialize = zpp::bits::members<6>;
};

void to_json(nlohmann::json& j, const GlowConfig& config);
void from_json(const nlohmann::json& j, GlowConfig& config);

} // namespace DirtSim
