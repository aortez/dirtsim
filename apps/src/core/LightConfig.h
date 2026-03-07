#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>

namespace DirtSim {

struct LightConfig {
    float air_scatter_rate;
    uint32_t ambient_color;
    float ambient_intensity;
    bool diagonal_light_enabled;
    float diagonal_light_intensity;
    int diffusion_iterations;
    float diffusion_rate;
    bool side_light_enabled;
    float side_light_intensity;
    uint32_t sun_color;
    bool sun_enabled;
    float sun_intensity;
};

LightConfig getDefaultLightConfig();
void to_json(nlohmann::json& j, const LightConfig& config);
void from_json(const nlohmann::json& j, LightConfig& config);

} // namespace DirtSim
