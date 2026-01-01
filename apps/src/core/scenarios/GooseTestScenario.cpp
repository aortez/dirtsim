#include "GooseTestScenario.h"
#include "core/Cell.h"
#include "core/Entity.h"
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

void GooseTestScenario::tick(World& world, double /*deltaTime*/)
{
    // Create entity for rendering the goose.
    // Find the goose and update its entity representation.
    Goose* goose = world.getOrganismManager().getGoose(goose_id_);
    if (!goose) {
        return;
    }

    // Clear old entities and recreate.
    world.getData().entities.clear();

    Entity goose_entity;
    // FIXME: Entity::id should be EntityId (strong type), not uint32_t.
    // OrganismId and EntityId may need a defined relationship.
    goose_entity.id = goose_id_.get();
    goose_entity.type = EntityType::GOOSE;
    goose_entity.visible = true;

    Vector2i anchor = goose->getAnchorCell();
    goose_entity.position = Vector2<float>{
        static_cast<float>(anchor.x),
        static_cast<float>(anchor.y)
    };

    // Get COM offset from the continuous position.
    double frac_x = goose->position.x - std::floor(goose->position.x);
    double frac_y = goose->position.y - std::floor(goose->position.y);
    goose_entity.com = Vector2<float>{
        static_cast<float>(frac_x * 2.0 - 1.0),
        static_cast<float>(frac_y * 2.0 - 1.0)
    };

    goose_entity.velocity = Vector2<float>{
        static_cast<float>(goose->velocity.x),
        static_cast<float>(goose->velocity.y)
    };

    goose_entity.facing = goose->getFacing();
    goose_entity.mass = static_cast<float>(goose->mass);

    world.getData().entities.push_back(goose_entity);
}

} // namespace DirtSim
