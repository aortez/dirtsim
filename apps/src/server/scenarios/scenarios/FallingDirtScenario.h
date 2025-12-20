#pragma once

#include "FallingDirtConfig.h"
#include "server/scenarios/Scenario.h"
#include <memory>
#include <random>

namespace DirtSim {

/**
 * Falling Dirt scenario - Dirt particles falling and accumulating.
 */
class FallingDirtScenario : public Scenario {
public:
    FallingDirtScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    FallingDirtConfig config_;

    // Random number generation for dirt drops.
    std::mt19937 rng_{ 123 }; // Different seed than rain.
    std::uniform_real_distribution<double> drop_dist_{ 0.0, 1.0 };
};

} // namespace DirtSim
