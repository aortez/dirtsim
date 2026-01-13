#pragma once

#include "LightsConfig.h"
#include "core/scenarios/Scenario.h"

namespace DirtSim {

/**
 * Lights scenario - Test lighting system with water, dirt, and leaf materials.
 */
class LightsScenario : public ScenarioRunner {
public:
    LightsScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    Config::Lights config_;
};

} // namespace DirtSim
