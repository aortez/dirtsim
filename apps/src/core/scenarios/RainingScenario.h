#pragma once

#include "RainingConfig.h"
#include "core/scenarios/Scenario.h"
#include <memory>
#include <random>

namespace DirtSim {

/**
 * Raining scenario - Rain falling from the sky.
 */
class RainingScenario : public ScenarioRunner {
public:
    RainingScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    Config::Raining config_;

    // Random number generation for rain drops.
    std::mt19937 rng_{ 42 }; // Deterministic seed for consistency.
    std::uniform_real_distribution<double> drop_dist_{ 0.0, 1.0 };
};

} // namespace DirtSim
