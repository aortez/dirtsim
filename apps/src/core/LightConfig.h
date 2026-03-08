#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace DirtSim {

struct LightConfig {
    bool air_fast_path;
    uint32_t ambient_color;
    float ambient_intensity;
    uint32_t sky_color;
    float sky_intensity;
    int steps_per_frame;
    uint32_t sun_color;
    float sun_intensity;
    float temporal_decay;
    bool temporal_persistence;
};

LightConfig getDefaultLightConfig();
void to_json(nlohmann::json& j, const LightConfig& config);
void from_json(const nlohmann::json& j, LightConfig& config);

} // namespace DirtSim
