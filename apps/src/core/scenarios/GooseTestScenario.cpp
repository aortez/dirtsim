#include "GooseTestScenario.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/Goose.h"
#include "core/organisms/OrganismManager.h"
#include <spdlog/spdlog.h>

namespace DirtSim {

GooseTestScenario::GooseTestScenario()
{
    metadata_.name = "Goose Test";
    metadata_.description = "Test rigid body physics with a goose";
    metadata_.category = "organisms";
    metadata_.requiredWidth = 40;
    metadata_.requiredHeight = 30;
}

const ScenarioMetadata& GooseTestScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig GooseTestScenario::getConfig() const
{
    return Config::Empty{};
}

void GooseTestScenario::setConfig(const ScenarioConfig& /*newConfig*/, World& /*world*/)
{
    // No config for this scenario.
}

void GooseTestScenario::setup(World& world)
{
    spdlog::info("GooseTestScenario::setup - creating world with goose");

    WorldData& data = world.getData();

    // Clear world to air.
    for (uint32_t y = 0; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
            data.at(x, y) = Cell();
        }
    }

    // Create ground at bottom 5 rows.
    for (uint32_t y = data.height - 5; y < data.height; ++y) {
        for (uint32_t x = 0; x < data.width; ++x) {
            world.addMaterialAtCell(x, y, MaterialType::DIRT, 1.0);
        }
    }

    // Create goose in the middle, above ground.
    uint32_t goose_x = data.width / 2;
    uint32_t goose_y = data.height - 6; // Just above ground.
    goose_id_ = world.getOrganismManager().createGoose(world, goose_x, goose_y);

    spdlog::info(
        "GooseTestScenario: Created goose {} at ({}, {})", goose_id_, goose_x, goose_y);
}

void GooseTestScenario::reset(World& world)
{
    spdlog::info("GooseTestScenario::reset");
    setup(world);
}

void GooseTestScenario::tick(World& /*world*/, double /*deltaTime*/)
{
    // Entity sync is handled automatically by OrganismManager::syncEntitiesToWorldData().
}

} // namespace DirtSim
