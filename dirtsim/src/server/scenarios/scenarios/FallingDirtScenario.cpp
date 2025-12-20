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
    config_.drop_height = 20.0;
    config_.drop_rate = 5.0; // 5 particles per second.
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
    if (std::holds_alternative<FallingDirtConfig>(newConfig)) {
        config_ = std::get<FallingDirtConfig>(newConfig);
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
    for (uint32_t y = 0; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }

    // Configure physics.
    world.getPhysicsSettings().gravity = 9.81;
    world.setWallsEnabled(false);

    // Add floor.
    for (uint32_t x = 0; x < world.getData().width; ++x) {
        world.getData()
            .at(x, world.getData().height - 1)
            .replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Add some initial dirt piles to make it interesting.
    uint32_t width = world.getData().width;
    uint32_t height = world.getData().height;

    if (width >= 7 && height >= 7) {
        // Left mound.
        world.addMaterialAtCell(1, height - 2, MaterialType::DIRT, 1.0);
        world.addMaterialAtCell(2, height - 2, MaterialType::DIRT, 1.0);
        world.addMaterialAtCell(1, height - 3, MaterialType::DIRT, 0.5);

        // Right mound.
        world.addMaterialAtCell(width - 3, height - 2, MaterialType::DIRT, 1.0);
        world.addMaterialAtCell(width - 2, height - 2, MaterialType::DIRT, 1.0);
        world.addMaterialAtCell(width - 2, height - 3, MaterialType::DIRT, 0.5);
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
    const double drop_probability = config_.drop_rate * deltaTime;

    if (drop_dist_(rng_) < drop_probability) {
        std::uniform_int_distribution<uint32_t> x_dist(1, world.getData().width - 2);
        uint32_t x = x_dist(rng_);
        uint32_t y = 1; // Start near top.

        // Add dirt at the position.
        world.addMaterialAtCell(x, y, MaterialType::DIRT, 0.7);
    }
}

} // namespace DirtSim
