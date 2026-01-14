#pragma once

#include "WaterEqualizationConfig.h"
#include "core/scenarios/Scenario.h"
#include <memory>

namespace DirtSim {

/**
 * Water Equalization scenario - Demonstrates hydrostatic pressure and flow.
 * Water flows through a small opening at the bottom to achieve equilibrium between two columns.
 */
class WaterEqualizationScenario : public ScenarioRunner {
public:
    WaterEqualizationScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    Config::WaterEqualization config_;
};

} // namespace DirtSim
