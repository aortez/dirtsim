#include "TreeGerminationScenario.h"
#include "core/Cell.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/brains/NeuralNetBrain.h"
#include "core/organisms/brains/RuleBasedBrain.h"
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

void TreeGerminationScenario::setConfig(const ScenarioConfig& newConfig, World& world)
{
    if (!std::holds_alternative<Config::TreeGermination>(newConfig)) {
        spdlog::error("TreeGerminationScenario: Invalid config type provided");
        return;
    }

    const auto& cfg = std::get<Config::TreeGermination>(newConfig);

    // Check if brain type changed.
    if (cfg.brain_type != config_.brain_type) {
        Tree* tree = world.getOrganismManager().getTree(treeId_);
        if (tree) {
            std::unique_ptr<TreeBrain> brain;
            if (cfg.brain_type == Config::TreeBrainType::NEURAL_NET) {
                brain = std::make_unique<NeuralNetBrain>(cfg.neural_seed);
                spdlog::info(
                    "TreeGerminationScenario: Swapped to NeuralNetBrain (seed {})",
                    cfg.neural_seed);
            }
            else {
                brain = std::make_unique<RuleBasedBrain>();
                spdlog::info("TreeGerminationScenario: Swapped to RuleBasedBrain");
            }
            tree->setBrain(std::move(brain));
        }
    }

    config_ = cfg;
}

void TreeGerminationScenario::setup(World& world)
{
    spdlog::info("TreeGerminationScenario::setup - creating 9x9 world with balanced tree growth");

    // Clear world to air.
    for (uint32_t y = 0; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }

    // Dirt at bottom 3 rows.
    for (uint32_t y = 6; y < world.getData().height; ++y) {
        for (uint32_t x = 0; x < world.getData().width; ++x) {
            world.addMaterialAtCell(
                { static_cast<int16_t>(x), static_cast<int16_t>(y) }, MaterialType::DIRT, 1.0);
        }
    }

    // Create brain based on config.
    std::unique_ptr<TreeBrain> brain;
    if (config_.brain_type == Config::TreeBrainType::NEURAL_NET) {
        brain = std::make_unique<NeuralNetBrain>(config_.neural_seed);
        spdlog::info(
            "TreeGerminationScenario: Using NeuralNetBrain with seed {}", config_.neural_seed);
    }
    else {
        brain = std::make_unique<RuleBasedBrain>();
        spdlog::info("TreeGerminationScenario: Using RuleBasedBrain");
    }

    // Plant seed in center for balanced growth demonstration.
    treeId_ = world.getOrganismManager().createTree(world, 4, 4, std::move(brain));
    spdlog::info("TreeGerminationScenario: Planted seed {} at (4, 4)", treeId_);
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
