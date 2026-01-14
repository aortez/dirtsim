#pragma once

#include "core/organisms/OrganismType.h"
#include "core/scenarios/Scenario.h"

namespace DirtSim {

/**
 * Goose Test scenario - simple world with a goose to test rigid body physics.
 */
class GooseTestScenario : public ScenarioRunner {
public:
    GooseTestScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    OrganismId goose_id_{};
};

} // namespace DirtSim
