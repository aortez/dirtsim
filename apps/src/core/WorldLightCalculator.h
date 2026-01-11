#pragma once

#include "ColorNames.h"
#include "GridBuffer.h"
#include "LightBuffer.h"
#include "LightConfig.h"
#include "WorldCalculatorBase.h"
#include <cstdint>
#include <string>
#include <vector>

class Timers;

namespace DirtSim {

class GridOfCells;
struct PointLight;
class World;

/**
 * Calculates illumination across the world grid from multiple light sources:
 * ambient, directional sunlight, emissive materials, and an emissive overlay.
 * Scenarios can use the overlay to make specific cells glow (e.g., clock digits).
 */
class WorldLightCalculator : public WorldCalculatorBase {
public:
    WorldLightCalculator() = default;

    void calculate(
        World& world, const GridOfCells& grid, const LightConfig& config, Timers& timers);
    std::string lightMapString(const World& world) const;
    const LightBuffer& getRawLightBuffer() const;

    // Emissive overlay for scenario-controlled per-cell emission.
    void setEmissive(uint32_t x, uint32_t y, uint32_t color, float intensity = 1.0f);
    void clearEmissive(uint32_t x, uint32_t y);
    void clearAllEmissive();
    void resize(uint32_t width, uint32_t height);

private:
    void applyAmbient(World& world, const GridOfCells& grid, const LightConfig& config);
    void applyDiffusion(World& world, const GridOfCells& grid, int iterations, float rate);
    void applyEmissiveCells(World& world);
    void applyEmissiveOverlay(World& world);
    void applyMaterialColors(World& world);
    void applyPointLights(World& world, const GridOfCells& grid);
    void applySunlight(World& world, const GridOfCells& grid, uint32_t sun_color, float intensity);
    void clearLight(World& world);
    void storeRawLight(World& world);
    ColorNames::RgbF traceRay(
        const GridOfCells& grid, int x0, int y0, int x1, int y1, ColorNames::RgbF color) const;

    GridBuffer<ColorNames::RgbF> emissive_overlay_;
    std::vector<ColorNames::RgbF> light_buffer_;
    LightBuffer raw_light_;
};

} // namespace DirtSim
