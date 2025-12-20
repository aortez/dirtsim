#include "WaterEqualizationScenario.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {

WaterEqualizationScenario::WaterEqualizationScenario()
{
    metadata_.name = "Water Equalization";
    metadata_.description = "Water flows through bottom opening to equalize between columns";
    metadata_.category = "demo";
    metadata_.requiredWidth = 3;  // Match test specifications.
    metadata_.requiredHeight = 6; // Match test specifications.

    // Initialize with default config.
    config_.left_height = 15.0;
    config_.right_height = 5.0;
    config_.separator_enabled = true;
}

const ScenarioMetadata& WaterEqualizationScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig WaterEqualizationScenario::getConfig() const
{
    return config_;
}

void WaterEqualizationScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    // Validate type and update.
    if (std::holds_alternative<WaterEqualizationConfig>(newConfig)) {
        config_ = std::get<WaterEqualizationConfig>(newConfig);
        spdlog::info("WaterEqualizationScenario: Config updated");
    }
    else {
        spdlog::error("WaterEqualizationScenario: Invalid config type provided");
    }
}

void WaterEqualizationScenario::setup(World& world)
{
    spdlog::info("WaterEqualizationScenario::setup - initializing world");

    // Clear world first.
    for (uint32_t y = 0; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }

    // Setup world geometry only - preserve user's physics settings.
    // 3x6 world with water on left, wall separator in middle, air on right.
    // Left column (x=0): fill with water.
    for (uint32_t y = 0; y < 6; y++) {
        world.addMaterialAtCell(0, y, MaterialType::WATER, 1.0);
    }

    // Middle column (x=1): wall barrier with bottom cell open for flow.
    for (uint32_t y = 0; y < 5; y++) { // Only y=0 to y=4 (leave y=5 open).
        world.addMaterialAtCell(1, y, MaterialType::WALL, 1.0);
    }
    // Bottom cell at (1, 5) is left empty for water to flow through.

    // Right column (x=2): empty (air) - no need to explicitly set.

    spdlog::info(
        "WaterEqualizationScenario::setup complete - water at x=0, wall at x=1 (y=0-4), bottom "
        "open at (1,5)");
}

void WaterEqualizationScenario::reset(World& world)
{
    spdlog::info("WaterEqualizationScenario::reset - resetting world");
    setup(world);
}

void WaterEqualizationScenario::tick(World& /*world*/, double /*deltaTime*/)
{
    // No dynamic particle generation needed.
    // Water equalization happens automatically through physics.
}

} // namespace DirtSim
