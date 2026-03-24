#pragma once

#include "core/ScenarioConfig.h"
#include "core/ScenarioMetadata.h"
#include "core/Vector2.h"
#include <cstdint>
#include <memory>
#include <string>

namespace DirtSim {

class World;

class ScenarioRunner {
public:
    virtual ~ScenarioRunner() = default;

    virtual const ScenarioMetadata& getMetadata() const = 0;
    virtual ScenarioConfig getConfig() const = 0;
    virtual ScenarioConfig resolveInitialConfig(
        const ScenarioConfig& config, const Vector2s& containerSize) const
    {
        (void)containerSize;
        return config;
    }
    virtual Vector2i resolveInitialWorldSize(
        const ScenarioConfig& config, const Vector2i& defaultWorldSize) const
    {
        (void)config;
        const ScenarioMetadata& metadata = getMetadata();
        if (metadata.requiredWidth > 0 && metadata.requiredHeight > 0) {
            return {
                static_cast<int>(metadata.requiredWidth),
                static_cast<int>(metadata.requiredHeight),
            };
        }
        return defaultWorldSize;
    }
    virtual void setConfig(const ScenarioConfig& config, World& world) = 0;
    virtual void setup(World& world) = 0;
    virtual void reset(World& world) = 0;
    // Runs at the start of each frame on the committed world state from the end of the previous
    // frame.
    virtual void tick(World& world, double deltaTime) = 0;
};

} // namespace DirtSim
