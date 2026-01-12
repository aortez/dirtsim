#pragma once

#include "core/ScenarioConfig.h"
#include <cstdint>
#include <memory>
#include <string>

namespace DirtSim {

class World;

struct ScenarioMetadata {
    std::string name;
    std::string description;
    std::string category;
    uint32_t requiredWidth = 0;
    uint32_t requiredHeight = 0;
};

class ScenarioRunner {
public:
    virtual ~ScenarioRunner() = default;

    virtual const ScenarioMetadata& getMetadata() const = 0;
    virtual ScenarioConfig getConfig() const = 0;
    virtual void setConfig(const ScenarioConfig& config, World& world) = 0;
    virtual void setup(World& world) = 0;
    virtual void reset(World& world) = 0;
    virtual void tick(World& world, double deltaTime) = 0;
};

} // namespace DirtSim
