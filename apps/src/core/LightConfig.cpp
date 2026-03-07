#include "LightConfig.h"

#include "ColorNames.h"
#include "ReflectSerializer.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const LightConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

void from_json(const nlohmann::json& j, LightConfig& config)
{
    config = ReflectSerializer::from_json<LightConfig>(j);
}

LightConfig getDefaultLightConfig()
{
    return LightConfig{
        .air_scatter_rate = 0.15f,
        .ambient_color = ColorNames::dayAmbient(),
        .ambient_intensity = 0.7f,
        .diagonal_light_enabled = true,
        .diagonal_light_intensity = 0.4f,
        .diffusion_iterations = 2,
        .diffusion_rate = 0.6f,
        .side_light_enabled = true,
        .side_light_intensity = 0.2f,
        .sun_color = ColorNames::warmSunlight(),
        .sun_enabled = true,
        .sun_intensity = 0.8f,
    };
}

} // namespace DirtSim
