#include "LightConfig.h"
#include "ColorNames.h"

namespace DirtSim {

LightConfig getDefaultLightConfig()
{
    return LightConfig{
        .ambient_color = ColorNames::dayAmbient(),
        .ambient_intensity = 1.0f,
        .diffusion_iterations = 2,
        .diffusion_rate = 0.3f,
        .sky_access_enabled = true,
        .sky_access_falloff = 1.0f,
        .sun_color = ColorNames::warmSunlight(),
        .sun_enabled = true,
        .sun_intensity = 1.0f,
    };
}

} // namespace DirtSim
