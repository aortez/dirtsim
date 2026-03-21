#pragma once

#include "ColorNames.h"
#include "GridBuffer.h"
#include "LightBuffer.h"
#include "LightCalculatorBase.h"
#include "Vector2.h"
#include <cstdint>

class Timers;

namespace DirtSim {

class GridOfCells;
class World;
struct LightConfig;
struct WorldData;

// 8 compass directions for light propagation.
enum class LightDir : uint8_t { N, NE, E, SE, S, SW, W, NW, COUNT };

// Upstream neighbor offset for each direction.
constexpr Vector2i upstream(LightDir d)
{
    constexpr Vector2i offsets[] = {
        { 0, 1 },   // N: light heading north came from south.
        { -1, 1 },  // NE: came from southwest.
        { -1, 0 },  // E: came from west.
        { -1, -1 }, // SE: came from northwest.
        { 0, -1 },  // S: came from north.
        { 1, -1 },  // SW: came from northeast.
        { 1, 0 },   // W: came from east.
        { 1, 1 },   // NW: came from southeast.
    };
    return offsets[static_cast<int>(d)];
}

// Opposite direction for specular reflection.
constexpr LightDir opposite(LightDir d)
{
    return static_cast<LightDir>((static_cast<int>(d) + 4) % 8);
}

// Weight for diagonal vs cardinal directions.
constexpr float dirWeight(LightDir d)
{
    constexpr float kDiag = 0.707f;
    return (static_cast<int>(d) % 2 == 0) ? 1.0f : kDiag;
}

// Per-cell directional light. 8 channels x RgbF = 96 bytes/cell.
struct DirectionalLight {
    ColorNames::RgbF channel[8] = {};

    ColorNames::RgbF total() const
    {
        ColorNames::RgbF sum{};
        for (int i = 0; i < 8; ++i) {
            sum.r += channel[i].r;
            sum.g += channel[i].g;
            sum.b += channel[i].b;
        }
        return sum;
    }
};

/**
 * Propagation-based light calculator.
 * Each cell stores light in 8 compass directions. Each step, light advances
 * one cell, interacting with materials. Sources inject at their positions.
 */
class LightPropagator : public LightCalculatorBase {
public:
    void calculate(
        World& world, const GridOfCells& grid, const LightConfig& config, Timers& timers) override;
    std::string lightMapString(const World& world) const override;
    const LightBuffer& getRawLightBuffer() const override;

    void setEmissive(int x, int y, uint32_t color, float intensity = 1.0f) override;
    void clearEmissive(int x, int y) override;
    void clearAllEmissive() override;
    void resize(int width, int height) override;
    void setAmbientBoost(ColorNames::RgbF boost) override;

private:
    void applyFlatBasic(WorldData& data);
    void clearPropagatedState();
    void ensureBufferSizes(int width, int height);
    void propagateStep(const WorldData& data, bool air_fast_path);
    void injectSources(World& world, const LightConfig& config);
    void applyAmbient(WorldData& data, const LightConfig& config);
    void storeRawLight(WorldData& data);

    GridBuffer<DirectionalLight> light_field_;
    GridBuffer<DirectionalLight> light_field_next_;
    GridBuffer<ColorNames::RgbF> emissive_overlay_;
    bool inFlatBasicMode_ = false;
    ColorNames::RgbF ambient_boost_{};
    LightBuffer raw_light_;
};

} // namespace DirtSim
