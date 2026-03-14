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
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {

namespace {
constexpr int kReferenceSoilDepth = 12;
constexpr int kReferenceWorldHeight = 32;

int resolveSoilDepth(int worldHeight)
{
    if (worldHeight <= 0) {
        return 0;
    }

    const int scaledDepth = static_cast<int>(std::lround(
        (static_cast<double>(worldHeight) * kReferenceSoilDepth) / kReferenceWorldHeight));
    return std::clamp(scaledDepth, 1, worldHeight);
}
} // namespace

TreeGerminationScenario::TreeGerminationScenario(GenomeRepository& genomeRepository)
    : genomeRepository_(genomeRepository)
{
    metadata_.kind = ScenarioKind::GridWorld;
    metadata_.name = "Tree Germination";
    metadata_.description = "32x32 world with seed growing into balanced tree";
    metadata_.category = "organisms";
    metadata_.requiredWidth = 32;
    metadata_.requiredHeight = 32;
    metadata_.deterministicEvaluation = true;
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
    spdlog::info(
        "TreeGerminationScenario::setup - configuring {}x{} world with balanced tree growth",
        world.getData().width,
        world.getData().height);

    // Clear world to air.
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell();
        }
    }

    // Scale soil depth from the canonical 32x32 layout for smaller test worlds.
    const int soilDepth = resolveSoilDepth(world.getData().height);
    const int soilStartY = std::max(0, world.getData().height - soilDepth);
    for (int y = soilStartY; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.addMaterialAtCell(
                { static_cast<int16_t>(x), static_cast<int16_t>(y) },
                Material::EnumType::Dirt,
                1.0);
        }
    }

    // Tree spawning is handled by training or external controllers.
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
