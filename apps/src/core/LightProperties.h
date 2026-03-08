#pragma once

#include <cstdint>

namespace DirtSim {

/**
 * Light interaction properties for materials.
 * Used by the light calculator to compute illumination.
 */
struct LightProperties {
    float opacity = 0.0f;                 // Blocks direct light [0-1].
    float saturation = 1.0f;              // Material color strength [0-1]. Independent of opacity.
    float scatter = 0.0f;                 // Re-emits light to neighbors [0-1].
    float specularity = 0.0f;             // Scatter direction: 0=diffuse, 1=mirror [0-1].
    uint32_t tint = 0xFFFFFFFF;           // Color filter for transmitted light (RGBA).
    float emission = 0.0f;                // Self-illumination intensity [0-1].
    uint32_t emission_color = 0xFFFFFFFF; // Color of emitted light (RGBA).
};

} // namespace DirtSim
