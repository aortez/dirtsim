#include "DamBreakScenario.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"

namespace DirtSim {

DamBreakScenario::DamBreakScenario()
{
    metadata_.name = "Dam Break";
    metadata_.description = "Water column held by wall dam that breaks at timestep 30";
    metadata_.category = "demo";
    metadata_.requiredWidth = 6;  // Match test specifications.
    metadata_.requiredHeight = 6; // Match test specifications.

    // Initialize with default config.
    config_.damHeight = 10.0;
    config_.autoRelease = true;
    config_.releaseTime = 2.0; // 2 seconds ~= timestep 30 at 60fps.
}

const ScenarioMetadata& DamBreakScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig DamBreakScenario::getConfig() const
{
    return config_;
}

void DamBreakScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    // Validate type and update.
    if (std::holds_alternative<Config::DamBreak>(newConfig)) {
        config_ = std::get<Config::DamBreak>(newConfig);
        spdlog::info("DamBreakScenario: Config updated");
    }
    else {
        spdlog::error("DamBreakScenario: Invalid config type provided");
    }
}

void DamBreakScenario::setup(World& world)
{
    spdlog::info("DamBreakScenario::setup - initializing world");

    // Clear world first.
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }

    // Reset state.
    damBroken_ = false;
    elapsedTime_ = 0.0;

    // Configure physics for dynamic pressure.
    world.getPhysicsSettings().gravity = 9.81;
    world.getPhysicsSettings().pressure_dynamic_enabled = true;
    world.getPhysicsSettings().pressure_dynamic_strength = 1.0;
    world.getPhysicsSettings().pressure_hydrostatic_enabled = false;
    world.getPhysicsSettings().pressure_hydrostatic_strength = 0.0;
    world.getPhysicsSettings().pressure_diffusion_strength = 1.0;
    world.getPhysicsSettings().pressure_scale = 1.0;

    // Create water column on left side - full height.
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 6; y++) {
            world.addMaterialAtCell(
                { static_cast<int16_t>(x), static_cast<int16_t>(y) },
                Material::EnumType::Water,
                1.0);
        }
    }

    // Create dam (wall) at x=2 - full height.
    for (int y = 0; y < 6; y++) {
        world.addMaterialAtCell({ 2, static_cast<int16_t>(y) }, Material::EnumType::Wall, 1.0);
    }

    spdlog::info("DamBreakScenario::setup complete - water at x=0-1, dam at x=2");
}

void DamBreakScenario::reset(World& world)
{
    spdlog::info("DamBreakScenario::reset");
    setup(world);
}

void DamBreakScenario::tick(World& world, double deltaTime)
{
    // Break dam automatically based on time if configured.
    if (!damBroken_ && config_.autoRelease) {
        elapsedTime_ += deltaTime;

        if (elapsedTime_ >= config_.releaseTime) {
            spdlog::info("DamBreakScenario: Breaking dam at t={:.2f}s", elapsedTime_);

            // Dam is at x=2, break only the bottom cell for realistic flow.
            world.getData().at(2, 5).clear(); // Bottom cell at (2,5).
            spdlog::info("DamBreakScenario: Dam broken at (2, 5)");
            damBroken_ = true;
        }
    }
}

} // namespace DirtSim
