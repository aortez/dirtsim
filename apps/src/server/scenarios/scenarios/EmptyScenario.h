#pragma once

#include "EmptyConfig.h"
#include "server/scenarios/Scenario.h"
#include <memory>

namespace DirtSim {

/**
 * Empty scenario - A truly empty world with no particles.
 */
class EmptyScenario : public Scenario {
public:
    EmptyScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    EmptyConfig config_;
};

} // namespace DirtSim
