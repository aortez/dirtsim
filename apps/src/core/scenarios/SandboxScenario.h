#pragma once

#include "SandboxConfig.h"
#include "core/scenarios/Scenario.h"
#include <memory>
#include <random>

namespace DirtSim {

/**
 * Sandbox scenario - The default world setup without walls.
 */
class SandboxScenario : public ScenarioRunner {
public:
    SandboxScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    ScenarioMetadata metadata_;
    Config::Sandbox config_;

    // Timing state for particle generation.
    double lastSimTime_ = 0.0;
    double nextRightThrow_ = 1.0;

    // Water column auto-disable state.
    double waterColumnStartTime_ = -1.0;
    static constexpr double WATER_COLUMN_DURATION = 2.0;

    // Random number generator for rain drops.
    std::mt19937 rng_{ std::random_device{}() };

    // Helper methods.
    void addWaterColumn(World& world);
    void clearWaterColumn(World& world);
    void addDirtQuadrant(World& world);
    void clearDirtQuadrant(World& world);
    void refillWaterColumn(World& world);
    void addRainDrops(World& world, double deltaTime);
    void spawnWaterDrop(World& world, int centerX, int centerY, double radius, double fillAmount);
    void throwDirtBalls(World& world);
};

} // namespace DirtSim
