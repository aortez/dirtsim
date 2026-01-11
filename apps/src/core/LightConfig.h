#pragma once

#include "ReflectSerializer.h"
#include <cstdint>
#include <nlohmann/json.hpp>

namespace DirtSim {

struct LightConfig {
    uint32_t ambient_color;
    float ambient_intensity;
    int diffusion_iterations;
    float diffusion_rate;
    bool sky_access_enabled;
    float sky_access_falloff;
    uint32_t sun_color;
    bool sun_enabled;
    float sun_intensity;
};

LightConfig getDefaultLightConfig();

inline void to_json(nlohmann::json& j, const LightConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

inline void from_json(const nlohmann::json& j, LightConfig& config)
{
    config = ReflectSerializer::from_json<LightConfig>(j);
}

} // namespace DirtSim
