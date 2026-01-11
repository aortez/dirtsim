#pragma once

#include "Vector2d.h"
#include <cstdint>

namespace DirtSim {

/**
 * Localized light source with position, color, intensity, and falloff.
 *
 * Point lights can be explicit objects added to the world, or implicit
 * from emissive materials. Light intensity falls off with distance squared.
 */
struct PointLight {
    Vector2d position;
    uint32_t color = 0xFFFFFFFF;
    float intensity = 1.0f;
    float radius = 20.0f;
    float attenuation = 0.1f;
};

} // namespace DirtSim
