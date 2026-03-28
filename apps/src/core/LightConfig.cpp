#include "LightConfig.h"

#include "Assert.h"
#include "ColorNames.h"
#include "ReflectSerializer.h"

namespace DirtSim {

void applyLightModePreset(LightConfig& config, LightMode mode)
{
    config.mode = mode;

    switch (mode) {
        case LightMode::Propagated:
            config.air_fast_path = false;
            config.steps_per_frame = 15;
            config.temporal_decay = 0.99f;
            config.temporal_persistence = false;
            return;
        case LightMode::Fast:
            config.air_fast_path = true;
            config.steps_per_frame = 1;
            config.temporal_decay = 0.995f;
            config.temporal_persistence = true;
            return;
        case LightMode::FlatBasic:
            return;
    }

    DIRTSIM_ASSERT(false, "Unhandled light mode");
}

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
        .mode = LightMode::Propagated,
        .air_fast_path = false,
        .ambient_color = ColorNames::dayAmbient(),
        .ambient_intensity = 0.7f,
        .sky_color = ColorNames::skyBlue(),
        .sky_intensity = 0.4f,
        .local_light_indirect_scale = 1.0f,
        .steps_per_frame = 15,
        .sun_color = ColorNames::warmSunlight(),
        .sun_intensity = 0.8f,
        .temporal_decay = 0.99f,
        .temporal_persistence = false,
    };
}

} // namespace DirtSim
