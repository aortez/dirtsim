#pragma once

#include "DamBreakConfig.h"
#include "core/scenarios/Scenario.h"
#include <memory>

namespace DirtSim {

/**
 * Dam Break scenario - Classic fluid dynamics demonstration.
 * Water held by a wall dam that breaks after pressure builds up.
 */
class DamBreakScenario : public ScenarioRunner {
public:
    DamBreakScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    Config::DamBreak config_;

    // Scenario state.
    bool damBroken_ = false;
    double elapsedTime_ = 0.0;
};

} // namespace DirtSim
