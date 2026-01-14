#pragma once

#include "BenchmarkConfig.h"
#include "core/MaterialType.h"
#include "core/scenarios/Scenario.h"
#include <memory>

namespace DirtSim {

/**
 * Benchmark scenario - Performance testing with complex physics.
 * 200x200 world with water pool and falling metal/wood balls.
 */
class BenchmarkScenario : public ScenarioRunner {
public:
    BenchmarkScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    Config::Benchmark config_;

    void addBall(
        World& world,
        uint32_t centerX,
        uint32_t centerY,
        uint32_t radius,
        Material::EnumType material);
};

} // namespace DirtSim
