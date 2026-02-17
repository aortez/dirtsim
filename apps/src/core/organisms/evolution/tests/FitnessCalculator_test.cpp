#include "core/MaterialType.h"
#include "core/organisms/Tree.h"
#include "core/organisms/TreeCommandProcessor.h"
#include "core/organisms/TreeResourceTotals.h"
#include "core/organisms/brains/RuleBasedBrain.h"
#include "core/organisms/evolution/EvolutionConfig.h"
#include "core/organisms/evolution/FitnessCalculator.h"
#include "core/organisms/evolution/FitnessResult.h"
#include "core/organisms/evolution/OrganismTracker.h"
#include "core/organisms/evolution/TreeEvaluator.h"
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>

namespace DirtSim {

namespace {
EvolutionConfig makeConfig()
{
    EvolutionConfig config;
    config.maxSimulationTime = 20.0;
    config.energyReference = 100.0;
    config.waterReference = 100.0;
    return config;
}

std::unique_ptr<Tree> makeTree()
{
    return std::make_unique<Tree>(
        OrganismId{ 1 },
        std::make_unique<RuleBasedBrain>(),
        std::make_unique<TreeCommandProcessor>());
}

OrganismTrackingHistory makeHistory(std::initializer_list<Vector2d> positions)
{
    OrganismTrackingHistory history;
    double simTime = 0.0;
    for (const Vector2d& position : positions) {
        history.samples.push_back({ .simTime = simTime, .position = position });
        simTime += 0.016;
    }
    return history;
}
} // namespace

TEST(FitnessCalculatorTest, TreeFitnessIgnoresDistance)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory history = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 50.0, 0.0 },
        });
    const TreeResourceTotals resources{};

    const FitnessContext with_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .organismTrackingHistory = &history,
        .treeResources = &resources,
    };
    const FitnessContext without_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = nullptr,
        .treeResources = &resources,
    };

    const double fitness_with = computeFitnessForOrganism(with_context);
    const double fitness_without = computeFitnessForOrganism(without_context);

    EXPECT_DOUBLE_EQ(fitness_with, fitness_without);
}

TEST(FitnessCalculatorTest, TreeFitnessIncludesEnergy)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult low_energy{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const FitnessResult high_energy{ .lifespan = 10.0, .maxEnergy = 100.0 };
    auto tree = makeTree();
    tree->setEnergy(100.0);
    tree->addCellToLocalShape({ 0, -1 }, Material::EnumType::Wood, 1.0);
    tree->addCellToLocalShape({ 0, 1 }, Material::EnumType::Root, 1.0);
    tree->addCellToLocalShape({ 1, -1 }, Material::EnumType::Leaf, 1.0);

    const FitnessContext low_context{
        .result = low_energy,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &tree->getResourceTotals(),
    };
    const FitnessContext high_context{
        .result = high_energy,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &tree->getResourceTotals(),
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_GT(fitness_high, fitness_low);
}

TEST(FitnessCalculatorTest, TreeFitnessRewardsResourceCollection)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const TreeResourceTotals low_resources{ .waterAbsorbed = 0.0, .energyProduced = 0.0 };
    const TreeResourceTotals high_resources{ .waterAbsorbed = 100.0, .energyProduced = 100.0 };
    auto tree = makeTree();
    tree->addCellToLocalShape({ 0, -1 }, Material::EnumType::Wood, 1.0);
    tree->addCellToLocalShape({ 0, 1 }, Material::EnumType::Root, 1.0);
    tree->addCellToLocalShape({ 1, -1 }, Material::EnumType::Leaf, 1.0);

    const FitnessContext low_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &low_resources,
    };
    const FitnessContext high_context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &high_resources,
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_GT(fitness_high, fitness_low);
}

TEST(FitnessCalculatorTest, TreeResourceScoreRequiresMinimalStructure)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const TreeResourceTotals high_resources{ .waterAbsorbed = 100.0, .energyProduced = 100.0 };
    auto tree = makeTree();

    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &high_resources,
    };

    TreeEvaluator evaluator;
    const TreeFitnessBreakdown breakdown = evaluator.evaluateWithBreakdown(context);

    EXPECT_DOUBLE_EQ(breakdown.resourceScore, 0.0);
}

TEST(FitnessCalculatorTest, TreeHeldEnergyScoreRequiresMinimalStructure)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 100.0 };
    auto tree = makeTree();
    tree->setEnergy(100.0);

    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &tree->getResourceTotals(),
    };

    TreeEvaluator evaluator;
    const TreeFitnessBreakdown breakdown = evaluator.evaluateWithBreakdown(context);

    EXPECT_DOUBLE_EQ(breakdown.energyScore, 0.0);
}

TEST(FitnessCalculatorTest, TreeHeldEnergyScoreScalesAfterMinimalStructure)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 100.0 };
    auto tree = makeTree();
    tree->setEnergy(100.0);
    tree->addCellToLocalShape({ 0, -1 }, Material::EnumType::Wood, 1.0);
    tree->addCellToLocalShape({ 0, 1 }, Material::EnumType::Root, 1.0);
    tree->addCellToLocalShape({ 1, -1 }, Material::EnumType::Leaf, 1.0);

    const FitnessContext context{
        .result = result,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .finalOrganism = tree.get(),
        .treeResources = &tree->getResourceTotals(),
    };

    TreeEvaluator evaluator;
    const TreeFitnessBreakdown breakdown = evaluator.evaluateWithBreakdown(context);

    EXPECT_DOUBLE_EQ(breakdown.energyScore, 1.0);
}

TEST(FitnessCalculatorTest, TreeCommandScoreIsDisabled)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult oneAccepted{
        .lifespan = config.maxSimulationTime,
        .maxEnergy = 0.0,
        .commandsAccepted = 1,
    };
    const FitnessResult manyAccepted{
        .lifespan = config.maxSimulationTime,
        .maxEnergy = 0.0,
        .commandsAccepted = 42,
    };

    const FitnessContext oneContext{
        .result = oneAccepted,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const FitnessContext manyContext{
        .result = manyAccepted,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };

    TreeEvaluator evaluator;
    const TreeFitnessBreakdown oneBreakdown = evaluator.evaluateWithBreakdown(oneContext);
    const TreeFitnessBreakdown manyBreakdown = evaluator.evaluateWithBreakdown(manyContext);

    EXPECT_DOUBLE_EQ(oneBreakdown.commandScore, 0.0);
    EXPECT_DOUBLE_EQ(manyBreakdown.commandScore, 0.0);

    const FitnessResult manyRejects{
        .lifespan = config.maxSimulationTime,
        .maxEnergy = 0.0,
        .commandsRejected = 1234,
        .idleCancels = 999,
    };
    const FitnessContext rejectsContext{
        .result = manyRejects,
        .organismType = OrganismType::TREE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
    };
    const TreeFitnessBreakdown rejectsBreakdown = evaluator.evaluateWithBreakdown(rejectsContext);

    EXPECT_DOUBLE_EQ(rejectsBreakdown.commandScore, 0.0);
}

TEST(FitnessCalculatorTest, DuckFitnessIgnoresEnergy)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult low_energy{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const FitnessResult high_energy{ .lifespan = 10.0, .maxEnergy = 100.0 };
    const OrganismTrackingHistory history = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 5.0, 0.0 },
        });
    const FitnessContext low_context{
        .result = low_energy,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &history,
    };
    const FitnessContext high_context{
        .result = high_energy,
        .organismType = OrganismType::DUCK,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &history,
    };

    const double fitness_low = computeFitnessForOrganism(low_context);
    const double fitness_high = computeFitnessForOrganism(high_context);

    EXPECT_DOUBLE_EQ(fitness_low, fitness_high);
}

TEST(FitnessCalculatorTest, GooseFitnessRewardsDistance)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult static_move{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const FitnessResult moved{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory staticHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 0.0, 0.0 },
        });
    const OrganismTrackingHistory movedHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 10.0, 0.0 },
        });
    const FitnessContext static_context{
        .result = static_move,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &staticHistory,
    };
    const FitnessContext moved_context{
        .result = moved,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 10,
        .worldHeight = 10,
        .evolutionConfig = config,
        .organismTrackingHistory = &movedHistory,
    };

    const double fitness_static = computeFitnessForOrganism(static_context);
    const double fitness_moved = computeFitnessForOrganism(moved_context);

    EXPECT_GT(fitness_moved, fitness_static);
}

TEST(FitnessCalculatorTest, GooseFitnessUsesPathHistory)
{
    const EvolutionConfig config = makeConfig();
    const FitnessResult result{ .lifespan = 10.0, .maxEnergy = 0.0 };
    const OrganismTrackingHistory directHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 10.0, 0.0 },
        });
    const OrganismTrackingHistory longPathHistory = makeHistory(
        {
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 10.0, 0.0 },
            Vector2d{ 0.0, 0.0 },
            Vector2d{ 10.0, 0.0 },
        });

    const FitnessContext directContext{
        .result = result,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 1000,
        .worldHeight = 1000,
        .evolutionConfig = config,
        .organismTrackingHistory = &directHistory,
    };
    const FitnessContext longPathContext{
        .result = result,
        .organismType = OrganismType::GOOSE,
        .worldWidth = 1000,
        .worldHeight = 1000,
        .evolutionConfig = config,
        .organismTrackingHistory = &longPathHistory,
    };

    const double directFitness = computeFitnessForOrganism(directContext);
    const double longPathFitness = computeFitnessForOrganism(longPathContext);

    EXPECT_GT(longPathFitness, directFitness);
}

} // namespace DirtSim
