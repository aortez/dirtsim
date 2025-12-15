#include "TreeGerminationScenario.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include <spdlog/spdlog.h>

namespace DirtSim {

TreeGerminationScenario::TreeGerminationScenario()
{
    metadata_.name = "Tree Germination";
    metadata_.description = "9x9 world with seed growing into balanced tree";
    metadata_.category = "organisms";
    metadata_.requiredWidth = 9;
    metadata_.requiredHeight = 9;
}

const ScenarioMetadata& TreeGerminationScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig TreeGerminationScenario::getConfig() const
{
    return config_;
}

void TreeGerminationScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    if (std::holds_alternative<EmptyConfig>(newConfig)) {
        config_ = std::get<EmptyConfig>(newConfig);
        spdlog::info("TreeGerminationScenario: Config updated");
    }
    else {
        spdlog::error("TreeGerminationScenario: Invalid config type provided");
    }
}

void TreeGerminationScenario::setup(World& world)
{
    spdlog::info(
        "TreeGerminationScenario::setup - creating 9x9 world with balanced tree growth");

    // Clear world to air.
    for (uint32_t y = 0; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }

    // Dirt at bottom 3 rows.
    for (uint32_t y = 6; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.addMaterialAtCell(x, y, MaterialType::DIRT, 1.0);
        }
    }

    // Plant seed in center for balanced growth demonstration.
    OrganismId tree_id = world.getOrganismManager().createTree(world, 4, 4);
    spdlog::info("TreeGerminationScenario: Planted seed {} at (4, 4)", tree_id);
}

void TreeGerminationScenario::reset(World& world)
{
    spdlog::info("TreeGerminationScenario::reset");
    setup(world);
}

void TreeGerminationScenario::tick(World& /*world*/, double /*deltaTime*/)
{
    // No dynamic particles - just watch the tree grow.
}

} // namespace DirtSim
