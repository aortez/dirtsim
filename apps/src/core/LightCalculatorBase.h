#pragma once

#include "ColorNames.h"
#include "WorldCalculatorBase.h"
#include <cstdint>
#include <string>

class Timers;

namespace DirtSim {

class GridOfCells;
class World;
struct LightBuffer;
struct LightConfig;

/**
 * Abstract interface for light calculators.
 * Allows swapping between WorldLightCalculator and LightPropagator.
 */
class LightCalculatorBase : public WorldCalculatorBase {
public:
    ~LightCalculatorBase() override = default;

    virtual void calculate(
        World& world, const GridOfCells& grid, const LightConfig& config, Timers& timers) = 0;
    virtual std::string lightMapString(const World& world) const = 0;
    virtual const LightBuffer& getRawLightBuffer() const = 0;

    virtual void setEmissive(int x, int y, uint32_t color, float intensity = 1.0f) = 0;
    virtual void clearEmissive(int x, int y) = 0;
    virtual void clearAllEmissive() = 0;
    virtual void resize(int width, int height) = 0;
    virtual void setAmbientBoost(ColorNames::RgbF boost) = 0;
};

} // namespace DirtSim
