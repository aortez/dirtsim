#pragma once

#include "DuckTrainingConfig.h"
#include "clock_scenario/ObstacleManager.h"
#include "core/ScenarioConfig.h"
#include "core/scenarios/Scenario.h"

#include <random>
#include <vector>

namespace DirtSim {

class World;
struct WorldData;

class DuckTrainingScenario : public ScenarioRunner {
public:
    DuckTrainingScenario();

    const ScenarioMetadata& getMetadata() const override;
    ScenarioConfig getConfig() const override;
    void setConfig(const ScenarioConfig& newConfig, World& world) override;
    void setup(World& world) override;
    void reset(World& world) override;
    void tick(World& world, double deltaTime) override;

private:
    struct WallSpec {
        int16_t x = 0;
        int16_t y = 0;
        Material::EnumType renderAs = Material::EnumType::Wall;
    };

    ScenarioMetadata metadata_;
    Config::DuckTraining config_;
    ObstacleManager obstacleManager_;

    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniformDist_{ 0.0, 1.0 };

    void rebuildWorld(World& world);
    void spawnObstacles(World& world);

    std::vector<WallSpec> generateWallSpecs(const WorldData& data) const;
    void applyWalls(World& world, const std::vector<WallSpec>& walls);
    void redrawWalls(World& world);
};

} // namespace DirtSim
