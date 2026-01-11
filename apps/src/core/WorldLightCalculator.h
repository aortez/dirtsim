#pragma once

#include "LightBuffer.h"
#include "LightConfig.h"
#include "WorldCalculatorBase.h"
#include <cstdint>
#include <string>
#include <vector>

namespace DirtSim {

class World;

/**
 * Calculates illumination across the world grid from multiple light sources.
 *
 * Light sources include:
 * - Ambient light (baseline illumination everywhere)
 * - Directional sunlight (top-down with opacity/tinting)
 * - Point lights (localized sources with falloff)
 * - Emissive materials (cells with emission > 0)
 *
 * Results are written to each cell's color_ field as packed RGBA.
 */
class WorldLightCalculator : public WorldCalculatorBase {
public:
    WorldLightCalculator() = default;

    /**
     * Calculate lighting for the entire world.
     * Writes final lit color to each cell's color_ field.
     */
    void calculate(World& world, const LightConfig& config);

    std::string lightMapString(const World& world) const;

    const LightBuffer& getRawLightBuffer() const;

private:
    void clearLight(World& world);
    void applyAmbient(World& world, const LightConfig& config);
    void applySunlight(World& world, uint32_t sun_color, float intensity);
    void applyEmissiveCells(World& world);
    void applyDiffusion(World& world, int iterations, float rate);
    void applyMaterialColors(World& world);
    void storeRawLight(World& world);

    std::vector<uint32_t> light_buffer_;
    LightBuffer raw_light_;
};

} // namespace DirtSim
