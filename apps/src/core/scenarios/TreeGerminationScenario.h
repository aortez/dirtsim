#pragma once

#include "TreeGerminationConfig.h"
#include "core/organisms/OrganismType.h"
#include "core/scenarios/Scenario.h"

namespace DirtSim {

/**
 * Tree Germination scenario - 9x9 world with seed growing into balanced tree.
 */
class TreeGerminationScenario : public ScenarioRunner {
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
    Config::TreeGermination config_;
    OrganismId treeId_{};
};

} // namespace DirtSim
