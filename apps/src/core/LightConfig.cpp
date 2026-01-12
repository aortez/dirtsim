#include "LightConfig.h"
#include "ColorNames.h"

namespace DirtSim {

LightConfig getDefaultLightConfig()
{
    return LightConfig{
        .air_scatter_rate = 0.15f,
        .ambient_color = ColorNames::dayAmbient(),
        .ambient_intensity = 0.5f,
        .diffusion_iterations = 2,
        .diffusion_rate = 0.6f,
        .sky_access_enabled = true,
        .sky_access_falloff = 0.03f,
        .sun_color = ColorNames::warmSunlight(),
        .sun_enabled = true,
        .sun_intensity = 0.8f,
    };
}

} // namespace DirtSim
