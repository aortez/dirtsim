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
        .air_fast_path = true,
        .ambient_color = ColorNames::dayAmbient(),
        .ambient_intensity = 0.7f,
        .sky_color = ColorNames::skyBlue(),
        .sky_intensity = 0.4f,
        .steps_per_frame = 15,
        .sun_color = ColorNames::warmSunlight(),
        .sun_intensity = 0.8f,
        .temporal_decay = 0.85f,
        .temporal_persistence = false,
    };
}

} // namespace DirtSim
