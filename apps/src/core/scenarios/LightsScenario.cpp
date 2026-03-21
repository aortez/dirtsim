#include "LightsScenario.h"
#include "core/Assert.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/PhysicsSettings.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/water/WaterVolumeView.h"
#include "spdlog/spdlog.h"

#include <algorithm>

namespace DirtSim {

LightsScenario::LightsScenario()
{
    metadata_.kind = ScenarioKind::GridWorld;
    metadata_.name = "Lights";
    metadata_.description = "Test lighting system with water, metal, leaf, and dirt materials";
    metadata_.category = "test";
    metadata_.requiredWidth = 20;
    metadata_.requiredHeight = 20;
}

const ScenarioMetadata& LightsScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig LightsScenario::getConfig() const
{
    return config_;
}

void LightsScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    // Validate type and update.
    if (std::holds_alternative<Config::Lights>(newConfig)) {
        config_ = std::get<Config::Lights>(newConfig);
        spdlog::info("LightsScenario: Config updated");
    }
    else {
        spdlog::error("LightsScenario: Invalid config type provided");
    }
}

void LightsScenario::setup(World& world)
{
    spdlog::info("LightsScenario::setup - initializing world");

    // Clear world first.
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }

    WaterVolumeMutableView waterVolume{};
    if (world.tryGetMutableWaterVolumeView(waterVolume)
        && waterVolume.width == world.getData().width
        && waterVolume.height == world.getData().height) {
        std::fill(waterVolume.volume.begin(), waterVolume.volume.end(), 0.0f);
    }

    // Enable sun with full light.
    world.getPhysicsSettings().light.sun_intensity = 1.0;

    // Bottom row: 4 5x5 groups of materials.
    // Water (x=0-4, y=15-19).
    for (int y = 15; y <= 19; ++y) {
        for (int x = 0; x <= 4; ++x) {
            if (world.getData().inBounds(x, y)) {
                world.replaceMaterialAtCell(
                    Vector2s{ static_cast<int16_t>(x), static_cast<int16_t>(y) },
                    Material::EnumType::Water);
            }
        }
    }

    // Metal (x=5-9, y=15-19).
    for (int y = 15; y <= 19; ++y) {
        for (int x = 5; x <= 9; ++x) {
            if (world.getData().inBounds(x, y)) {
                world.getData().at(x, y).replaceMaterial(Material::EnumType::Metal, 1.0);
            }
        }
    }

    // Leaf (x=10-14, y=15-19).
    for (int y = 15; y <= 19; ++y) {
        for (int x = 10; x <= 14; ++x) {
            if (world.getData().inBounds(x, y)) {
                world.getData().at(x, y).replaceMaterial(Material::EnumType::Leaf, 1.0);
            }
        }
    }

    // Dirt (x=15-19, y=15-19).
    for (int y = 15; y <= 19; ++y) {
        for (int x = 15; x <= 19; ++x) {
            if (world.getData().inBounds(x, y)) {
                world.getData().at(x, y).replaceMaterial(Material::EnumType::Dirt, 1.0);
            }
        }
    }

    spdlog::info("LightsScenario::setup complete");
}

void LightsScenario::reset(World& world)
{
    spdlog::info("LightsScenario::reset");
    setup(world);
}

void LightsScenario::tick(World& world, double deltaTime)
{
    (void)world;
    (void)deltaTime;
}

} // namespace DirtSim
