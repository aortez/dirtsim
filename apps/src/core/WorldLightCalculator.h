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
struct SpotLight;
struct RotatingLight;
template <typename T>
struct Vector2;
using Vector2d = Vector2<double>;
using Vector2f = Vector2<float>;
class World;
struct WorldData;

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
    void setEmissive(int x, int y, uint32_t color, float intensity = 1.0f);
    void clearEmissive(int x, int y);
    void clearAllEmissive();
    void resize(int width, int height);
    void setAmbientBoost(ColorNames::RgbF boost);

private:
    void applyAmbient(World& world, const GridOfCells& grid, const LightConfig& config);
    void applyDiffusion(
        World& world, const GridOfCells& grid, int iterations, float rate, float air_scatter_rate);
    void applyEmissiveCells(World& world);
    void applyEmissiveOverlay(World& world);
    void applyMaterialColors(World& world);
    void applyPointLight(const PointLight& light, World& world, const GridOfCells& grid);
    void applyPointLights(World& world, const GridOfCells& grid);
    void applyRotatingLight(const RotatingLight& light, World& world, const GridOfCells& grid);
    void applySpotLight(const SpotLight& light, World& world, const GridOfCells& grid);
    void applySunlight(World& world, const GridOfCells& grid, uint32_t sun_color, float intensity);
    void clearLight(World& world);
    float getSpotAngularFactor(
        const Vector2f& light_pos,
        float direction,
        float arc_width,
        float focus,
        const Vector2f& target_pos) const;
    void storeRawLight(World& world);
    ColorNames::RgbF traceRay(
        const GridOfCells& grid,
        const WorldData& data,
        float x0,
        float y0,
        int x1,
        int y1,
        ColorNames::RgbF color) const;

    ColorNames::RgbF ambient_boost_{};
    GridBuffer<ColorNames::RgbF> emissive_overlay_;
    std::vector<ColorNames::RgbF> light_buffer_;
    LightBuffer raw_light_;
};

} // namespace DirtSim
