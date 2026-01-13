#include "FallingDirtScenario.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {

FallingDirtScenario::FallingDirtScenario()
{
    metadata_.name = "Falling Dirt";
    metadata_.description = "Dirt particles falling from the sky and accumulating";
    metadata_.category = "demo";

    // Initialize with default config.
    config_.dropHeight = 20.0;
    config_.dropRate = 5.0; // 5 particles per second.
}

const ScenarioMetadata& FallingDirtScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig FallingDirtScenario::getConfig() const
{
    return config_;
}

void FallingDirtScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    // Validate type and update.
    if (std::holds_alternative<Config::FallingDirt>(newConfig)) {
        config_ = std::get<Config::FallingDirt>(newConfig);
        spdlog::info("FallingDirtScenario: Config updated");
    }
    else {
        spdlog::error("FallingDirtScenario: Invalid config type provided");
    }
}

void FallingDirtScenario::setup(World& world)
{
    spdlog::info("FallingDirtScenario::setup - initializing world");

    // Clear world first.
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }

    // Configure physics.
    world.getPhysicsSettings().gravity = 9.81;
    world.setWallsEnabled(false);

    // Add floor.
    for (int x = 0; x < world.getData().width; ++x) {
        world.getData()
            .at(x, world.getData().height - 1)
            .replaceMaterial(Material::EnumType::WALL, 1.0);
    }

    // Add some initial dirt piles to make it interesting.
    int width = world.getData().width;
    int height = world.getData().height;

    if (width >= 7 && height >= 7) {
        // Left mound.
        world.addMaterialAtCell(1, height - 2, Material::EnumType::DIRT, 1.0);
        world.addMaterialAtCell(2, height - 2, Material::EnumType::DIRT, 1.0);
        world.addMaterialAtCell(1, height - 3, Material::EnumType::DIRT, 0.5);

        // Right mound.
        world.addMaterialAtCell(width - 3, height - 2, Material::EnumType::DIRT, 1.0);
        world.addMaterialAtCell(width - 2, height - 2, Material::EnumType::DIRT, 1.0);
        world.addMaterialAtCell(width - 2, height - 3, Material::EnumType::DIRT, 0.5);
    }

    spdlog::info("FallingDirtScenario::setup complete");
}

void FallingDirtScenario::reset(World& world)
{
    spdlog::info("FallingDirtScenario::reset");
    setup(world);
}

void FallingDirtScenario::tick(World& world, double deltaTime)
{
    // Drop dirt particles based on configured rate.
    const double drop_probability = config_.dropRate * deltaTime;

    if (drop_dist_(rng_) < drop_probability) {
        std::uniform_int_distribution<int> x_dist(1, world.getData().width - 2);
        int x = x_dist(rng_);
        int y = 1; // Start near top.

        // Add dirt at the position.
        world.addMaterialAtCell(x, y, Material::EnumType::DIRT, 0.7);
    }
}

} // namespace DirtSim
