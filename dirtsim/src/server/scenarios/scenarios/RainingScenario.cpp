#include "RainingScenario.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {

RainingScenario::RainingScenario()
{
    metadata_.name = "Raining";
    metadata_.description = "Rain falling from the sky";
    metadata_.category = "demo";

    // Initialize with default config.
    config_.rain_rate = 10.0;       // 10 drops per second.
    config_.drain_size = 0.0;       // No drain (solid floor).
    config_.max_fill_percent = 0.0; // No evaporation limit.
}

const ScenarioMetadata& RainingScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig RainingScenario::getConfig() const
{
    return config_;
}

void RainingScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    // Validate type and update.
    if (std::holds_alternative<RainingConfig>(newConfig)) {
        config_ = std::get<RainingConfig>(newConfig);
        spdlog::info("RainingScenario: Config updated");
    }
    else {
        spdlog::error("RainingScenario: Invalid config type provided");
    }
}

void RainingScenario::setup(World& world)
{
    spdlog::info("RainingScenario::setup - initializing world");

    // Clear world first.
    for (uint32_t y = 0; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }

    // Add a solid floor of walls.
    uint32_t bottomY = world.getData().height - 1;
    for (uint32_t x = 0; x < world.getData().width; ++x) {
        world.getData().at(x, bottomY).replaceMaterial(MaterialType::WALL, 1.0);
    }

    // Configure physics.
    world.setWallsEnabled(false);
    world.getPhysicsSettings().gravity = 9.81;

    spdlog::info("RainingScenario::setup complete");
}

void RainingScenario::reset(World& world)
{
    spdlog::info("RainingScenario::reset");
    setup(world);
}

void RainingScenario::tick(World& world, double deltaTime)
{
    // Uniform evaporation when over max fill threshold.
    if (config_.max_fill_percent > 0.0) {
        // Calculate total fill percentage.
        double totalFill = 0.0;
        uint32_t totalCells = world.getData().width * world.getData().height;

        for (uint32_t y = 0; y < world.getData().height; ++y) {
            for (uint32_t x = 0; x < world.getData().width; ++x) {
                const Cell& cell = world.getData().at(x, y);
                if (cell.material_type != MaterialType::AIR) {
                    totalFill += cell.fill_ratio;
                }
            }
        }

        double fillPercent = (totalFill / totalCells) * 100.0;

        // Gradually evaporate when over threshold.
        if (fillPercent > config_.max_fill_percent) {
            // Evaporation rate scales with how far over the limit we are.
            double overage = fillPercent - config_.max_fill_percent;
            double evaporationRate = 0.01 + (overage * 0.005); // Base rate + proportional.

            for (uint32_t y = 0; y < world.getData().height; ++y) {
                for (uint32_t x = 0; x < world.getData().width; ++x) {
                    Cell& cell = world.getData().at(x, y);
                    if (cell.material_type == MaterialType::WATER) {
                        cell.fill_ratio -= evaporationRate * deltaTime;
                        if (cell.fill_ratio < 0.01) {
                            cell.replaceMaterial(MaterialType::AIR, 0.0);
                        }
                    }
                }
            }
        }
    }

    // Add rain drops based on configured rain rate.
    const double drop_probability = config_.rain_rate * deltaTime;

    if (drop_dist_(rng_) < drop_probability) {
        std::uniform_int_distribution<uint32_t> x_dist(1, world.getData().width - 2);
        uint32_t x = x_dist(rng_);
        uint32_t y = 1; // Start near top.

        // Add water at the position.
        world.addMaterialAtCell(x, y, MaterialType::WATER, 0.5);
    }

    // Manage drain opening in the floor and evaporate water in the drain.
    uint32_t bottomY = world.getData().height - 1;
    uint32_t centerX = world.getData().width / 2;
    uint32_t drainSize = static_cast<uint32_t>(config_.drain_size);
    uint32_t halfDrain = drainSize / 2;

    // Calculate drain boundaries (centered).
    uint32_t drainStart = (centerX > halfDrain) ? centerX - halfDrain : 0;
    uint32_t drainEnd = std::min(centerX + halfDrain, world.getData().width - 1);

    for (uint32_t x = 0; x < world.getData().width; ++x) {
        Cell& cell = world.getData().at(x, bottomY);
        bool inDrain = (x >= drainStart && x <= drainEnd && drainSize > 0);

        if (inDrain) {
            // Inside drain area - remove walls, evaporate water.
            if (cell.material_type == MaterialType::WALL) {
                cell.replaceMaterial(MaterialType::AIR, 0.0);
            }
            else if (cell.material_type == MaterialType::WATER) {
                // Evaporate water at 10% per tick.
                cell.fill_ratio -= 0.1;
                if (cell.fill_ratio < 0.01) {
                    cell.replaceMaterial(MaterialType::AIR, 0.0);
                }
            }
        }
        else {
            // Outside drain area - ensure floor is walls.
            if (cell.material_type != MaterialType::WALL) {
                cell.replaceMaterial(MaterialType::WALL, 1.0);
            }
        }
    }
}

} // namespace DirtSim
