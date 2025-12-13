#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "server/scenarios/Scenario.h"
#include "server/scenarios/ScenarioRegistry.h"
#include "spdlog/spdlog.h"
#include <random>

using namespace DirtSim;

/**
 * Raining scenario - Rain falling from the sky.
 */
class RainingScenario : public Scenario {
public:
    RainingScenario()
    {
        metadata_.name = "Raining";
        metadata_.description = "Rain falling from the sky";
        metadata_.category = "demo";

        // Initialize with default config.
        config_.rain_rate = 10.0;       // 10 drops per second.
        config_.drain_rate = 0.0;       // No drainage (water puddles).
        config_.max_fill_percent = 0.0; // No auto-clear.
    }

    const ScenarioMetadata& getMetadata() const override { return metadata_; }

    ScenarioConfig getConfig() const override { return config_; }

    void setConfig(const ScenarioConfig& newConfig, World& /*world*/) override
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

    void setup(World& world) override
    {
        spdlog::info("RainingScenario::setup - initializing world");

        // Clear world first.
        for (uint32_t y = 0; y < world.getData().height; ++y) {
            for (uint32_t x = 0; x < world.getData().width; ++x) {
                world.getData().at(x, y) = Cell(); // Reset to empty cell.
            }
        }

        // Configure physics.
        world.setWallsEnabled(false);
        world.getPhysicsSettings().gravity = 9.81;

        spdlog::info("RainingScenario::setup complete");
    }

    void reset(World& world) override
    {
        spdlog::info("RainingScenario::reset");
        setup(world);
    }

    void tick(World& world, double deltaTime) override
    {
        // Check max fill and clear world if exceeded.
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

            // Clear world if fill exceeds threshold.
            if (fillPercent > config_.max_fill_percent) {
                spdlog::info(
                    "RainingScenario: Fill {:.1f}% exceeds max {:.1f}% - clearing world",
                    fillPercent,
                    config_.max_fill_percent);

                for (uint32_t y = 0; y < world.getData().height; ++y) {
                    for (uint32_t x = 0; x < world.getData().width; ++x) {
                        Cell& cell = world.getData().at(x, y);
                        if (cell.material_type != MaterialType::AIR) {
                            cell.replaceMaterial(MaterialType::AIR, 0.0);
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

        // Drain particles from bottom row based on drain_rate.
        if (config_.drain_rate > 0.0) {
            uint32_t bottomY = world.getData().height - 1;

            // Normalize drain_rate from [0, 100] to [0, 1].
            double normalizedDrainRate = config_.drain_rate / 100.0;

            // Scale factor controls drainage speed (tunable).
            const double drainScale = 2.0;
            double drainAmount = normalizedDrainRate * drainScale * deltaTime;

            for (uint32_t x = 0; x < world.getData().width; ++x) {
                Cell& cell = world.getData().at(x, bottomY);
                if (cell.material_type != MaterialType::AIR) {
                    // Reduce fill ratio gradually.
                    cell.fill_ratio = std::max(0.0, cell.fill_ratio - drainAmount);

                    // Remove cell if nearly empty.
                    if (cell.fill_ratio < 0.01) {
                        cell.replaceMaterial(MaterialType::AIR, 0.0);
                    }
                }
            }
        }
    }

private:
    ScenarioMetadata metadata_;
    RainingConfig config_;

    // Random number generation for rain drops.
    std::mt19937 rng_{ 42 }; // Deterministic seed for consistency
    std::uniform_real_distribution<double> drop_dist_{ 0.0, 1.0 };
};
