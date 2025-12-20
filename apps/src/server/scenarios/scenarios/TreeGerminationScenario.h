#pragma once

#include "EmptyConfig.h"
#include "server/scenarios/Scenario.h"
#include <memory>

namespace DirtSim {

/**
 * Tree Germination scenario - 9x9 world with seed growing into balanced tree.
 */
class TreeGerminationScenario : public Scenario {
public:
    TreeGerminationScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    EmptyConfig config_; // No configuration needed.
};

} // namespace DirtSim
