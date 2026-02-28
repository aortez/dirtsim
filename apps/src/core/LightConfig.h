#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace DirtSim {

struct LightConfig {
    float air_scatter_rate;
    uint32_t ambient_color;
    float ambient_intensity;
    int diffusion_iterations;
    float diffusion_rate;
    bool sky_access_enabled;
    float sky_access_falloff;
    bool sky_access_multi_directional;
    uint32_t sun_color;
    bool sun_enabled;
    float sun_intensity;
};

LightConfig getDefaultLightConfig();
void to_json(nlohmann::json& j, const LightConfig& config);
void from_json(const nlohmann::json& j, LightConfig& config);

} // namespace DirtSim
